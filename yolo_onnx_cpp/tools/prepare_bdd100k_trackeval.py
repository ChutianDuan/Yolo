#!/usr/bin/env python3
"""Prepare BDD100K ground truth and C++ tracking output for TrackEval."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

import evaluate_bdd100k_tracking as evaluation


BDD_EVALUATED_CATEGORIES = (
    "pedestrian",
    "rider",
    "car",
    "bus",
    "truck",
    "train",
    "motorcycle",
    "bicycle",
)
BDD_DISTRACTOR_CATEGORIES = ("other person", "trailer", "other vehicle")
BDD_INPUT_CATEGORIES = frozenset(
    BDD_EVALUATED_CATEGORIES + BDD_DISTRACTOR_CATEGORIES
)
PREDICTION_CATEGORIES = {
    "person": "pedestrian",
    "rider": "rider",
    "car": "car",
    "bus": "bus",
    "truck": "truck",
    "train": "train",
    "motor": "motorcycle",
    "bike": "bicycle",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument(
        "--evaluation-report",
        type=Path,
        action="append",
        required=True,
        help="Schema v1/v2 report emitted by evaluate_bdd100k_tracking.py; repeat per video.",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_reports(paths: Iterable[Path]) -> list[dict[str, Any]]:
    reports: list[dict[str, Any]] = []
    videos: set[str] = set()
    expected_runs: list[str] | None = None
    for raw_path in paths:
        path = raw_path.resolve()
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise evaluation.EvaluationError(f"Invalid evaluation report: {path}") from exc
        if not isinstance(report, dict) or int(report.get("schema_version", 0)) not in (1, 2):
            raise evaluation.EvaluationError(f"Unsupported evaluation report schema: {path}")
        video_name = str(report.get("video_name", "")).strip()
        if not video_name or video_name in videos:
            raise evaluation.EvaluationError(f"Missing or duplicate report video: {video_name!r}")
        videos.add(video_name)
        runs = report.get("runs")
        if not isinstance(runs, list) or not runs:
            raise evaluation.EvaluationError(f"Evaluation report has no runs: {path}")
        run_names = [str(run.get("name", "")).strip() for run in runs]
        if any(not name for name in run_names) or len(set(run_names)) != len(run_names):
            raise evaluation.EvaluationError(f"Invalid run names in evaluation report: {path}")
        if expected_runs is None:
            expected_runs = run_names
        elif run_names != expected_runs:
            raise evaluation.EvaluationError(
                f"Run order differs for video {video_name}: {run_names} != {expected_runs}"
            )
        report["_report_path"] = str(path)
        report["_report_sha256"] = sha256_file(path)
        reports.append(report)
    return reports


def label_object(row: dict[str, str], video_name: str) -> dict[str, Any] | None:
    category = row["category"].strip().lower()
    if category not in BDD_INPUT_CATEGORIES:
        return None
    try:
        object_id = int(row["id"])
        box = {
            "x1": float(row["box2d.x1"]),
            "y1": float(row["box2d.y1"]),
            "x2": float(row["box2d.x2"]),
            "y2": float(row["box2d.y2"]),
        }
    except (TypeError, ValueError) as exc:
        raise evaluation.EvaluationError(f"Invalid BDD label row for {video_name}: {row}") from exc
    if box["x2"] <= box["x1"] or box["y2"] <= box["y1"]:
        return None
    return {
        "id": object_id,
        "category": category,
        "attributes": {
            "Crowd": evaluation.parse_bool(row["attributes.crowd"]),
            "Occluded": evaluation.parse_bool(row.get("attributes.occluded", "false")),
            "Truncated": evaluation.parse_bool(row.get("attributes.truncated", "false")),
        },
        "box2d": box,
    }


def load_official_labels(
    path: Path,
    video_names: Iterable[str],
) -> tuple[dict[str, dict[int, dict[str, Any]]], dict[str, Any]]:
    resolved = path.resolve()
    if not resolved.is_file():
        raise evaluation.EvaluationError(f"Label CSV does not exist: {resolved}")
    requested = set(video_names)
    frames: dict[str, dict[int, dict[str, Any]]] = {
        video: {} for video in requested
    }
    selected_rows = Counter()
    ignored_categories: dict[str, Counter[str]] = {
        video: Counter() for video in requested
    }
    selected_digests = {video: hashlib.sha256() for video in requested}
    with resolved.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required = {
            "name",
            "videoName",
            "frameIndex",
            "id",
            "category",
            "attributes.crowd",
            "box2d.x1",
            "box2d.x2",
            "box2d.y1",
            "box2d.y2",
        }
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise evaluation.EvaluationError(f"Label CSV is missing columns: {sorted(missing)}")
        for row in reader:
            video_name = row["videoName"]
            if video_name not in requested:
                continue
            selected_rows[video_name] += 1
            selected_digests[video_name].update(
                json.dumps(row, sort_keys=True, separators=(",", ":")).encode("utf-8")
            )
            try:
                frame_index = int(row["frameIndex"])
            except (TypeError, ValueError) as exc:
                raise evaluation.EvaluationError(
                    f"Invalid label frame for {video_name}: {row}"
                ) from exc
            frame = frames[video_name].setdefault(
                frame_index,
                {
                    "name": row["name"],
                    "videoName": video_name,
                    "index": frame_index,
                    "labels": [],
                },
            )
            if frame["name"] != row["name"]:
                raise evaluation.EvaluationError(
                    f"Conflicting frame names for {video_name} frame {frame_index}"
                )
            item = label_object(row, video_name)
            if item is None:
                ignored_categories[video_name][row["category"]] += 1
            else:
                frame["labels"].append(item)

    for video_name in requested:
        if not selected_rows[video_name] or not frames[video_name]:
            raise evaluation.EvaluationError(f"No labels found for video {video_name!r}")
    metadata = {
        "path": str(resolved),
        "file_size_bytes": resolved.stat().st_size,
        "videos": {
            video: {
                "selected_rows": selected_rows[video],
                "selected_rows_sha256": selected_digests[video].hexdigest(),
                "annotated_frame_count": len(frames[video]),
                "ignored_categories": dict(sorted(ignored_categories[video].items())),
            }
            for video in sorted(requested)
        },
    }
    return frames, metadata


def prediction_object(
    item: evaluation.ObjectRecord,
    identity_map: dict[str, int],
    next_identity: list[int],
) -> dict[str, Any]:
    category = PREDICTION_CATEGORIES.get(item.class_name)
    if category is None:
        raise evaluation.EvaluationError(
            f"No official BDD100K category for prediction class {item.class_name!r}"
        )
    if item.object_id not in identity_map:
        identity_map[item.object_id] = next_identity[0]
        next_identity[0] += 1
    return {
        "id": identity_map[item.object_id],
        "category": category,
        "box2d": {
            "x1": item.box[0],
            "y1": item.box[1],
            "x2": item.box[2],
            "y2": item.box[3],
        },
    }


def prepare_video(
    report: dict[str, Any],
    label_frames: dict[int, dict[str, Any]],
    tracker_identity_maps: dict[str, dict[tuple[str, str], int]],
    tracker_next_identities: dict[str, list[int]],
) -> tuple[list[dict[str, Any]], dict[str, list[dict[str, Any]]], dict[str, Any]]:
    video_name = report["video_name"]
    frame_stride = int(report["frame_stride"])
    max_missing_tail = int(report.get("max_missing_tail_label_frames", 1))
    label_indices = sorted(label_frames)
    alignment_labels = {frame_index: [] for frame_index in label_indices}
    aligned_by_run: dict[str, dict[int, list[evaluation.ObjectRecord]]] = {}
    skipped_by_run: dict[str, list[int]] = {}
    for run in report["runs"]:
        run_name = run["name"]
        prediction_path = Path(run["prediction"]["path"])
        payload, metadata = evaluation.load_prediction(prediction_path)
        reported_sha256 = run["prediction"].get("sha256")
        if reported_sha256 and metadata["sha256"] != reported_sha256:
            raise evaluation.EvaluationError(
                f"Prediction changed since evaluation report: {prediction_path}"
            )
        predictions, _ = evaluation.prediction_frames(payload)
        _, aligned, _, skipped = evaluation.align_frames(
            alignment_labels,
            predictions,
            frame_stride,
            max_missing_tail,
        )
        reported_alignment = run["metrics"]["alignment"]
        if skipped != reported_alignment.get("skipped_tail_label_frames", []):
            raise evaluation.EvaluationError(
                f"Alignment differs from report for {video_name}/{run_name}"
            )
        aligned_by_run[run_name] = aligned
        skipped_by_run[run_name] = skipped

    first_skipped = next(iter(skipped_by_run.values()))
    if any(skipped != first_skipped for skipped in skipped_by_run.values()):
        raise evaluation.EvaluationError(
            f"Runs have different evaluated frames for video {video_name}: {skipped_by_run}"
        )
    selected_indices = [index for index in label_indices if index not in set(first_skipped)]
    ground_truth = [label_frames[index] for index in selected_indices]
    trackers: dict[str, list[dict[str, Any]]] = {}
    for run_name, aligned in aligned_by_run.items():
        identity_map = tracker_identity_maps[run_name]
        next_identity = tracker_next_identities[run_name]
        video_identity_map = {
            original: mapped
            for (mapped_video, original), mapped in identity_map.items()
            if mapped_video == video_name
        }
        frames: list[dict[str, Any]] = []
        for frame_index in selected_indices:
            labels = [
                prediction_object(item, video_identity_map, next_identity)
                for item in aligned[frame_index]
            ]
            ids = [item["id"] for item in labels]
            if len(ids) != len(set(ids)):
                raise evaluation.EvaluationError(
                    f"Duplicate tracker ID in {video_name}/{run_name}/frame {frame_index}"
                )
            frames.append(
                {
                    "name": label_frames[frame_index]["name"],
                    "videoName": video_name,
                    "index": frame_index,
                    "labels": labels,
                }
            )
        for original, mapped in video_identity_map.items():
            identity_map[(video_name, original)] = mapped
        trackers[run_name] = frames
    return ground_truth, trackers, {
        "frame_stride": frame_stride,
        "input_label_frames": len(label_indices),
        "evaluated_label_frames": len(selected_indices),
        "skipped_tail_label_frames": first_skipped,
    }


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def build_export(labels_path: Path, report_paths: Iterable[Path], output_dir: Path) -> Path:
    reports = load_reports(report_paths)
    official_labels, label_metadata = load_official_labels(
        labels_path, (report["video_name"] for report in reports)
    )
    run_names = [run["name"] for run in reports[0]["runs"]]
    identity_maps: dict[str, dict[tuple[str, str], int]] = {
        run_name: {} for run_name in run_names
    }
    next_identities = {run_name: [1] for run_name in run_names}
    prepared: list[tuple[dict[str, Any], list[dict[str, Any]], dict[str, list[dict[str, Any]]]]] = []
    video_metadata: dict[str, Any] = {}
    for report in reports:
        video_name = report["video_name"]
        ground_truth, trackers, metadata = prepare_video(
            report,
            official_labels[video_name],
            identity_maps,
            next_identities,
        )
        prepared.append((report, ground_truth, trackers))
        video_metadata[video_name] = metadata

    resolved_output = output_dir.resolve()
    try:
        resolved_output.mkdir(parents=True, exist_ok=False)
    except FileExistsError as exc:
        raise evaluation.EvaluationError(
            f"Output directory already exists: {resolved_output}"
        ) from exc

    exported_files: list[dict[str, Any]] = []
    for report, ground_truth, trackers in prepared:
        video_name = report["video_name"]
        gt_path = resolved_output / "gt" / f"{video_name}.json"
        write_json(gt_path, ground_truth)
        exported_files.append(
            {"path": str(gt_path.relative_to(resolved_output)), "sha256": sha256_file(gt_path)}
        )
        for run_name, frames in trackers.items():
            tracker_path = resolved_output / "trackers" / run_name / "data" / f"{video_name}.json"
            write_json(tracker_path, frames)
            exported_files.append(
                {
                    "path": str(tracker_path.relative_to(resolved_output)),
                    "sha256": sha256_file(tracker_path),
                }
            )

    manifest = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "format": "TrackEval BDD100K JSON",
        "evaluated_categories": list(BDD_EVALUATED_CATEGORIES),
        "distractor_categories": list(BDD_DISTRACTOR_CATEGORIES),
        "labels": label_metadata,
        "reports": [
            {
                "path": report["_report_path"],
                "sha256": report["_report_sha256"],
                "video_name": report["video_name"],
            }
            for report in reports
        ],
        "runs": run_names,
        "videos": video_metadata,
        "files": sorted(exported_files, key=lambda item: item["path"]),
    }
    manifest_path = resolved_output / "manifest.json"
    write_json(manifest_path, manifest)
    return manifest_path


def main() -> int:
    args = parse_args()
    manifest_path = build_export(args.labels, args.evaluation_report, args.output_dir)
    print(manifest_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except evaluation.EvaluationError as exc:
        raise SystemExit(f"error: {exc}") from exc
