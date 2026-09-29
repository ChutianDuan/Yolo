#!/usr/bin/env python3
"""Evaluate ONNX detectors on a hashed BDD100K YOLO validation image subset."""

from __future__ import annotations

import argparse
import json
import math
import platform
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import cv2
import numpy as np
import openvino as ov

import evaluate_bdd100k_openvino_quantization as evaluation


class ValidationEvaluationError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--model",
        action="append",
        required=True,
        metavar="NAME=PATH",
        help="Named ONNX model; repeat to compare candidates.",
    )
    parser.add_argument(
        "--runtime",
        action="append",
        default=[],
        metavar="NAME=RUNTIME",
        help="Runtime for a named model: openvino (default) or onnxruntime.",
    )
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--dataset-dir", type=Path, required=True)
    parser.add_argument("--classes", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--device", default="CPU")
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--iou-threshold", type=float, default=0.5)
    parser.add_argument("--nms-iou-threshold", type=float, default=0.45)
    parser.add_argument("--minimum-score", type=float, default=0.001)
    parser.add_argument(
        "--score-thresholds",
        default="0.01,0.05,0.10,0.15,0.20,0.25",
    )
    return parser.parse_args()


def load_json(path: Path, description: str) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValidationEvaluationError(f"Invalid {description}: {path}") from exc
    if not isinstance(payload, dict):
        raise ValidationEvaluationError(f"{description} must be a JSON object: {path}")
    return payload


def source_path(dataset_dir: Path, value: Any, description: str) -> Path:
    if not isinstance(value, str):
        raise ValidationEvaluationError(f"Invalid {description} path")
    relative = Path(value)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValidationEvaluationError(f"{description} path must stay relative: {value}")
    path = dataset_dir / relative
    if not path.is_file():
        raise ValidationEvaluationError(f"{description} file is missing: {path}")
    return path


def load_validation_ground_truth(
    manifest_path: Path,
    dataset_dir: Path,
    class_names: list[str],
) -> tuple[
    dict[str, Path],
    dict[str, dict[int, list[evaluation.ObjectRecord]]],
    dict[str, dict[int, list[evaluation.ObjectRecord]]],
    dict[str, int],
    dict[str, Any],
]:
    manifest = load_json(manifest_path, "manifest")
    root = manifest_path.parent
    try:
        evaluation.verify_manifest_files(root, manifest)
    except evaluation.QuantizationEvaluationError as exc:
        raise ValidationEvaluationError(str(exc)) from exc

    samples_path = root / "validation_samples.json"
    payload = load_json(samples_path, "validation samples")
    if payload.get("schema_version") != 1:
        raise ValidationEvaluationError("Unsupported validation samples schema")
    dataset = payload.get("dataset")
    if not isinstance(dataset, dict) or dataset.get("split") != "val":
        raise ValidationEvaluationError("Validation samples must declare split=val")
    if dataset.get("classes") != class_names:
        raise ValidationEvaluationError("Validation classes do not match --classes")

    raw_samples = payload.get("samples")
    if not isinstance(raw_samples, list) or not raw_samples:
        raise ValidationEvaluationError("Validation samples are empty")

    images: dict[str, Path] = {}
    labels: dict[str, dict[int, list[evaluation.ObjectRecord]]] = {}
    crowds: dict[str, dict[int, list[evaluation.ObjectRecord]]] = {}
    strides: dict[str, int] = {}
    for sample in raw_samples:
        if not isinstance(sample, dict):
            raise ValidationEvaluationError("Invalid validation sample entry")
        sample_id = str(sample.get("sample_id", "")).strip()
        if not sample_id or sample_id in images:
            raise ValidationEvaluationError(f"Duplicate or empty sample ID: {sample_id}")
        image_path = source_path(dataset_dir, sample.get("image"), "image")
        label_path = source_path(dataset_dir, sample.get("label"), "label")
        if evaluation.sha256_file(image_path) != sample.get("image_sha256"):
            raise ValidationEvaluationError(f"Image hash mismatch: {image_path}")
        if evaluation.sha256_file(label_path) != sample.get("label_sha256"):
            raise ValidationEvaluationError(f"Label hash mismatch: {label_path}")

        image_width = int(sample.get("image_width", 0))
        image_height = int(sample.get("image_height", 0))
        if image_width <= 0 or image_height <= 0:
            raise ValidationEvaluationError(f"Invalid image dimensions: {sample_id}")
        records: list[evaluation.ObjectRecord] = []
        raw_labels = sample.get("labels")
        if not isinstance(raw_labels, list):
            raise ValidationEvaluationError(f"Invalid labels: {sample_id}")
        for label_index, item in enumerate(raw_labels):
            if not isinstance(item, dict):
                raise ValidationEvaluationError(f"Invalid label entry: {sample_id}")
            class_id = int(item.get("class_id", -1))
            if not 0 <= class_id < len(class_names):
                raise ValidationEvaluationError(f"Class ID out of range: {sample_id}")
            if item.get("class_name") != class_names[class_id]:
                raise ValidationEvaluationError(f"Class name mismatch: {sample_id}")
            raw_box = item.get("box_xyxy")
            if not isinstance(raw_box, list) or len(raw_box) != 4:
                raise ValidationEvaluationError(f"Invalid box: {sample_id}")
            box = tuple(float(value) for value in raw_box)
            if (
                not all(math.isfinite(value) for value in box)
                or box[0] < 0.0
                or box[1] < 0.0
                or box[2] > image_width
                or box[3] > image_height
                or box[2] <= box[0]
                or box[3] <= box[1]
            ):
                raise ValidationEvaluationError(f"Box out of bounds: {sample_id}")
            records.append(
                evaluation.ObjectRecord(
                    0,
                    f"{sample_id}-{label_index}",
                    class_names[class_id],
                    box,
                )
            )
        images[sample_id] = image_path
        labels[sample_id] = {0: records}
        crowds[sample_id] = {0: []}
        strides[sample_id] = 1

    declared_count = int(payload.get("selection", {}).get("selected_images", -1))
    if declared_count != len(images):
        raise ValidationEvaluationError(
            f"Selected image count mismatch: declared={declared_count}, actual={len(images)}"
        )
    return images, labels, crowds, strides, payload


def markdown_report(report: dict[str, Any]) -> str:
    selection = report["inputs"]["selection"]
    lines = [
        "# BDD100K YOLO validation comparison",
        "",
        f"Generated: {report['generated_at']}",
        "",
        f"Subset: {selection['selected_images']} validation images; "
        f"method={selection['method']}; seed={selection['seed']}; "
        f"forced={','.join(selection['forced_classes'])}.",
        "",
        "| Model | Runtime | Score | mAP50 | Precision | Recall | F1 | Mean infer ms | P95 infer ms |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for model_name, data in report["models"].items():
        latency = data["model"]["latency"]
        for threshold, point in data["evaluation"]["operating_points"].items():
            metrics = point["pooled"]
            lines.append(
                f"| {model_name} | {data['model']['runtime']} | {threshold} | "
                f"{metrics['map50']:.6f} | {metrics['precision']:.6f} | "
                f"{metrics['recall']:.6f} | {metrics['f1']:.6f} | "
                f"{latency['mean_ms']:.3f} | {latency['p95_ms']:.3f} |"
            )
    lines.extend(
        [
            "",
            "## Per-class measured best F1",
            "",
            "AP50 uses the minimum-score candidate floor. Best score is selected only from "
            "the measured grid.",
            "",
            "| Model | Runtime | Class | GT | AP50 | Best score | Precision | Recall | F1 |",
            "|---|---|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for model_name, data in report["models"].items():
        for item in evaluation.best_class_operating_points(data["evaluation"]):
            lines.append(
                f"| {model_name} | {data['model']['runtime']} | {item['class_name']} | "
                f"{item['ground_truth']} | {item['ap50']:.6f} | "
                f"{item['threshold']:.3f} | {item['precision']:.6f} | "
                f"{item['recall']:.6f} | {item['f1']:.6f} |"
            )
    lines.extend(
        [
            "",
            "Notes: this subset is independent from train, but forced-class enrichment means "
            "pooled metrics are diagnostic rather than an unbiased full-validation estimate. "
            "YOLO detection labels have no crowd/distractor regions. Inference latency excludes "
            "image loading, preprocessing, decode and NMS.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    if args.threads <= 0:
        raise ValidationEvaluationError("--threads must be positive")
    if not 0.0 < args.iou_threshold <= 1.0 or not 0.0 < args.nms_iou_threshold <= 1.0:
        raise ValidationEvaluationError("IoU thresholds must be in (0, 1]")

    models = evaluation.parse_named_paths(args.model, "--model")
    runtimes = evaluation.parse_named_runtimes(
        args.runtime,
        {name for name, _ in models},
    )
    thresholds = evaluation.parse_thresholds(args.score_thresholds, args.minimum_score)
    class_names = evaluation.load_classes(args.classes.resolve())
    manifest_path = args.manifest.expanduser().resolve()
    dataset_dir = args.dataset_dir.expanduser().resolve()
    images, labels, crowds, strides, samples = load_validation_ground_truth(
        manifest_path,
        dataset_dir,
        class_names,
    )
    output_dir = args.output_dir.expanduser().resolve()
    if output_dir.exists():
        raise ValidationEvaluationError(f"Output directory already exists: {output_dir}")

    report: dict[str, Any] = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "environment": {
            "python": platform.python_version(),
            "openvino": getattr(ov, "__version__", "unknown"),
            "opencv": cv2.__version__,
            "numpy": np.__version__,
            "device": args.device,
            "inference_threads": args.threads,
        },
        "inputs": {
            "manifest_path": str(manifest_path),
            "manifest_sha256": evaluation.sha256_file(manifest_path),
            "samples_sha256": evaluation.sha256_file(
                manifest_path.parent / "validation_samples.json"
            ),
            "dataset_dir": str(dataset_dir),
            "selection": samples["selection"],
            "candidate_images": samples["dataset"]["candidate_images"],
            "evaluated_images": len(images),
            "evaluated_ground_truth_objects": sum(
                len(frame_items)
                for sample_frames in labels.values()
                for frame_items in sample_frames.values()
            ),
        },
        "configuration": {
            "iou_threshold": args.iou_threshold,
            "nms_iou_threshold": args.nms_iou_threshold,
            "minimum_score": args.minimum_score,
            "score_thresholds": thresholds,
            "ignored_regions": "none; YOLO detection labels do not encode crowd/distractor",
            "preprocessing": "C++-aligned BGR letterbox 114, RGB NCHW float32 / 255",
        },
        "models": {},
    }
    for model_name, model_path in models:
        runtime = runtimes[model_name]
        print(
            f"evaluating model={model_name} runtime={runtime} samples={len(images)}",
            flush=True,
        )
        predictions, model_metadata = evaluation.infer_model(
            model_name,
            runtime,
            model_path,
            images,
            labels,
            strides,
            class_names,
            args,
            canonicalize_classes=False,
        )
        report["models"][model_name] = {
            "model": model_metadata,
            "evaluation": evaluation.evaluate_predictions(
                predictions,
                labels,
                crowds,
                thresholds,
                args.minimum_score,
                args.iou_threshold,
                include_per_source=False,
            ),
        }

    baseline = models[0][0]
    report["comparison"] = {
        "baseline": baseline,
        "deltas": evaluation.model_deltas(report["models"], baseline),
    }

    output_dir.mkdir(parents=True)
    json_path = output_dir / "validation_evaluation.json"
    markdown_path = output_dir / "validation_evaluation.md"
    json_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    markdown_path.write_text(markdown_report(report), encoding="utf-8")
    output_manifest = {
        "schema_version": 1,
        "files": [
            {"path": json_path.name, "sha256": evaluation.sha256_file(json_path)},
            {"path": markdown_path.name, "sha256": evaluation.sha256_file(markdown_path)},
        ],
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(output_manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"report={json_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
