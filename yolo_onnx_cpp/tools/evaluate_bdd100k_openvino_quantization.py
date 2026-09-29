#!/usr/bin/env python3
"""Compare FP32/INT8 detectors on aligned BDD100K label frames.

The evaluator mirrors the C++ letterbox, decode and class-aware NMS path.  It
uses the immutable TrackEval ground-truth export to avoid rescanning the large
source CSV, but reports detection AP/P/R/F1 rather than tracking metrics. A
per-model runtime switch isolates ONNX Runtime/OpenVINO compatibility issues.
"""

from __future__ import annotations

import argparse
import json
import math
import platform
import sys
import time
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any, Iterable

import cv2
import numpy as np
import openvino as ov

from evaluate_bdd100k_tracking import (
    ObjectRecord,
    box_iou,
    canonical_class,
    detection_metrics,
    percentile,
    sha256_file,
)

DISTRACTOR_CATEGORIES = frozenset({"other person", "trailer", "other vehicle"})
IGNORE_REGION_CLASS = "__ignore_region__"


class QuantizationEvaluationError(RuntimeError):
    pass


@dataclass(frozen=True)
class LetterboxTransform:
    image_width: int
    image_height: int
    scale_x: float
    scale_y: float
    pad_w: float
    pad_h: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--model",
        action="append",
        required=True,
        metavar="NAME=PATH",
        help="Named ONNX model; repeat for FP32 and INT8.",
    )
    parser.add_argument(
        "--runtime",
        action="append",
        default=[],
        metavar="NAME=RUNTIME",
        help="Runtime for a named model: openvino (default) or onnxruntime.",
    )
    parser.add_argument(
        "--video",
        action="append",
        required=True,
        metavar="NAME=PATH",
        help="Named source video matching a manifest video; may be repeated.",
    )
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--classes", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--device", default="CPU")
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--iou-threshold", type=float, default=0.5)
    parser.add_argument("--nms-iou-threshold", type=float, default=0.45)
    parser.add_argument("--minimum-score", type=float, default=0.001)
    parser.add_argument(
        "--score-thresholds",
        default="0.05,0.10,0.15,0.20,0.25",
        help="Comma-separated operating points; AP also uses all candidates >= minimum-score.",
    )
    return parser.parse_args()


def parse_named_paths(values: Iterable[str], option: str) -> list[tuple[str, Path]]:
    result: list[tuple[str, Path]] = []
    names: set[str] = set()
    for value in values:
        name, separator, raw_path = value.partition("=")
        name = name.strip()
        if not separator or not name or not raw_path.strip():
            raise QuantizationEvaluationError(
                f"Invalid {option} value {value!r}; expected NAME=PATH"
            )
        if name in names:
            raise QuantizationEvaluationError(f"Duplicate {option} name: {name}")
        path = Path(raw_path).expanduser().resolve()
        if not path.is_file():
            raise QuantizationEvaluationError(f"{option} file does not exist: {path}")
        names.add(name)
        result.append((name, path))
    return result


def parse_named_runtimes(
    values: Iterable[str], model_names: set[str]
) -> dict[str, str]:
    result = {name: "openvino" for name in model_names}
    assigned: set[str] = set()
    for value in values:
        name, separator, raw_runtime = value.partition("=")
        name = name.strip()
        runtime = raw_runtime.strip().lower()
        if not separator or not name or not runtime:
            raise QuantizationEvaluationError(
                f"Invalid --runtime value {value!r}; expected NAME=RUNTIME"
            )
        if name not in model_names:
            raise QuantizationEvaluationError(f"Runtime names unknown model: {name}")
        if name in assigned:
            raise QuantizationEvaluationError(f"Duplicate --runtime name: {name}")
        if runtime not in {"openvino", "onnxruntime"}:
            raise QuantizationEvaluationError(f"Unsupported runtime: {runtime}")
        assigned.add(name)
        result[name] = runtime
    return result


