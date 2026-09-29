#!/usr/bin/env python3
"""Evaluate C++ /infer_video tracking output against BDD100K MOT labels.

BDD100K tracking annotations are sampled at 5 FPS while the source videos are
typically about 30 FPS.  This tool maps each annotation to every sixth decoded
frame, evaluates only classes covered by the MOT annotations, and writes a
machine-readable JSON report plus a compact Markdown summary.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any, Iterable


CLASS_ALIASES = {
    "pedestrian": "person",
    "person": "person",
    "rider": "rider",
    "car": "car",
    "truck": "truck",
    "bus": "bus",
    "train": "train",
    "motorcycle": "motor",
    "motor": "motor",
    "bicycle": "bike",
    "bike": "bike",
}
EVALUATED_CLASSES = frozenset(CLASS_ALIASES.values())


class EvaluationError(RuntimeError):
    pass


@dataclass(frozen=True)
class ObjectRecord:
    frame_index: int
    object_id: str
    class_name: str
    box: tuple[float, float, float, float]
    score: float = 1.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--prediction",
        action="append",
        required=True,
        metavar="NAME=PATH",
        help="Named C++ /infer_video response; may be repeated.",
    )
    parser.add_argument("--labels", type=Path, required=True, help="BDD100K mot_labels.csv")
    parser.add_argument("--video-name", required=True, help="BDD100K video stem")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--label-fps", type=float, default=5.0)
    parser.add_argument("--frame-stride", type=int, default=6)
    parser.add_argument("--iou-threshold", type=float, default=0.5)
    parser.add_argument(
        "--max-missing-tail-label-frames",
        type=int,
        default=1,
        help=(
            "Maximum number of trailing labels allowed beyond the decoded response; "
            "missing response frames inside the covered range always fail."
        ),
    )
    parser.add_argument(
        "--include-crowd",
        action="store_true",
        help="Include BDD rows marked crowd (ignored by default).",
    )
    return parser.parse_args()


def canonical_class(value: Any) -> str | None:
    return CLASS_ALIASES.get(str(value).strip().lower())


def parse_bool(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes"}


def parse_named_paths(values: Iterable[str]) -> list[tuple[str, Path]]:
    result: list[tuple[str, Path]] = []
    names: set[str] = set()
    for value in values:
        name, separator, raw_path = value.partition("=")
        name = name.strip()
        if not separator or not name or not raw_path.strip():
            raise EvaluationError(f"Invalid --prediction value: {value!r}; expected NAME=PATH")
        if name in names:
            raise EvaluationError(f"Duplicate prediction name: {name}")
        names.add(name)
        result.append((name, Path(raw_path).expanduser().resolve()))
    return result


def load_labels(
    path: Path,
    video_name: str,
    include_crowd: bool = False,
) -> tuple[dict[int, list[ObjectRecord]], dict[str, Any]]:
    if not path.is_file():
        raise EvaluationError(f"Label CSV does not exist: {path}")

    frames: dict[int, list[ObjectRecord]] = defaultdict(list)
    selected_rows = 0
    ignored_crowd = 0
    ignored_unsupported = Counter()
    selected_digest = hashlib.sha256()
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required = {
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
            raise EvaluationError(f"Label CSV is missing columns: {sorted(missing)}")
        for row in reader:
            if row["videoName"] != video_name:
                continue
            selected_rows += 1
            selected_digest.update(
                json.dumps(row, sort_keys=True, separators=(",", ":")).encode("utf-8")
            )
            try:
                frame_index = int(row["frameIndex"])
            except (TypeError, ValueError) as exc:
                raise EvaluationError(f"Invalid label frame for {video_name}: {row}") from exc
            frames.setdefault(frame_index, [])
            if not include_crowd and parse_bool(row["attributes.crowd"]):
                ignored_crowd += 1
                continue
            class_name = canonical_class(row["category"])
            if class_name is None:
                ignored_unsupported[row["category"]] += 1
                continue
            try:
                box = (
                    float(row["box2d.x1"]),
                    float(row["box2d.y1"]),
                    float(row["box2d.x2"]),
                    float(row["box2d.y2"]),
                )
            except (TypeError, ValueError) as exc:
                raise EvaluationError(f"Invalid label row for {video_name}: {row}") from exc
            if box[2] <= box[0] or box[3] <= box[1]:
                continue
            frames[frame_index].append(
                ObjectRecord(frame_index, str(row["id"]), class_name, box)
            )

    if selected_rows == 0:
        raise EvaluationError(f"No labels found for video {video_name!r}")
    if not any(frames.values()):
        raise EvaluationError(f"No supported non-crowd labels found for video {video_name!r}")

    label_indices = sorted(frames)
    metadata = {
        "path": str(path.resolve()),
        "file_size_bytes": path.stat().st_size,
        "selected_rows": selected_rows,
        "selected_rows_sha256": selected_digest.hexdigest(),
        "evaluated_objects": sum(len(items) for items in frames.values()),
        "annotated_frame_count": len(frames),
        "frame_index_min": label_indices[0],
        "frame_index_max": label_indices[-1],
        "ignored_crowd": ignored_crowd,
        "ignored_unsupported": dict(sorted(ignored_unsupported.items())),
    }
    return dict(frames), metadata


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_prediction(path: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    if not path.is_file():
        raise EvaluationError(f"Prediction JSON does not exist: {path}")
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise EvaluationError(f"Invalid prediction JSON: {path}") from exc
    if not isinstance(payload, dict) or not isinstance(payload.get("frames"), list):
        raise EvaluationError(f"Prediction JSON has no frames array: {path}")
    metadata = {
        "path": str(path),
        "file_size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
        "response_frame_count": len(payload["frames"]),
        "source_fps": float(payload.get("source_fps") or 0.0),
        "processed_frame_count": int(payload.get("processed_frame_count") or 0),
        "elapsed_sec": float(payload.get("elapsed_sec") or payload.get("_http_elapsed_sec") or 0.0),
    }
    return payload, metadata


def prediction_frames(
    payload: dict[str, Any],
) -> tuple[list[tuple[float, int, list[ObjectRecord]]], dict[str, Any]]:
    result: list[tuple[float, int, list[ObjectRecord]]] = []
    raw_track_count = 0
    ignored_classes: Counter[str] = Counter()
    for position, frame in enumerate(payload.get("frames") or []):
        if not isinstance(frame, dict):
            continue
        frame_index = int(frame.get("frame_index", position))
        timestamp_ms = float(frame.get("timestamp_ms", math.nan))
        if not math.isfinite(timestamp_ms):
            source_fps = float(payload.get("source_fps") or 0.0)
            if source_fps <= 0.0:
                raise EvaluationError("Prediction frame has no timestamp_ms and source_fps is unavailable")
            timestamp_ms = frame_index * 1000.0 / source_fps
        objects: list[ObjectRecord] = []
        for item_index, item in enumerate(frame.get("tracks") or []):
            if not isinstance(item, dict):
                continue
            raw_track_count += 1
            class_name = canonical_class(item.get("class_name", ""))
            if class_name is None:
                ignored_classes[str(item.get("class_name", "unknown"))] += 1
                continue
            box_value = item.get("box") or {}
            try:
                box = (
                    float(box_value["x1"]),
                    float(box_value["y1"]),
                    float(box_value["x2"]),
                    float(box_value["y2"]),
                )
                score = float(item.get("score", 0.0))
            except (KeyError, TypeError, ValueError) as exc:
                raise EvaluationError(f"Invalid track at response frame {frame_index}") from exc
            if box[2] <= box[0] or box[3] <= box[1] or not math.isfinite(score):
                continue
            object_id = str(item.get("track_id", f"untracked-{frame_index}-{item_index}"))
            objects.append(ObjectRecord(frame_index, object_id, class_name, box, score))
        result.append((timestamp_ms, frame_index, objects))
    if not result:
        raise EvaluationError("Prediction response contains no usable frames")
    result.sort(key=lambda item: (item[0], item[1]))
    evaluated_track_count = sum(len(item[2]) for item in result)
    return result, {
        "raw_track_count": raw_track_count,
        "evaluated_track_count": evaluated_track_count,
        "ignored_unannotated_class_count": raw_track_count - evaluated_track_count,
        "ignored_unannotated_classes": dict(sorted(ignored_classes.items())),
    }


def align_frames(
    labels: dict[int, list[ObjectRecord]],
    predictions: list[tuple[float, int, list[ObjectRecord]]],
    frame_stride: int,
    max_missing_tail_frames: int,
) -> tuple[
    dict[int, list[ObjectRecord]],
    dict[int, list[ObjectRecord]],
    list[dict[str, Any]],
    list[int],
]:
    if frame_stride <= 0:
        raise EvaluationError("--frame-stride must be positive")
    if max_missing_tail_frames < 0:
        raise EvaluationError("--max-missing-tail-label-frames must be non-negative")
    by_frame_index = {item[1]: item for item in predictions}
    if len(by_frame_index) != len(predictions):
        raise EvaluationError("Prediction response contains duplicate frame_index values")
    max_response_index = max(by_frame_index)
    evaluated_labels: dict[int, list[ObjectRecord]] = {}
    aligned_predictions: dict[int, list[ObjectRecord]] = {}
    alignment: list[dict[str, Any]] = []
    skipped_tail_frames: list[int] = []
    for label_index in sorted(labels):
        expected_response_index = label_index * frame_stride
        response = by_frame_index.get(expected_response_index)
        if response is None:
            if expected_response_index <= max_response_index:
                raise EvaluationError(
                    f"Response is missing frame {expected_response_index} required by label "
                    f"frame {label_index}"
                )
            skipped_tail_frames.append(label_index)
            continue
        response_timestamp, response_index, objects = response
        evaluated_labels[label_index] = labels[label_index]
        aligned_predictions[label_index] = objects
        alignment.append(
            {
                "label_frame_index": label_index,
                "response_frame_index": response_index,
                "response_timestamp_ms": round(response_timestamp, 6),
            }
        )
    if len(skipped_tail_frames) > max_missing_tail_frames:
        raise EvaluationError(
            f"Response is missing {len(skipped_tail_frames)} trailing label frames; "
            f"limit is {max_missing_tail_frames}"
        )
    if not alignment:
        raise EvaluationError("No label frame could be aligned to the response")
    return evaluated_labels, aligned_predictions, alignment, skipped_tail_frames


def box_iou(a: tuple[float, float, float, float], b: tuple[float, float, float, float]) -> float:
    x1 = max(a[0], b[0])
    y1 = max(a[1], b[1])
    x2 = min(a[2], b[2])
    y2 = min(a[3], b[3])
    intersection = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    area_a = max(0.0, a[2] - a[0]) * max(0.0, a[3] - a[1])
    area_b = max(0.0, b[2] - b[0]) * max(0.0, b[3] - b[1])
    union = area_a + area_b - intersection
    return intersection / union if union > 0.0 else 0.0


def match_frame(
    labels: list[ObjectRecord],
    predictions: list[ObjectRecord],
    iou_threshold: float,
) -> list[tuple[int, int, float]]:
    candidates: list[tuple[float, int, int]] = []
    for label_index, label in enumerate(labels):
        for prediction_index, prediction in enumerate(predictions):
            if label.class_name != prediction.class_name:
                continue
            iou = box_iou(label.box, prediction.box)
            if iou >= iou_threshold:
                candidates.append((iou, label_index, prediction_index))
    candidates.sort(key=lambda item: (-item[0], item[1], item[2]))
    used_labels: set[int] = set()
    used_predictions: set[int] = set()
    matches: list[tuple[int, int, float]] = []
    for iou, label_index, prediction_index in candidates:
        if label_index in used_labels or prediction_index in used_predictions:
            continue
        used_labels.add(label_index)
        used_predictions.add(prediction_index)
        matches.append((label_index, prediction_index, iou))
    return matches


def ap101(tp_flags: list[int], fp_flags: list[int], ground_truth_count: int) -> float:
    if ground_truth_count <= 0:
        return 0.0
    cumulative_tp = 0
    cumulative_fp = 0
    precisions: list[float] = []
    recalls: list[float] = []
    for tp, fp in zip(tp_flags, fp_flags):
        cumulative_tp += tp
        cumulative_fp += fp
        precisions.append(cumulative_tp / max(1, cumulative_tp + cumulative_fp))
        recalls.append(cumulative_tp / ground_truth_count)
    return sum(
        max((precision for precision, recall in zip(precisions, recalls) if recall >= level), default=0.0)
        for level in (index / 100.0 for index in range(101))
    ) / 101.0


def percentile(values: list[float], ratio: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * ratio
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def detection_metrics(
    labels_by_frame: dict[int, list[ObjectRecord]],
    predictions_by_frame: dict[int, list[ObjectRecord]],
    iou_threshold: float,
) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    classes = sorted(
        {item.class_name for items in labels_by_frame.values() for item in items}
        | {item.class_name for items in predictions_by_frame.values() for item in items}
    )
    per_class: dict[str, dict[str, Any]] = {}
    for class_name in classes:
        class_labels = {
            frame_index: [item for item in items if item.class_name == class_name]
            for frame_index, items in labels_by_frame.items()
        }
        predictions = sorted(
            (
                (item.score, frame_index, prediction_index, item)
                for frame_index, items in predictions_by_frame.items()
                for prediction_index, item in enumerate(items)
                if item.class_name == class_name
            ),
            key=lambda item: (-item[0], item[1], item[2]),
        )
        matched_labels: dict[int, set[int]] = defaultdict(set)
        tp_flags: list[int] = []
        fp_flags: list[int] = []
        matched_ious: list[float] = []
        for _, frame_index, _, prediction in predictions:
            best: tuple[float, int] | None = None
            for label_index, label in enumerate(class_labels.get(frame_index, [])):
                if label_index in matched_labels[frame_index]:
                    continue
                iou = box_iou(label.box, prediction.box)
                if iou >= iou_threshold and (best is None or iou > best[0]):
                    best = (iou, label_index)
            if best is None:
                tp_flags.append(0)
                fp_flags.append(1)
            else:
                matched_labels[frame_index].add(best[1])
                matched_ious.append(best[0])
                tp_flags.append(1)
                fp_flags.append(0)
        ground_truth_count = sum(len(items) for items in class_labels.values())
        true_positives = sum(tp_flags)
        prediction_count = len(predictions)
        precision = true_positives / prediction_count if prediction_count else 0.0
        recall = true_positives / ground_truth_count if ground_truth_count else 0.0
        per_class[class_name] = {
            "ground_truth": ground_truth_count,
            "predictions": prediction_count,
            "true_positives": true_positives,
            "false_positives": prediction_count - true_positives,
            "false_negatives": ground_truth_count - true_positives,
            "precision": round(precision, 6),
            "recall": round(recall, 6),
            "f1": round(2.0 * precision * recall / (precision + recall), 6)
            if precision + recall
            else 0.0,
            "ap50": round(ap101(tp_flags, fp_flags, ground_truth_count), 6)
            if ground_truth_count
            else None,
            "mean_matched_iou": round(mean(matched_ious), 6) if matched_ious else 0.0,
            "minimum_prediction_score": round(min((item[0] for item in predictions), default=0.0), 6),
        }

    totals = Counter()
    for metrics in per_class.values():
        for key in ("ground_truth", "predictions", "true_positives", "false_positives", "false_negatives"):
            totals[key] += int(metrics[key])
    precision = totals["true_positives"] / totals["predictions"] if totals["predictions"] else 0.0
    recall = totals["true_positives"] / totals["ground_truth"] if totals["ground_truth"] else 0.0
    overall = {
        **dict(totals),
        "precision": round(precision, 6),
        "recall": round(recall, 6),
        "f1": round(2.0 * precision * recall / (precision + recall), 6)
        if precision + recall
        else 0.0,
        "map50": round(
            mean(item["ap50"] for item in per_class.values() if item["ap50"] is not None), 6
        )
        if any(item["ap50"] is not None for item in per_class.values())
        else 0.0,
    }
    return overall, per_class


def maximum_assignment_weight(matrix: list[list[int]]) -> int:
    """Return maximum rectangular assignment weight using Hungarian O(n^3)."""
    if not matrix or not matrix[0]:
        return 0
    rows = len(matrix)
    columns = len(matrix[0])
    if rows > columns:
        matrix = [[matrix[row][column] for row in range(rows)] for column in range(columns)]
        rows, columns = columns, rows
    maximum = max(max(row) for row in matrix)
    u = [0] * (rows + 1)
    v = [0] * (columns + 1)
    assignment = [0] * (columns + 1)
    previous = [0] * (columns + 1)
    for row in range(1, rows + 1):
        assignment[0] = row
        column0 = 0
        minimum = [10**18] * (columns + 1)
        used = [False] * (columns + 1)
        while True:
            used[column0] = True
            current_row = assignment[column0]
            delta = 10**18
            column1 = 0
            for column in range(1, columns + 1):
                if used[column]:
                    continue
                cost = maximum - matrix[current_row - 1][column - 1]
                reduced = cost - u[current_row] - v[column]
                if reduced < minimum[column]:
                    minimum[column] = reduced
                    previous[column] = column0
                if minimum[column] < delta:
                    delta = minimum[column]
                    column1 = column
            for column in range(columns + 1):
                if used[column]:
                    u[assignment[column]] += delta
                    v[column] -= delta
                else:
                    minimum[column] -= delta
            column0 = column1
            if assignment[column0] == 0:
                break
        while True:
            column1 = previous[column0]
            assignment[column0] = assignment[column1]
            column0 = column1
            if column0 == 0:
                break
    return sum(
        matrix[assignment[column] - 1][column - 1]
        for column in range(1, columns + 1)
        if assignment[column] != 0
    )


def tracking_metrics(
    labels_by_frame: dict[int, list[ObjectRecord]],
    predictions_by_frame: dict[int, list[ObjectRecord]],
    iou_threshold: float,
    label_fps: float,
) -> dict[str, Any]:
    if label_fps <= 0.0:
        raise EvaluationError("--label-fps must be positive")
    ground_truth_total = sum(len(items) for items in labels_by_frame.values())
    prediction_total = sum(len(items) for items in predictions_by_frame.values())
    matched_total = 0
    matched_ious: list[float] = []
    pair_counts: Counter[tuple[str, str, str]] = Counter()
    gt_identity_counts: Counter[tuple[str, str]] = Counter()
    pred_identity_counts: Counter[tuple[str, str]] = Counter()
    last_match: dict[tuple[str, str], str] = {}
    was_matched: dict[tuple[str, str], bool] = {}
    ever_matched: set[tuple[str, str]] = set()
    last_visible_frame: dict[tuple[str, str], int] = {}
    active_miss_run: dict[tuple[str, str], int] = defaultdict(int)
    miss_runs: list[int] = []
    last_localization: dict[tuple[str, str], tuple[int, str, float, float]] = {}
    jitter_pixels: list[float] = []
    normalized_jitter: list[float] = []
    id_switches = 0
    fragments = 0

    for items in labels_by_frame.values():
        for item in items:
            gt_identity_counts[(item.class_name, item.object_id)] += 1
    for items in predictions_by_frame.values():
        for item in items:
            pred_identity_counts[(item.class_name, item.object_id)] += 1

    for frame_index in sorted(labels_by_frame):
        labels = labels_by_frame.get(frame_index, [])
        predictions = predictions_by_frame.get(frame_index, [])
        matches = match_frame(labels, predictions, iou_threshold)
        matched_total += len(matches)
        matched_by_label = {
            label_index: (prediction_index, iou)
            for label_index, prediction_index, iou in matches
        }
        for label_index, label in enumerate(labels):
            identity = (label.class_name, label.object_id)
            previous_visible = last_visible_frame.get(identity)
            consecutive_visibility = previous_visible == frame_index - 1
            if previous_visible is not None and not consecutive_visibility:
                if active_miss_run[identity]:
                    miss_runs.append(active_miss_run[identity])
                    active_miss_run[identity] = 0
                was_matched[identity] = False
                last_localization.pop(identity, None)
            last_visible_frame[identity] = frame_index

            match = matched_by_label.get(label_index)
            if match is None:
                active_miss_run[identity] += 1
                was_matched[identity] = False
                last_localization.pop(identity, None)
                continue

            prediction_index, iou = match
            prediction = predictions[prediction_index]
            if active_miss_run[identity]:
                miss_runs.append(active_miss_run[identity])
                active_miss_run[identity] = 0
            pair_counts[(label.class_name, label.object_id, prediction.object_id)] += 1
            matched_ious.append(iou)
            previous_id = last_match.get(identity)
            if previous_id is not None and previous_id != prediction.object_id:
                id_switches += 1
            if identity in ever_matched and consecutive_visibility and not was_matched.get(identity, False):
                fragments += 1

            gt_center_x = (label.box[0] + label.box[2]) * 0.5
            gt_center_y = (label.box[1] + label.box[3]) * 0.5
            pred_center_x = (prediction.box[0] + prediction.box[2]) * 0.5
            pred_center_y = (prediction.box[1] + prediction.box[3]) * 0.5
            error_x = pred_center_x - gt_center_x
            error_y = pred_center_y - gt_center_y
            previous_localization = last_localization.get(identity)
            if (
                previous_localization is not None
                and previous_localization[0] == frame_index - 1
                and previous_localization[1] == prediction.object_id
            ):
                jitter = math.hypot(
                    error_x - previous_localization[2],
                    error_y - previous_localization[3],
                )
                diagonal = math.hypot(
                    label.box[2] - label.box[0],
                    label.box[3] - label.box[1],
                )
                jitter_pixels.append(jitter)
                normalized_jitter.append(jitter / diagonal if diagonal > 0.0 else 0.0)
            last_localization[identity] = (
                frame_index, prediction.object_id, error_x, error_y
            )
            last_match[identity] = prediction.object_id
            was_matched[identity] = True
            ever_matched.add(identity)

    miss_runs.extend(length for length in active_miss_run.values() if length > 0)

    class_names = sorted({identity[0] for identity in gt_identity_counts})
    identity_true_positives = 0
    for class_name in class_names:
        gt_ids = sorted(identity[1] for identity in gt_identity_counts if identity[0] == class_name)
        pred_ids = sorted(identity[1] for identity in pred_identity_counts if identity[0] == class_name)
        matrix = [
            [pair_counts[(class_name, gt_id, pred_id)] for pred_id in pred_ids]
            for gt_id in gt_ids
        ]
        identity_true_positives += maximum_assignment_weight(matrix)

    false_positives = prediction_total - matched_total
    false_negatives = ground_truth_total - matched_total
    identity_false_positives = prediction_total - identity_true_positives
    identity_false_negatives = ground_truth_total - identity_true_positives
    id_denominator = 2 * identity_true_positives + identity_false_positives + identity_false_negatives
    mota = 1.0 - (false_negatives + false_positives + id_switches) / ground_truth_total
    miss_duration = {
        "event_count": len(miss_runs),
        "missed_ground_truth_observations": sum(miss_runs),
        "mean_frames": round(mean(miss_runs), 6) if miss_runs else 0.0,
        "p50_frames": round(percentile([float(value) for value in miss_runs], 0.50), 6),
        "p95_frames": round(percentile([float(value) for value in miss_runs], 0.95), 6),
        "max_frames": max(miss_runs, default=0),
        "mean_ms": round(mean(miss_runs) * 1000.0 / label_fps, 6) if miss_runs else 0.0,
        "p95_ms": round(percentile([float(value) for value in miss_runs], 0.95) * 1000.0 / label_fps, 6),
        "max_ms": round(max(miss_runs, default=0) * 1000.0 / label_fps, 6),
    }
    localization_jitter = {
        "sample_count": len(jitter_pixels),
        "mean_error_delta_px": round(mean(jitter_pixels), 6) if jitter_pixels else 0.0,
        "p50_error_delta_px": round(percentile(jitter_pixels, 0.50), 6),
        "p95_error_delta_px": round(percentile(jitter_pixels, 0.95), 6),
        "max_error_delta_px": round(max(jitter_pixels, default=0.0), 6),
        "sum_error_delta_px": round(sum(jitter_pixels), 6),
        "mean_normalized_error_delta": round(mean(normalized_jitter), 6) if normalized_jitter else 0.0,
        "p95_normalized_error_delta": round(percentile(normalized_jitter, 0.95), 6),
        "max_normalized_error_delta": round(max(normalized_jitter, default=0.0), 6),
        "sum_normalized_error_delta": round(sum(normalized_jitter), 6),
    }
    return {
        "ground_truth": ground_truth_total,
        "predictions": prediction_total,
        "matches": matched_total,
        "false_positives": false_positives,
        "false_negatives": false_negatives,
        "id_switches": id_switches,
        "fragments": fragments,
        "mota": round(mota, 6),
        "idf1": round(2 * identity_true_positives / id_denominator, 6) if id_denominator else 0.0,
        "idtp": identity_true_positives,
        "idfp": identity_false_positives,
        "idfn": identity_false_negatives,
        "mean_matched_iou": round(mean(matched_ious), 6) if matched_ious else 0.0,
        "miss_duration": miss_duration,
        "localization_jitter": localization_jitter,
    }


def evaluate_payload(
    payload: dict[str, Any],
    labels: dict[int, list[ObjectRecord]],
    frame_stride: int,
    label_fps: float,
    iou_threshold: float,
    max_missing_tail_frames: int,
) -> dict[str, Any]:
    predictions, prediction_filter = prediction_frames(payload)
    evaluated_labels, aligned_predictions, alignment, skipped_tail_frames = align_frames(
        labels, predictions, frame_stride, max_missing_tail_frames
    )
    detection, by_class = detection_metrics(
        evaluated_labels, aligned_predictions, iou_threshold
    )
    tracking = tracking_metrics(evaluated_labels, aligned_predictions, iou_threshold, label_fps)
    return {
        "alignment": {
            "frame_stride": frame_stride,
            "requested_label_frames": len(labels),
            "evaluated_label_frames": len(alignment),
            "unique_response_frames": len({item["response_frame_index"] for item in alignment}),
            "skipped_tail_label_frames": skipped_tail_frames,
            "skipped_tail_ground_truth_objects": sum(
                len(labels[index]) for index in skipped_tail_frames
            ),
            "first": alignment[0] if alignment else None,
            "last": alignment[-1] if alignment else None,
        },
        "prediction_filter": prediction_filter,
        "detection": detection,
        "by_class": by_class,
        "tracking": tracking,
    }


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# BDD100K 人工真值评估",
        "",
        f"- 视频：`{report['video_name']}`",
        f"- 标注：{report['label_fps']:g} FPS 人工 MOT CSV；每个标注帧映射到第 {report['frame_stride']} 个 C++ 原视频帧",
        f"- IoU 阈值：{report['iou_threshold']}",
        f"- 类别：{', '.join(report['evaluated_classes'])}",
        "- crowd：" + ("纳入" if report["include_crowd"] else "忽略"),
        "",
        "## 汇总",
        "",
        "| run | frames | skipped tail | GT | pred | P | R | F1 | mAP50 | MOTA | IDF1 | IDSW | fragments |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for run in report["runs"]:
        detection = run["metrics"]["detection"]
        tracking = run["metrics"]["tracking"]
        alignment = run["metrics"]["alignment"]
        lines.append(
            f"| {run['name']} | {alignment['evaluated_label_frames']} | {len(alignment['skipped_tail_label_frames'])} | "
            f"{detection['ground_truth']} | {detection['predictions']} | {detection['precision']:.4f} | "
            f"{detection['recall']:.4f} | {detection['f1']:.4f} | {detection['map50']:.4f} | "
            f"{tracking['mota']:.4f} | {tracking['idf1']:.4f} | {tracking['id_switches']} | "
            f"{tracking['fragments']} |"
        )
    lines.extend(
        [
            "",
            "## 漏检持续时间与轨迹抖动",
            "",
            "| run | miss events | missed GT | mean miss ms | p95 miss ms | max miss ms | jitter samples | mean normalized jitter | p95 normalized jitter |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for run in report["runs"]:
        tracking = run["metrics"]["tracking"]
        miss = tracking["miss_duration"]
        jitter = tracking["localization_jitter"]
        lines.append(
            f"| {run['name']} | {miss['event_count']} | "
            f"{miss['missed_ground_truth_observations']} | {miss['mean_ms']:.1f} | "
            f"{miss['p95_ms']:.1f} | {miss['max_ms']:.1f} | "
            f"{jitter['sample_count']} | {jitter['mean_normalized_error_delta']:.4f} | "
            f"{jitter['p95_normalized_error_delta']:.4f} |"
        )
    for run in report["runs"]:
        lines.extend(
            [
                "",
                f"## {run['name']} 分类别检测",
                "",
                "| class | GT | pred | TP | P | R | F1 | AP50 | mean IoU | min score |",
                "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
            ]
        )
        for class_name, metrics in run["metrics"]["by_class"].items():
            ap50 = "n/a" if metrics["ap50"] is None else f"{metrics['ap50']:.4f}"
            lines.append(
                f"| {class_name} | {metrics['ground_truth']} | {metrics['predictions']} | "
                f"{metrics['true_positives']} | {metrics['precision']:.4f} | {metrics['recall']:.4f} | "
                f"{metrics['f1']:.4f} | {ap50} | "
                f"{metrics['mean_matched_iou']:.4f} | {metrics['minimum_prediction_score']:.4f} |"
            )
    lines.extend(
        [
            "",
            "## 口径限制",
            "",
            "- 这里只评估 BDD100K MOT CSV 覆盖的 8 类；未标注的 traffic light/sign 不计入假阳性。",
            "- AP50 使用 101 点插值；MOTA/IDF1 基于按 frame_stride 映射后的标注帧做逐帧 class-aware IoU 匹配。",
            "- ID switch 在同一真值轨迹再次匹配到不同预测 ID 时计数；fragment 只在真值连续可见期间漏配后重新匹配时计数。",
            "- miss duration 只统计真值连续可见期间的连续漏配段；毫秒值按 label_fps 换算。",
            "- normalized jitter 是同一预测 ID 连续匹配时，相邻帧定位中心误差向量变化量除以当前真值框对角线。",
            "- 这不是 TrackEval 输出，暂不报告 HOTA；业务验收仍需真实监控机位标注和官方评估工具复核。",
            "",
        ]
    )
    return "\n".join(lines)


def create_output_directory(path: Path) -> Path:
    resolved = path.resolve()
    try:
        resolved.mkdir(parents=True, exist_ok=False)
    except FileExistsError as exc:
        raise EvaluationError(f"Output directory already exists: {resolved}") from exc
    return resolved


def main() -> int:
    args = parse_args()
    if not 0.0 < args.iou_threshold <= 1.0:
        raise EvaluationError("--iou-threshold must be in (0, 1]")
    if args.label_fps <= 0.0:
        raise EvaluationError("--label-fps must be positive")
    named_paths = parse_named_paths(args.prediction)
    labels, label_metadata = load_labels(args.labels.resolve(), args.video_name, args.include_crowd)
    runs: list[dict[str, Any]] = []
    for name, path in named_paths:
        payload, prediction_metadata = load_prediction(path)
        runs.append(
            {
                "name": name,
                "prediction": prediction_metadata,
                "metrics": evaluate_payload(
                    payload,
                    labels,
                    args.frame_stride,
                    args.label_fps,
                    args.iou_threshold,
                    args.max_missing_tail_label_frames,
                ),
            }
        )
    report = {
        "schema_version": 2,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "video_name": args.video_name,
        "label_fps": args.label_fps,
        "frame_stride": args.frame_stride,
        "iou_threshold": args.iou_threshold,
        "max_missing_tail_label_frames": args.max_missing_tail_label_frames,
        "include_crowd": args.include_crowd,
        "evaluated_classes": sorted(EVALUATED_CLASSES),
        "labels": label_metadata,
        "runs": runs,
    }
    output_directory = create_output_directory(args.output_dir)
    json_path = output_directory / "bdd100k_ground_truth_evaluation.json"
    markdown_path = output_directory / "bdd100k_ground_truth_evaluation.md"
    json_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    markdown_path.write_text(render_markdown(report), encoding="utf-8")
    print(markdown_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except EvaluationError as exc:
        raise SystemExit(f"error: {exc}") from exc