def parse_thresholds(value: str, minimum_score: float) -> list[float]:
    try:
        values = sorted({float(item.strip()) for item in value.split(",") if item.strip()})
    except ValueError as exc:
        raise QuantizationEvaluationError("Invalid --score-thresholds") from exc
    if not values or minimum_score < 0.0 or minimum_score > 1.0:
        raise QuantizationEvaluationError("Thresholds must be in [0, 1]")
    if any(item < minimum_score or item > 1.0 for item in values):
        raise QuantizationEvaluationError(
            "Every score threshold must be within [minimum-score, 1]"
        )
    return values


def load_classes(path: Path) -> list[str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise QuantizationEvaluationError(f"Invalid classes JSON: {path}") from exc
    if isinstance(payload, list):
        result = [str(item) for item in payload]
    elif isinstance(payload, dict):
        try:
            indexed = {int(key): str(value) for key, value in payload.items()}
        except (TypeError, ValueError) as exc:
            raise QuantizationEvaluationError("Class IDs must be integers") from exc
        if not indexed or sorted(indexed) != list(range(max(indexed) + 1)):
            raise QuantizationEvaluationError("Class IDs must be contiguous from zero")
        result = [indexed[index] for index in range(len(indexed))]
    else:
        raise QuantizationEvaluationError("Classes JSON must be an array or object")
    if not result:
        raise QuantizationEvaluationError("Classes JSON is empty")
    return result


def verify_manifest_files(root: Path, manifest: dict[str, Any]) -> None:
    for item in manifest.get("files", []):
        relative = item.get("path")
        expected = item.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected, str):
            raise QuantizationEvaluationError("Manifest file entry is invalid")
        path = root / relative
        if not path.is_file() or sha256_file(path) != expected:
            raise QuantizationEvaluationError(f"Manifest file verification failed: {path}")


def load_ground_truth(
    manifest_path: Path,
) -> tuple[
    dict[str, dict[int, list[ObjectRecord]]],
    dict[str, dict[int, list[ObjectRecord]]],
    dict[str, int],
    dict[str, Any],
]:
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise QuantizationEvaluationError(f"Invalid manifest: {manifest_path}") from exc
    root = manifest_path.resolve().parent
    verify_manifest_files(root, manifest)
    video_metadata = manifest.get("videos")
    if not isinstance(video_metadata, dict) or not video_metadata:
        raise QuantizationEvaluationError("Manifest has no videos")

    normal: dict[str, dict[int, list[ObjectRecord]]] = {}
    crowd: dict[str, dict[int, list[ObjectRecord]]] = {}
    strides: dict[str, int] = {}
    for video_name, metadata in video_metadata.items():
        if not isinstance(metadata, dict):
            raise QuantizationEvaluationError(f"Invalid video metadata: {video_name}")
        stride = int(metadata.get("frame_stride", 0))
        if stride <= 0:
            raise QuantizationEvaluationError(f"Invalid frame stride for {video_name}")
        gt_path = root / "gt" / f"{video_name}.json"
        try:
            frames = json.loads(gt_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise QuantizationEvaluationError(f"Invalid ground truth: {gt_path}") from exc
        normal_frames: dict[int, list[ObjectRecord]] = defaultdict(list)
        crowd_frames: dict[int, list[ObjectRecord]] = defaultdict(list)
        for frame in frames:
            frame_index = int(frame["index"])
            normal_frames.setdefault(frame_index, [])
            crowd_frames.setdefault(frame_index, [])
            for item in frame.get("labels", []):
                category = str(item.get("category", "")).strip().lower()
                class_name = canonical_class(category)
                is_distractor = category in DISTRACTOR_CATEGORIES
                box_value = item.get("box2d")
                if (
                    (class_name is None and not is_distractor)
                    or not isinstance(box_value, dict)
                ):
                    continue
                box = tuple(float(box_value[key]) for key in ("x1", "y1", "x2", "y2"))
                if box[2] <= box[0] or box[3] <= box[1]:
                    continue
                record = ObjectRecord(
                    frame_index,
                    str(item.get("id", "")),
                    IGNORE_REGION_CLASS if is_distractor else class_name,
                    box,
                )
                attributes = item.get("attributes") or {}
                is_crowd = bool(attributes.get("Crowd", attributes.get("crowd", False)))
                target = crowd_frames if is_crowd or is_distractor else normal_frames
                target[frame_index].append(record)
        expected_frames = int(metadata.get("evaluated_label_frames", -1))
        if expected_frames != len(normal_frames):
            raise QuantizationEvaluationError(
                f"Ground-truth frame mismatch for {video_name}: "
                f"manifest={expected_frames}, json={len(normal_frames)}"
            )
        normal[video_name] = dict(normal_frames)
        crowd[video_name] = dict(crowd_frames)
        strides[video_name] = stride
    return normal, crowd, strides, manifest


def letterbox_preprocess(
    image: np.ndarray,
    input_width: int,
    input_height: int,
) -> tuple[np.ndarray, LetterboxTransform]:
    if image.ndim != 3 or image.shape[2] != 3:
        raise QuantizationEvaluationError("Expected a BGR HWC image")
    image_height, image_width = image.shape[:2]
    ratio = min(input_width / image_width, input_height / image_height)
    resized_width = min(input_width, max(1, int(math.floor(image_width * ratio + 0.5))))
    resized_height = min(input_height, max(1, int(math.floor(image_height * ratio + 0.5))))
    resized = (
        image
        if (resized_width, resized_height) == (image_width, image_height)
        else cv2.resize(image, (resized_width, resized_height))
    )
    pad_width = input_width - resized_width
    pad_height = input_height - resized_height
    left = pad_width // 2
    top = pad_height // 2
    padded = cv2.copyMakeBorder(
        resized,
        top,
        pad_height - top,
        left,
        pad_width - left,
        cv2.BORDER_CONSTANT,
        value=(114, 114, 114),
    )
    rgb = cv2.cvtColor(padded, cv2.COLOR_BGR2RGB)
    tensor = np.expand_dims(
        np.transpose(rgb, (2, 0, 1)).astype(np.float32) / np.float32(255.0),
        axis=0,
    )
    transform = LetterboxTransform(
        image_width=image_width,
        image_height=image_height,
        scale_x=resized_width / image_width,
        scale_y=resized_height / image_height,
        pad_w=float(left),
        pad_h=float(top),
    )
    return tensor, transform


def clip_box(
    box: tuple[float, float, float, float], transform: LetterboxTransform
) -> tuple[float, float, float, float]:
    x1, y1, x2, y2 = box
    return (
        min(max((x1 - transform.pad_w) / transform.scale_x, 0.0), transform.image_width),
        min(max((y1 - transform.pad_h) / transform.scale_y, 0.0), transform.image_height),
        min(max((x2 - transform.pad_w) / transform.scale_x, 0.0), transform.image_width),
        min(max((y2 - transform.pad_h) / transform.scale_y, 0.0), transform.image_height),
    )


def class_aware_nms(
    detections: list[tuple[int, float, tuple[float, float, float, float]]],
    iou_threshold: float,
    max_candidates: int = 3000,
    max_detections: int = 300,
) -> list[tuple[int, float, tuple[float, float, float, float]]]:
    ordered = sorted(detections, key=lambda item: item[1], reverse=True)[:max_candidates]
    removed = [False] * len(ordered)
    kept: list[tuple[int, float, tuple[float, float, float, float]]] = []
    for index, current in enumerate(ordered):
        if removed[index]:
            continue
        kept.append(current)
        if len(kept) >= max_detections:
            break
        for other_index in range(index + 1, len(ordered)):
            other = ordered[other_index]
            if (
                not removed[other_index]
                and current[0] == other[0]
                and box_iou(current[2], other[2]) > iou_threshold
            ):
                removed[other_index] = True
    return kept


def decode_output(
    output: np.ndarray,
    transform: LetterboxTransform,
    class_names: list[str],
    score_threshold: float,
    nms_iou_threshold: float,
) -> list[tuple[int, float, tuple[float, float, float, float]]]:
    values = np.asarray(output, dtype=np.float32)
    if values.ndim != 3 or values.shape[0] != 1 or min(values.shape[1:]) <= 0:
        raise QuantizationEvaluationError(f"Unsupported output shape: {list(values.shape)}")
    candidates: list[tuple[int, float, tuple[float, float, float, float]]] = []
    if values.shape[1:] == (300, 6):
        for row in values[0]:
            rounded_class = round(float(row[5]))
            score = float(row[4])
            if (
                not math.isfinite(score)
                or score < score_threshold
                or rounded_class < 0
                or rounded_class >= len(class_names)
            ):
                continue
            box = clip_box(tuple(float(value) for value in row[:4]), transform)
            if box[2] > box[0] and box[3] > box[1]:
                candidates.append((rounded_class, score, box))
    else:
        channel_first = values.shape[1] < values.shape[2]
        matrix = values[0].T if channel_first else values[0]
        feature_count = matrix.shape[1]
        has_objectness = feature_count == len(class_names) + 5
        class_start = 5 if has_objectness else 4
        if feature_count < class_start + len(class_names):
            raise QuantizationEvaluationError(f"Unsupported output shape: {list(values.shape)}")
        for row in matrix:
            class_id = int(np.argmax(row[class_start : class_start + len(class_names)]))
            score = float(row[class_start + class_id])
            if has_objectness:
                score *= float(row[4])
            if not math.isfinite(score) or score < score_threshold:
                continue
            cx, cy, width, height = (float(value) for value in row[:4])
            box = clip_box(
                (
                    cx - width * 0.5,
                    cy - height * 0.5,
                    cx + width * 0.5,
                    cy + height * 0.5,
                ),
                transform,
            )
            if box[2] > box[0] and box[3] > box[1]:
                candidates.append((class_id, score, box))
    return class_aware_nms(candidates, nms_iou_threshold)


def static_input_shape(compiled_model: Any) -> tuple[int, int]:
    shape = list(compiled_model.input(0).shape)
    if len(shape) != 4 or shape[0] != 1 or shape[1] != 3:
        raise QuantizationEvaluationError(f"Expected static NCHW model input, got {shape}")
    return int(shape[3]), int(shape[2])


def infer_model(
    model_name: str,
    runtime: str,
    model_path: Path,
    videos: dict[str, Path],
    labels: dict[str, dict[int, list[ObjectRecord]]],
    strides: dict[str, int],
    class_names: list[str],
    args: argparse.Namespace,
    canonicalize_classes: bool = True,
) -> tuple[dict[str, dict[int, list[ObjectRecord]]], dict[str, Any]]:
    load_started = time.perf_counter()
    runtime_version: str
    if runtime == "openvino":
        core = ov.Core()
        model = core.read_model(str(model_path))
        compiled = core.compile_model(
            model,
            args.device,
            {"INFERENCE_NUM_THREADS": args.threads, "PERFORMANCE_HINT": "LATENCY"},
        )
        input_width, input_height = static_input_shape(compiled)
        request = compiled.create_infer_request()
        input_port = compiled.input(0)
        output_port = compiled.output(0)
        output_shape = list(output_port.shape)
        runtime_version = getattr(ov, "__version__", "unknown")

        def run_inference(tensor: np.ndarray) -> np.ndarray:
            return np.asarray(request.infer({input_port: tensor})[output_port])

    elif runtime == "onnxruntime":
        import onnxruntime as ort

        options = ort.SessionOptions()
        options.intra_op_num_threads = args.threads
        options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        session = ort.InferenceSession(
            str(model_path), options, providers=["CPUExecutionProvider"]
        )
        input_info = session.get_inputs()[0]
        output_info = session.get_outputs()[0]
        input_shape = input_info.shape
        if (
            len(input_shape) != 4
            or input_shape[0] != 1
            or input_shape[1] != 3
            or not all(isinstance(value, int) and value > 0 for value in input_shape)
        ):
            raise QuantizationEvaluationError(
                f"Expected static NCHW model input, got {input_shape}"
            )
        input_width, input_height = int(input_shape[3]), int(input_shape[2])
        output_shape = [int(value) for value in output_info.shape]
        runtime_version = ort.__version__

        def run_inference(tensor: np.ndarray) -> np.ndarray:
            return np.asarray(session.run([output_info.name], {input_info.name: tensor})[0])

    else:
        raise QuantizationEvaluationError(f"Unsupported runtime: {runtime}")
    load_ms = (time.perf_counter() - load_started) * 1000.0
    predictions: dict[str, dict[int, list[ObjectRecord]]] = {}
    inference_ms: list[float] = []
    decoded_candidates = 0

    for video_name, video_path in videos.items():
        frame_labels = labels[video_name]
        source_to_label = {
            label_index * strides[video_name]: label_index for label_index in frame_labels
        }
        maximum_source_frame = max(source_to_label)
        capture = cv2.VideoCapture(str(video_path))
        if not capture.isOpened():
            raise QuantizationEvaluationError(f"Failed to open video: {video_path}")
        video_predictions: dict[int, list[ObjectRecord]] = {}
        source_frame = 0
        try:
            while source_frame <= maximum_source_frame:
                ok, image = capture.read()
                if not ok or image is None:
                    raise QuantizationEvaluationError(
                        f"Video ended before source frame {maximum_source_frame}: {video_path}"
                    )
                label_index = source_to_label.get(source_frame)
                if label_index is not None:
                    tensor, transform = letterbox_preprocess(image, input_width, input_height)
                    started = time.perf_counter()
                    output = run_inference(tensor)
                    inference_ms.append((time.perf_counter() - started) * 1000.0)
                    detections = decode_output(
                        output,
                        transform,
                        class_names,
                        args.minimum_score,
                        args.nms_iou_threshold,
                    )
                    records: list[ObjectRecord] = []
                    for item_index, (class_id, score, box) in enumerate(detections):
                        output_class = (
                            canonical_class(class_names[class_id])
                            if canonicalize_classes
                            else class_names[class_id]
                        )
                        if output_class is None:
                            continue
                        records.append(
                            ObjectRecord(
                                label_index,
                                f"{model_name}-{label_index}-{item_index}",
                                output_class,
                                box,
                                score,
                            )
                        )
                    decoded_candidates += len(records)
                    video_predictions[label_index] = records
                source_frame += 1
        finally:
            capture.release()
        if set(video_predictions) != set(frame_labels):
            raise QuantizationEvaluationError(f"Incomplete inference for video {video_name}")
        predictions[video_name] = video_predictions

    latency = {
        "sample_count": len(inference_ms),
        "mean_ms": round(mean(inference_ms), 6),
        "p50_ms": round(percentile(inference_ms, 0.5), 6),
        "p95_ms": round(percentile(inference_ms, 0.95), 6),
        "maximum_ms": round(max(inference_ms), 6),
    }
    metadata = {
        "runtime": runtime,
        "runtime_version": runtime_version,
        "path": str(model_path),
        "file_size_bytes": model_path.stat().st_size,
        "sha256": sha256_file(model_path),
        "input_shape": [1, 3, input_height, input_width],
        "output_shape": output_shape,
        "compile_ms": round(load_ms, 6),
        "latency": latency,
        "decoded_supported_candidates": decoded_candidates,
    }
    return predictions, metadata


def suppress_crowd_predictions(
    predictions: dict[int, list[ObjectRecord]],
    labels: dict[int, list[ObjectRecord]],
    crowds: dict[int, list[ObjectRecord]],
    iou_threshold: float,
) -> tuple[dict[int, list[ObjectRecord]], int]:
    result: dict[int, list[ObjectRecord]] = {}
    ignored = 0
    for frame_index, items in predictions.items():
        normal = labels.get(frame_index, [])
        matched_labels: set[int] = set()
        matched_predictions: set[int] = set()
        prediction_order = sorted(
            range(len(items)), key=lambda index: items[index].score, reverse=True
        )
        for prediction_index in prediction_order:
            prediction = items[prediction_index]
            best: tuple[float, int] | None = None
            for label_index, label in enumerate(normal):
                if label_index in matched_labels or label.class_name != prediction.class_name:
                    continue
                overlap = box_iou(label.box, prediction.box)
                if overlap >= iou_threshold and (best is None or overlap > best[0]):
                    best = (overlap, label_index)
            if best is not None:
                matched_labels.add(best[1])
                matched_predictions.add(prediction_index)

        kept: list[ObjectRecord] = []
        for prediction_index, item in enumerate(items):
            overlaps_crowd = any(
                crowd.class_name in {IGNORE_REGION_CLASS, item.class_name}
                and box_iou(crowd.box, item.box) >= iou_threshold
                for crowd in crowds.get(frame_index, [])
            )
            if prediction_index not in matched_predictions and overlaps_crowd:
                ignored += 1
            else:
                kept.append(item)
        result[frame_index] = kept
    return result, ignored


def combine_frames(
    values: dict[str, dict[int, list[ObjectRecord]]]
) -> dict[int, list[ObjectRecord]]:
    combined: dict[int, list[ObjectRecord]] = {}
    offset = 0
    for video_name in sorted(values):
        frames = values[video_name]
        for frame_index, items in frames.items():
            combined[offset + frame_index] = [
                ObjectRecord(
                    offset + frame_index,
                    f"{video_name}:{item.object_id}",
                    item.class_name,
                    item.box,
                    item.score,
                )
                for item in items
            ]
        offset += max(frames, default=-1) + 2
    return combined


def evaluate_predictions(
    predictions: dict[str, dict[int, list[ObjectRecord]]],
    labels: dict[str, dict[int, list[ObjectRecord]]],
    crowds: dict[str, dict[int, list[ObjectRecord]]],
    thresholds: list[float],
    minimum_score: float,
    iou_threshold: float,
    include_per_source: bool = True,
) -> dict[str, Any]:
    result: dict[str, Any] = {"ap_floor": minimum_score, "operating_points": {}}
    for threshold in [minimum_score, *thresholds]:
        threshold_key = f"{threshold:.3f}"
        threshold_predictions: dict[str, dict[int, list[ObjectRecord]]] = {}
        ignored_crowd = 0
        per_video: dict[str, Any] = {}
        for video_name in sorted(labels):
            selected = {
                frame_index: [item for item in items if item.score >= threshold]
                for frame_index, items in predictions[video_name].items()
            }
            selected, ignored = suppress_crowd_predictions(
                selected, labels[video_name], crowds[video_name], iou_threshold
            )
            ignored_crowd += ignored
            threshold_predictions[video_name] = selected
            overall, _ = detection_metrics(labels[video_name], selected, iou_threshold)
            per_video[video_name] = overall
        pooled, per_class = detection_metrics(
            combine_frames(labels), combine_frames(threshold_predictions), iou_threshold
        )
        point = {
            "threshold": threshold,
            "pooled": pooled,
            "per_class": per_class,
            "ignored_crowd_predictions": ignored_crowd,
        }
        if include_per_source:
            point["per_video"] = per_video
        result["operating_points"][threshold_key] = point
    return result


def model_deltas(models: dict[str, Any], baseline: str) -> dict[str, Any]:
    result: dict[str, Any] = {}
    baseline_points = models[baseline]["evaluation"]["operating_points"]
    for model_name, data in models.items():
        if model_name == baseline:
            continue
        deltas: dict[str, Any] = {}
        for threshold, point in data["evaluation"]["operating_points"].items():
            reference = baseline_points[threshold]["pooled"]
            current = point["pooled"]
            deltas[threshold] = {
                key: round(float(current[key]) - float(reference[key]), 6)
                for key in ("map50", "precision", "recall", "f1")
            }
        latency = data["model"]["latency"]
        reference_latency = models[baseline]["model"]["latency"]
        result[model_name] = {
            "operating_point_deltas": deltas,
            "mean_latency_ratio": round(
                latency["mean_ms"] / reference_latency["mean_ms"], 6
            ),
            "p95_latency_ratio": round(
                latency["p95_ms"] / reference_latency["p95_ms"], 6
            ),
        }
    return result


def best_class_operating_points(evaluation: dict[str, Any]) -> list[dict[str, Any]]:
    """Select the measured score threshold with maximum F1 for each present class."""
    points = evaluation["operating_points"]
    floor_key = f"{float(evaluation['ap_floor']):.3f}"
    floor_classes = points[floor_key]["per_class"]
    candidates = [point for key, point in points.items() if key != floor_key]
    if not candidates:
        candidates = [points[floor_key]]

    result: list[dict[str, Any]] = []
    for class_name, floor_metrics in sorted(floor_classes.items()):
        if int(floor_metrics["ground_truth"]) <= 0:
            continue
        best = max(
            candidates,
            key=lambda point: (
                float(point["per_class"][class_name]["f1"]),
                float(point["per_class"][class_name]["precision"]),
                -float(point["threshold"]),
            ),
        )
        metrics = best["per_class"][class_name]
        result.append(
            {
                "class_name": class_name,
                "ground_truth": int(floor_metrics["ground_truth"]),
                "ap50": float(floor_metrics["ap50"]),
                "threshold": float(best["threshold"]),
                "precision": float(metrics["precision"]),
                "recall": float(metrics["recall"]),
                "f1": float(metrics["f1"]),
            }
        )
    return result


def markdown_report(report: dict[str, Any]) -> str:
    lines = [
        "# BDD100K PTQ runtime comparison",
        "",
        f"Generated: `{report['generated_at']}`",
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
                f"{metrics['map50']:.6f} | "
                f"{metrics['precision']:.6f} | {metrics['recall']:.6f} | "
                f"{metrics['f1']:.6f} | {latency['mean_ms']:.3f} | "
                f"{latency['p95_ms']:.3f} |"
            )
    lines.extend(
        [
            "",
            "Notes: inference latency excludes video decoding, preprocessing, decode and NMS. "
            "The minimum-score row is the AP candidate floor; higher rows are operating points. "
            "Unmatched predictions overlapping same-class crowd or any-class BDD distractor "
            "regions are ignored for every model.",
            "",
            "## Per-class measured best F1",
            "",
            "AP50 uses the minimum-score candidate floor. Best score is selected only from the "
            "reported operating-point grid; F1 ties prefer higher precision, then the lower score.",
            "",
            "| Model | Runtime | Class | GT | AP50 | Best score | Precision | Recall | F1 |",
            "|---|---|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for model_name, data in report["models"].items():
        for item in best_class_operating_points(data["evaluation"]):
            lines.append(
                f"| {model_name} | {data['model']['runtime']} | {item['class_name']} | "
                f"{item['ground_truth']} | {item['ap50']:.6f} | "
                f"{item['threshold']:.3f} | {item['precision']:.6f} | "
                f"{item['recall']:.6f} | {item['f1']:.6f} |"
            )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    if args.threads <= 0:
        raise QuantizationEvaluationError("--threads must be positive")
    if not 0.0 < args.iou_threshold <= 1.0 or not 0.0 < args.nms_iou_threshold <= 1.0:
        raise QuantizationEvaluationError("IoU thresholds must be in (0, 1]")
    models = parse_named_paths(args.model, "--model")
    runtimes = parse_named_runtimes(args.runtime, {name for name, _ in models})
    videos_list = parse_named_paths(args.video, "--video")
    videos = dict(videos_list)
    thresholds = parse_thresholds(args.score_thresholds, args.minimum_score)
    class_names = load_classes(args.classes.resolve())
    labels, crowds, strides, manifest = load_ground_truth(args.manifest.resolve())
    if set(videos) != set(labels):
        raise QuantizationEvaluationError(
            f"Video names must exactly match manifest: got={sorted(videos)}, "
            f"expected={sorted(labels)}"
        )
    output_dir = args.output_dir.resolve()
    if output_dir.exists():
        raise QuantizationEvaluationError(f"Output directory already exists: {output_dir}")

    report: dict[str, Any] = {
        "schema_version": 3,
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
            "manifest_path": str(args.manifest.resolve()),
            "manifest_sha256": sha256_file(args.manifest.resolve()),
            "manifest_schema_version": manifest.get("schema_version"),
            "classes_path": str(args.classes.resolve()),
            "classes_sha256": sha256_file(args.classes.resolve()),
            "videos": {
                name: {"path": str(path), "sha256": sha256_file(path)}
                for name, path in videos_list
            },
            "evaluated_frames": sum(len(items) for items in labels.values()),
            "evaluated_ground_truth_objects": sum(
                len(objects) for frames in labels.values() for objects in frames.values()
            ),
            "crowd_ground_truth_objects": sum(
                sum(item.class_name != IGNORE_REGION_CLASS for item in objects)
                for frames in crowds.values()
                for objects in frames.values()
            ),
            "distractor_ground_truth_objects": sum(
                sum(item.class_name == IGNORE_REGION_CLASS for item in objects)
                for frames in crowds.values()
                for objects in frames.values()
            ),
            "ignored_ground_truth_regions": sum(
                len(objects) for frames in crowds.values() for objects in frames.values()
            ),
        },
        "configuration": {
            "iou_threshold": args.iou_threshold,
            "nms_iou_threshold": args.nms_iou_threshold,
            "minimum_score": args.minimum_score,
            "score_thresholds": thresholds,
            "ignored_region_matching": (
                "normal GT first; unmatched predictions use IoU threshold; "
                "crowd is class-specific and BDD distractors are any-class"
            ),
            "preprocessing": "C++-aligned BGR letterbox 114, RGB NCHW float32 / 255",
        },
        "models": {},
    }
    for model_name, model_path in models:
        runtime = runtimes[model_name]
        print(
            f"evaluating model={model_name} runtime={runtime} path={model_path}", flush=True
        )
        predictions, model_metadata = infer_model(
            model_name,
            runtime,
            model_path,
            videos,
            labels,
            strides,
            class_names,
            args,
        )
        report["models"][model_name] = {
            "model": model_metadata,
            "evaluation": evaluate_predictions(
                predictions,
                labels,
                crowds,
                thresholds,
                args.minimum_score,
                args.iou_threshold,
            ),
        }
    baseline = models[0][0]
    report["comparison"] = {
        "baseline": baseline,
        "deltas": model_deltas(report["models"], baseline),
    }

    output_dir.mkdir(parents=True)
    json_path = output_dir / "quantization_evaluation.json"
    markdown_path = output_dir / "quantization_evaluation.md"
    json_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    markdown_path.write_text(markdown_report(report), encoding="utf-8")
    manifest_output = {
        "schema_version": 1,
        "files": [
            {"path": json_path.name, "sha256": sha256_file(json_path)},
            {"path": markdown_path.name, "sha256": sha256_file(markdown_path)},
        ],
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest_output, indent=2) + "\n", encoding="utf-8"
    )
    print(f"report={json_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
