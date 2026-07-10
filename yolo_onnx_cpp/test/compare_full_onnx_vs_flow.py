#!/usr/bin/env python3
"""Compare full-frame ONNX video tracking against dynamic/fixed ONNX + flow.

The full-frame ONNX run is treated as pseudo-labels. Dynamic and fixed
ONNX + optical-flow runs are compared frame-by-frame with class-aware greedy
IoU matching. Raw responses, a compact pseudo-label JSONL, annotated videos,
and a Markdown report are saved under the output directory.
"""

from __future__ import annotations

import argparse
import colorsys
import http.client
import json
import mimetypes
import shutil
import socket
import subprocess
import sys
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = REPO_ROOT / "build" / "yolo_api"
DEFAULT_CONFIG = REPO_ROOT / "yolo_onnx_cpp" / "config.yaml"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "yolo_onnx_cpp" / "test_outputs" / "video_compare"


class TestError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run /infer_video on the same video with full-frame ONNX, "
            "dynamic ONNX + optical flow, and fixed ONNX + optical flow."
        )
    )
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--video", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--flow-detect-fps", type=float, default=4.0)
    parser.add_argument(
        "--fixed-flow-detect-fps",
        type=float,
        help="Fixed ONNX + flow target detect FPS. Defaults to --flow-detect-fps.",
    )
    parser.add_argument(
        "--no-save-videos",
        action="store_true",
        help="Skip rendering annotated detection videos.",
    )
    parser.add_argument(
        "--use-async",
        action="store_true",
        help="Use production async ONNX scheduling instead of deterministic offline scheduling.",
    )
    parser.add_argument("--startup-timeout", type=float, default=120.0)
    parser.add_argument("--request-timeout", type=float, default=1800.0)
    parser.add_argument(
        "--iou-thresholds",
        default="0.3,0.5,0.7",
        help="Comma-separated IoU thresholds used for class-aware matching.",
    )
    return parser.parse_args()


def resolve_path(path: Path) -> Path:
    if path.is_absolute():
        return path
    return (REPO_ROOT / path).resolve()


def unquote_config_value(value: str) -> str:
    value = value.split("#", 1)[0].strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
        return value[1:-1]
    return value


def model_path_from_config(config: Path, lines: list[str]) -> Path | None:
    for line in lines:
        stripped = line.lstrip()
        if not stripped.startswith("model_path:"):
            continue
        value = unquote_config_value(stripped.split(":", 1)[1])
        if not value:
            return None
        model_path = Path(value)
        return model_path if model_path.is_absolute() else (config.parent / model_path).resolve()
    return None


def port_is_open(host: str, port: int, timeout: float = 0.5) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def wait_for_port(host: str, port: int, timeout: float, process: subprocess.Popen[Any]) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise TestError(f"Server exited before accepting connections: {process.returncode}")
        if port_is_open(host, port):
            return
        time.sleep(0.25)
    raise TestError(f"Timed out waiting for {host}:{port}")


def update_scalar_config(lines: list[str], key: str, value: str) -> list[str]:
    output: list[str] = []
    replaced = False
    for line in lines:
        stripped = line.lstrip()
        if stripped.startswith(key + ":"):
            indent = line[: len(line) - len(stripped)]
            output.append(f"{indent}{key}: {value}")
            replaced = True
        else:
            output.append(line)
    if not replaced:
        output.append(f"{key}: {value}")
    return output


def make_runtime_config(
    config: Path,
    output_dir: Path,
    name: str,
    detect_fps: float,
    stride_mode: str,
    onnx_async: bool,
) -> Path:
    lines = config.read_text(encoding="utf-8").splitlines()
    model_path = model_path_from_config(config, lines)
    if model_path is not None:
        lines = update_scalar_config(lines, "model_path", str(model_path))
    lines = update_scalar_config(lines, "video_detect_fps", f"{detect_fps:g}")
    lines = update_scalar_config(lines, "video_stride_mode", stride_mode)
    lines = update_scalar_config(lines, "video_onnx_async", "true" if onnx_async else "false")
    runtime_config = output_dir / f"{name}_config.yaml"
    runtime_config.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return runtime_config


def start_server(
    binary: Path,
    config: Path,
    output_dir: Path,
    host: str,
    port: int,
    startup_timeout: float,
    name: str,
) -> subprocess.Popen[Any]:
    if port_is_open(host, port):
        raise TestError(f"{host}:{port} is already open")
    log_path = output_dir / f"{name}_server.log"
    log_file = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(
        [str(binary), str(config)],
        cwd=str(REPO_ROOT),
        stdout=log_file,
        stderr=subprocess.STDOUT,
        text=True,
    )
    log_file.close()
    wait_for_port(host, port, startup_timeout, process)
    return process


def stop_server(process: subprocess.Popen[Any] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def post_video(
    host: str,
    port: int,
    video_path: Path,
    timeout: float,
    endpoint: str = "/infer_video",
) -> tuple[int, dict[str, Any], bytes, float]:
    boundary = f"----codex-yolo-video-compare-{time.time_ns()}"
    content_type = mimetypes.guess_type(video_path.name)[0] or "application/octet-stream"
    video_bytes = video_path.read_bytes()
    head = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="video"; filename="{video_path.name}"\r\n'
        f"Content-Type: {content_type}\r\n\r\n"
    ).encode("utf-8")
    tail = f"\r\n--{boundary}--\r\n".encode("utf-8")
    body = head + video_bytes + tail
    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(body)),
    }

    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    start = time.perf_counter()
    try:
        separator = "&" if "?" in endpoint else "?"
        request_path = f"{endpoint}{separator}include_frames=true"
        conn.request("POST", request_path, body=body, headers=headers)
        response = conn.getresponse()
        response_body = response.read()
    finally:
        elapsed = time.perf_counter() - start
        conn.close()

    try:
        payload = json.loads(response_body.decode("utf-8"))
    except json.JSONDecodeError as exc:
        snippet = response_body[:1000].decode("utf-8", errors="replace")
        raise TestError(f"Response is not JSON: {snippet}") from exc

    return response.status, payload, response_body, elapsed


def require_video_success(status: int, payload: dict[str, Any], name: str) -> None:
    if status != 200:
        raise TestError(f"{name} HTTP {status}: {payload}")
    if payload.get("code") != 0 or payload.get("message") != "success":
        raise TestError(f"{name} unexpected response: {payload}")
    if not isinstance(payload.get("frames"), list):
        raise TestError(f"{name} response frames is not a list")


def run_video_case(
    name: str,
    binary: Path,
    runtime_config: Path,
    video_path: Path,
    output_dir: Path,
    host: str,
    port: int,
    startup_timeout: float,
    request_timeout: float,
    endpoint: str = "/infer_video",
) -> dict[str, Any]:
    print(f"[{name}] starting yolo_api with {runtime_config}", flush=True)
    process: subprocess.Popen[Any] | None = None
    try:
        process = start_server(binary, runtime_config, output_dir, host, port, startup_timeout, name)
        print(f"[{name}] posting {video_path}", flush=True)
        status, payload, raw_body, elapsed_sec = post_video(
            host,
            port,
            video_path,
            request_timeout,
            endpoint,
        )
        (output_dir / f"{name}_infer_video_response.raw").write_bytes(raw_body)
        require_video_success(status, payload, name)
        payload["_http_elapsed_sec"] = elapsed_sec
        (output_dir / f"{name}_infer_video_response.json").write_text(
            json.dumps(payload, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        print(
            f"[{name}] done: elapsed={elapsed_sec:.3f}s, "
            f"processed={payload.get('processed_frame_count')}, "
            f"display={payload.get('display_frame_count', payload.get('frame_count'))}",
            flush=True,
        )
        return payload
    finally:
        stop_server(process)


def box_iou(a: dict[str, float], b: dict[str, float]) -> float:
    ax1 = float(a.get("x1", 0.0))
    ay1 = float(a.get("y1", 0.0))
    ax2 = float(a.get("x2", 0.0))
    ay2 = float(a.get("y2", 0.0))
    bx1 = float(b.get("x1", 0.0))
    by1 = float(b.get("y1", 0.0))
    bx2 = float(b.get("x2", 0.0))
    by2 = float(b.get("y2", 0.0))
    inter_x1 = max(ax1, bx1)
    inter_y1 = max(ay1, by1)
    inter_x2 = min(ax2, bx2)
    inter_y2 = min(ay2, by2)
    inter_w = max(0.0, inter_x2 - inter_x1)
    inter_h = max(0.0, inter_y2 - inter_y1)
    inter_area = inter_w * inter_h
    area_a = max(0.0, ax2 - ax1) * max(0.0, ay2 - ay1)
    area_b = max(0.0, bx2 - bx1) * max(0.0, by2 - by1)
    union = area_a + area_b - inter_area
    return inter_area / union if union > 0.0 else 0.0


def frame_tracks_by_index(payload: dict[str, Any]) -> dict[int, list[dict[str, Any]]]:
    result: dict[int, list[dict[str, Any]]] = {}
    for frame in payload.get("frames", []):
        if not isinstance(frame, dict):
            continue
        frame_index = int(frame.get("frame_index", -1))
        tracks = frame.get("tracks") or []
        if isinstance(tracks, list):
            result[frame_index] = [track for track in tracks if isinstance(track, dict)]
    return result


def frame_source_by_index(payload: dict[str, Any]) -> dict[int, str]:
    result: dict[int, str] = {}
    for frame in payload.get("frames", []):
        if isinstance(frame, dict):
            result[int(frame.get("frame_index", -1))] = str(frame.get("tracks_source", "unknown"))
    return result


def greedy_matches(
    labels: list[dict[str, Any]],
    predictions: list[dict[str, Any]],
    iou_threshold: float,
) -> list[tuple[int, int, float]]:
    candidates: list[tuple[float, int, int]] = []
    for label_index, label in enumerate(labels):
        label_box = label.get("box") or {}
        label_class = label.get("class_id")
        for pred_index, pred in enumerate(predictions):
            if pred.get("class_id") != label_class:
                continue
            iou = box_iou(label_box, pred.get("box") or {})
            if iou >= iou_threshold:
                candidates.append((iou, label_index, pred_index))
    candidates.sort(key=lambda item: (-item[0], item[1], item[2]))

    used_labels: set[int] = set()
    used_predictions: set[int] = set()
    matches: list[tuple[int, int, float]] = []
    for iou, label_index, pred_index in candidates:
        if label_index in used_labels or pred_index in used_predictions:
            continue
        used_labels.add(label_index)
        used_predictions.add(pred_index)
        matches.append((label_index, pred_index, iou))
    return matches


def greedy_match(
    labels: list[dict[str, Any]],
    predictions: list[dict[str, Any]],
    iou_threshold: float,
) -> tuple[int, list[float]]:
    matches = greedy_matches(labels, predictions, iou_threshold)
    return len(matches), [iou for _, _, iou in matches]


def class_key(item: dict[str, Any]) -> str:
    class_id = item.get("class_id", "unknown")
    class_name = item.get("class_name")
    return f"{class_id}:{class_name}" if class_name else str(class_id)


def evaluate_at_threshold(
    full_payload: dict[str, Any],
    flow_payload: dict[str, Any],
    iou_threshold: float,
) -> dict[str, Any]:
    label_frames = frame_tracks_by_index(full_payload)
    pred_frames = frame_tracks_by_index(flow_payload)
    source_by_frame = frame_source_by_index(flow_payload)
    frame_indices = sorted(set(label_frames) | set(pred_frames))

    totals = Counter()
    matched_ious: list[float] = []
    by_source: dict[str, Counter[str]] = {}
    by_source_ious: dict[str, list[float]] = {}
    by_class: dict[str, Counter[str]] = {}
    by_class_ious: dict[str, list[float]] = {}

    for frame_index in frame_indices:
        labels = label_frames.get(frame_index, [])
        predictions = pred_frames.get(frame_index, [])
        matches = greedy_matches(labels, predictions, iou_threshold)
        matched_count = len(matches)
        frame_ious = [iou for _, _, iou in matches]
        source = source_by_frame.get(frame_index, "missing")
        source_counter = by_source.setdefault(source, Counter())
        source_ious = by_source_ious.setdefault(source, [])
        frame_class_keys: set[str] = set()

        totals["labels"] += len(labels)
        totals["predictions"] += len(predictions)
        totals["matches"] += matched_count
        source_counter["frames"] += 1
        source_counter["labels"] += len(labels)
        source_counter["predictions"] += len(predictions)
        source_counter["matches"] += matched_count
        matched_ious.extend(frame_ious)
        source_ious.extend(frame_ious)

        for label in labels:
            key = class_key(label)
            by_class.setdefault(key, Counter())["labels"] += 1
            frame_class_keys.add(key)
        for prediction in predictions:
            key = class_key(prediction)
            by_class.setdefault(key, Counter())["predictions"] += 1
            frame_class_keys.add(key)
        for label_index, _, iou in matches:
            key = class_key(labels[label_index])
            by_class.setdefault(key, Counter())["matches"] += 1
            by_class_ious.setdefault(key, []).append(iou)
        for key in frame_class_keys:
            by_class.setdefault(key, Counter())["frames"] += 1

    def metrics(counter: Counter[str], ious: list[float]) -> dict[str, Any]:
        labels = int(counter.get("labels", 0))
        predictions = int(counter.get("predictions", 0))
        matches = int(counter.get("matches", 0))
        false_positives = max(0, predictions - matches)
        false_negatives = max(0, labels - matches)
        precision = matches / predictions if predictions else 0.0
        recall = matches / labels if labels else 0.0
        f1 = 2.0 * precision * recall / (precision + recall) if precision + recall else 0.0
        return {
            "frames": int(counter.get("frames", len(frame_indices))),
            "labels": labels,
            "predictions": predictions,
            "matches": matches,
            "false_positives": false_positives,
            "false_negatives": false_negatives,
            "precision": round(precision, 6),
            "recall": round(recall, 6),
            "f1": round(f1, 6),
            "mean_matched_iou": round(mean(ious), 6) if ious else 0.0,
        }

    return {
        "iou_threshold": iou_threshold,
        "overall": metrics(totals, matched_ious),
        "by_flow_frame_source": {
            source: metrics(counter, by_source_ious.get(source, []))
            for source, counter in sorted(by_source.items())
        },
        "by_class": {
            key: metrics(counter, by_class_ious.get(key, []))
            for key, counter in sorted(by_class.items())
        },
    }


def count_unique_tracks(payload: dict[str, Any]) -> int:
    track_ids: set[int] = set()
    for frame in payload.get("frames", []):
        if not isinstance(frame, dict):
            continue
        for track in frame.get("tracks") or []:
            if isinstance(track, dict) and "track_id" in track:
                track_ids.add(int(track["track_id"]))
    return len(track_ids)


def summarize_run(payload: dict[str, Any]) -> dict[str, Any]:
    frames = payload.get("frames") or []
    source_counts = Counter(str(frame.get("tracks_source", "unknown")) for frame in frames if isinstance(frame, dict))
    track_counts = [len(frame.get("tracks") or []) for frame in frames if isinstance(frame, dict)]
    return {
        "http_elapsed_sec": round(float(payload.get("_http_elapsed_sec", 0.0)), 3),
        "source_fps": payload.get("source_fps", payload.get("fps")),
        "target_detect_fps": payload.get("target_detect_fps"),
        "effective_detect_fps": payload.get("effective_detect_fps"),
        "stride_mode": payload.get("stride_mode"),
        "onnx_async": payload.get("onnx_async"),
        "frame_stride": payload.get("frame_stride"),
        "base_frame_stride": payload.get("base_frame_stride"),
        "min_frame_stride_used": payload.get("min_frame_stride_used"),
        "max_frame_stride_used": payload.get("max_frame_stride_used"),
        "final_frame_stride": payload.get("final_frame_stride"),
        "frame_count": payload.get("frame_count"),
        "source_frame_count": payload.get("source_frame_count"),
        "processed_frame_count": payload.get("processed_frame_count"),
        "display_frame_count": payload.get("display_frame_count", payload.get("frame_count")),
        "detected_frame_count": payload.get("detected_frame_count"),
        "high_res_detection_count": payload.get("high_res_detection_count"),
        "low_res_detection_count": payload.get("low_res_detection_count"),
        "roi_detection_count": payload.get("roi_detection_count"),
        "roi_requested_count": payload.get("roi_requested_count"),
        "roi_merged_count": payload.get("roi_merged_count"),
        "onnx_call_count": payload.get("onnx_call_count"),
        "async_infer_request_count": payload.get("async_infer_request_count"),
        "async_correction_count": payload.get("async_correction_count"),
        "async_corrected_frame_count": payload.get("async_corrected_frame_count"),
        "forced_detection_count": payload.get("forced_detection_count"),
        "scheduled_detection_count": payload.get("scheduled_detection_count"),
        "skipped_detection_count": payload.get("skipped_detection_count"),
        "weak_tracked_frame_count": payload.get("weak_tracked_frame_count"),
        "interpolated_frame_count": payload.get("interpolated_frame_count"),
        "empty_frame_count": payload.get("empty_frame_count"),
        "average_fps": payload.get("average_fps"),
        "cpu_utilization_percent": payload.get("cpu_utilization_percent"),
        "rss_memory_mb": payload.get("rss_memory_mb"),
        "queue_length": payload.get("queue_length"),
        "max_queue_length": payload.get("max_queue_length"),
        "dropped_frame_count": payload.get("dropped_frame_count"),
        "timing_ms": payload.get("timing_ms") or {},
        "timing_ratio": payload.get("timing_ratio") or {},
        "latency_percentiles_ms": payload.get("latency_percentiles_ms") or {},
        "metrics": payload.get("metrics") or {},
        "frame_source_counts": dict(source_counts),
        "unique_track_count": count_unique_tracks(payload),
        "avg_tracks_per_frame": round(mean(track_counts), 3) if track_counts else 0.0,
        "max_tracks_in_frame": max(track_counts) if track_counts else 0,
    }


def parse_iou_thresholds(text: str) -> list[float]:
    thresholds: list[float] = []
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        thresholds.append(float(item))
    if not thresholds:
        raise TestError("At least one IoU threshold is required")
    return thresholds


def write_pseudo_labels_jsonl(payload: dict[str, Any], output_path: Path) -> None:
    with output_path.open("w", encoding="utf-8") as file:
        for frame in payload.get("frames") or []:
            if not isinstance(frame, dict):
                continue
            item = {
                "frame_index": frame.get("frame_index"),
                "timestamp_ms": frame.get("timestamp_ms"),
                "labels": frame.get("tracks") or [],
            }
            file.write(json.dumps(item, ensure_ascii=False, separators=(",", ":")) + "\n")


def format_metric(value: Any) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.3f}"
    return str(value)


def performance_vs_full(full_summary: dict[str, Any], run_summary: dict[str, Any]) -> dict[str, Any]:
    full_elapsed = float(full_summary.get("http_elapsed_sec") or 0.0)
    run_elapsed = float(run_summary.get("http_elapsed_sec") or 0.0)
    full_processed = int(full_summary.get("processed_frame_count") or 0)
    run_processed = int(run_summary.get("processed_frame_count") or 0)
    full_display = int(full_summary.get("display_frame_count") or 0)
    run_display = int(run_summary.get("display_frame_count") or 0)
    return {
        "elapsed_speedup_full_over_run": round(full_elapsed / run_elapsed, 6) if run_elapsed else 0.0,
        "elapsed_saved_sec": round(full_elapsed - run_elapsed, 3),
        "full_onnx_display_fps": round(full_display / full_elapsed, 3) if full_elapsed else 0.0,
        "run_display_fps": round(run_display / run_elapsed, 3) if run_elapsed else 0.0,
        "full_onnx_processed_fps": round(full_processed / full_elapsed, 3) if full_elapsed else 0.0,
        "run_processed_fps": round(run_processed / run_elapsed, 3) if run_elapsed else 0.0,
        "onnx_frames_saved": full_processed - run_processed,
        "onnx_frame_reduction_ratio": round(
            (full_processed - run_processed) / full_processed,
            6,
        ) if full_processed else 0.0,
    }


def display_fps(summary: dict[str, Any]) -> float:
    elapsed = float(summary.get("http_elapsed_sec") or 0.0)
    display_frames = int(summary.get("display_frame_count") or 0)
    return round(display_frames / elapsed, 3) if elapsed else 0.0


def run_title(name: str) -> str:
    return {
        "full_onnx": "Full ONNX",
        "dynamic_onnx_flow": "Dynamic ONNX + Optical Flow",
        "fixed_onnx_flow": "Fixed ONNX + Optical Flow",
    }.get(name, name)


def track_color(track: dict[str, Any]) -> tuple[int, int, int]:
    track_id = int(track.get("track_id") or 0)
    class_id = int(track.get("class_id") or 0)
    hue = ((track_id * 37 + class_id * 17) % 180) / 180.0
    red, green, blue = colorsys.hsv_to_rgb(hue, 0.78, 1.0)
    return int(blue * 255), int(green * 255), int(red * 255)


def clipped_int(value: Any, low: int, high: int) -> int:
    return max(low, min(high, int(round(float(value or 0)))))


def draw_tracks(frame: Any, frame_item: dict[str, Any] | None, run_label: str, frame_index: int) -> None:
    import cv2

    height, width = frame.shape[:2]
    tracks = frame_item.get("tracks") if isinstance(frame_item, dict) else []
    if not isinstance(tracks, list):
        tracks = []
    source = str(frame_item.get("tracks_source", "missing")) if isinstance(frame_item, dict) else "missing"

    banner = f"{run_label} | frame {frame_index} | {source} | tracks {len(tracks)}"
    font = cv2.FONT_HERSHEY_SIMPLEX
    (banner_w, banner_h), _ = cv2.getTextSize(banner, font, 0.58, 2)
    cv2.rectangle(frame, (0, 0), (min(width, banner_w + 16), banner_h + 14), (0, 0, 0), -1)
    cv2.putText(frame, banner, (8, banner_h + 6), font, 0.58, (255, 255, 255), 2, cv2.LINE_AA)

    for track in tracks:
        if not isinstance(track, dict):
            continue
        box = track.get("box") or {}
        if not isinstance(box, dict):
            continue
        x1 = clipped_int(box.get("x1"), 0, width - 1)
        y1 = clipped_int(box.get("y1"), 0, height - 1)
        x2 = clipped_int(box.get("x2"), 0, width - 1)
        y2 = clipped_int(box.get("y2"), 0, height - 1)
        if x2 <= x1 or y2 <= y1:
            continue

        color = track_color(track)
        cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
        class_name = track.get("class_name") or str(track.get("class_id", "cls"))
        score = float(track.get("score") or 0.0)
        label = f"#{track.get('track_id', '?')} {class_name} {score:.2f}"
        (label_w, label_h), baseline = cv2.getTextSize(label, font, 0.42, 1)
        label_y1 = max(0, y1 - label_h - baseline - 4)
        label_y2 = label_y1 + label_h + baseline + 4
        label_x2 = min(width - 1, x1 + label_w + 6)
        cv2.rectangle(frame, (x1, label_y1), (label_x2, label_y2), color, -1)
        cv2.putText(
            frame,
            label,
            (x1 + 3, label_y2 - baseline - 2),
            font,
            0.42,
            (0, 0, 0),
            1,
            cv2.LINE_AA,
        )


def draw_tracks_pillow(image: Any, frame_item: dict[str, Any] | None, run_label: str, frame_index: int) -> None:
    from PIL import ImageDraw, ImageFont

    draw = ImageDraw.Draw(image)
    width, height = image.size
    tracks = frame_item.get("tracks") if isinstance(frame_item, dict) else []
    if not isinstance(tracks, list):
        tracks = []
    source = str(frame_item.get("tracks_source", "missing")) if isinstance(frame_item, dict) else "missing"

    font = ImageFont.load_default()
    banner = f"{run_label} | frame {frame_index} | {source} | tracks {len(tracks)}"
    banner_bbox = draw.textbbox((0, 0), banner, font=font)
    banner_w = banner_bbox[2] - banner_bbox[0]
    banner_h = banner_bbox[3] - banner_bbox[1]
    draw.rectangle((0, 0, min(width, banner_w + 16), banner_h + 14), fill=(0, 0, 0))
    draw.text((8, 7), banner, font=font, fill=(255, 255, 255))

    for track in tracks:
        if not isinstance(track, dict):
            continue
        box = track.get("box") or {}
        if not isinstance(box, dict):
            continue
        x1 = clipped_int(box.get("x1"), 0, width - 1)
        y1 = clipped_int(box.get("y1"), 0, height - 1)
        x2 = clipped_int(box.get("x2"), 0, width - 1)
        y2 = clipped_int(box.get("y2"), 0, height - 1)
        if x2 <= x1 or y2 <= y1:
            continue

        blue, green, red = track_color(track)
        color = (red, green, blue)
        draw.rectangle((x1, y1, x2, y2), outline=color, width=2)
        class_name = track.get("class_name") or str(track.get("class_id", "cls"))
        score = float(track.get("score") or 0.0)
        label = f"#{track.get('track_id', '?')} {class_name} {score:.2f}"
        label_bbox = draw.textbbox((0, 0), label, font=font)
        label_w = label_bbox[2] - label_bbox[0]
        label_h = label_bbox[3] - label_bbox[1]
        label_y1 = max(0, y1 - label_h - 6)
        label_y2 = label_y1 + label_h + 6
        label_x2 = min(width - 1, x1 + label_w + 6)
        draw.rectangle((x1, label_y1, label_x2, label_y2), fill=color)
        draw.text((x1 + 3, label_y1 + 3), label, font=font, fill=(0, 0, 0))


def find_ffmpeg() -> Path | None:
    env_path = shutil.which("ffmpeg")
    if env_path:
        return Path(env_path)

    candidates = [
        Path("/root/vcpkg/installed/x64-linux-gcc15/tools/ffmpeg/ffmpeg"),
        Path("/tmp/codex_video_deps/imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2"),
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def encode_h264_from_frames(
    frame_pattern: Path,
    output_path: Path,
    fps: float,
    log_path: Path,
) -> None:
    ffmpeg = find_ffmpeg()
    if ffmpeg is None:
        raise TestError("ffmpeg with libx264 is required to save VS Code-playable MP4 videos")

    command = [
        str(ffmpeg),
        "-y",
        "-framerate",
        f"{fps:g}",
        "-start_number",
        "0",
        "-i",
        str(frame_pattern),
        "-vf",
        "scale=in_range=pc:out_range=tv,format=yuv420p",
        "-c:v",
        "libx264",
        "-preset",
        "medium",
        "-crf",
        "23",
        "-pix_fmt",
        "yuv420p",
        "-movflags",
        "+faststart",
        "-an",
        str(output_path),
    ]
    with log_path.open("w", encoding="utf-8") as log_file:
        process = subprocess.run(
            command,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    if process.returncode != 0:
        raise TestError(f"ffmpeg failed while encoding {output_path}; see {log_path}")


def render_annotated_video_with_pillow(
    video_path: Path,
    payload: dict[str, Any],
    output_path: Path,
    run_label: str,
) -> dict[str, Any]:
    from PIL import Image

    ffmpeg = find_ffmpeg()
    if ffmpeg is None:
        raise TestError("ffmpeg is required to extract frames for annotated videos")

    frame_by_index = {
        int(frame.get("frame_index", -1)): frame
        for frame in payload.get("frames") or []
        if isinstance(frame, dict)
    }

    frame_dir = output_path.with_name(output_path.stem + "_annotated_frames")
    frame_dir.mkdir(parents=True, exist_ok=True)
    source_pattern = frame_dir / "source_%06d.jpg"
    frame_pattern = frame_dir / "frame_%06d.jpg"
    extract_log_path = output_path.with_name(output_path.stem + "_ffmpeg_extract.log")
    encode_log_path = output_path.with_name(output_path.stem + "_ffmpeg_h264.log")

    command = [
        str(ffmpeg),
        "-y",
        "-i",
        str(video_path),
        "-q:v",
        "2",
        str(source_pattern),
    ]
    with extract_log_path.open("w", encoding="utf-8") as log_file:
        process = subprocess.run(
            command,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    if process.returncode != 0:
        raise TestError(f"ffmpeg failed while extracting frames from {video_path}; see {extract_log_path}")

    source_frames = sorted(frame_dir.glob("source_*.jpg"))
    if not source_frames:
        raise TestError(f"No frames extracted from {video_path}; see {extract_log_path}")

    rendered = 0
    width = 0
    height = 0
    for rendered, source_frame in enumerate(source_frames):
        with Image.open(source_frame) as image:
            image = image.convert("RGB")
            width, height = image.size
            draw_tracks_pillow(image, frame_by_index.get(rendered), run_label, rendered)
            image.save(frame_dir / f"frame_{rendered:06d}.jpg", quality=92)
        source_frame.unlink(missing_ok=True)
    rendered += 1

    fps = float(payload.get("source_fps") or payload.get("fps") or 24.0)
    encode_h264_from_frames(frame_pattern, output_path, fps, encode_log_path)
    shutil.rmtree(frame_dir)

    return {
        "path": str(output_path),
        "frames": rendered,
        "fps": round(fps, 3),
        "width": width,
        "height": height,
        "codec": "h264",
        "ffmpeg_log": str(encode_log_path),
        "ffmpeg_extract_log": str(extract_log_path),
        "renderer": "pillow",
    }


def render_annotated_video(
    video_path: Path,
    payload: dict[str, Any],
    output_path: Path,
    run_label: str,
) -> dict[str, Any]:
    try:
        import cv2
    except ImportError:
        return render_annotated_video_with_pillow(video_path, payload, output_path, run_label)

    frame_by_index = {
        int(frame.get("frame_index", -1)): frame
        for frame in payload.get("frames") or []
        if isinstance(frame, dict)
    }

    capture = cv2.VideoCapture(str(video_path))
    if not capture.isOpened():
        raise TestError(f"Failed to open video for rendering: {video_path}")

    fps = float(capture.get(cv2.CAP_PROP_FPS) or payload.get("source_fps") or payload.get("fps") or 24.0)
    width = int(capture.get(cv2.CAP_PROP_FRAME_WIDTH) or payload.get("width") or 0)
    height = int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT) or payload.get("height") or 0)
    if width <= 0 or height <= 0:
        capture.release()
        raise TestError(f"Invalid video dimensions for rendering: {width}x{height}")

    frame_dir = output_path.with_name(output_path.stem + "_annotated_frames")
    frame_dir.mkdir(parents=True, exist_ok=True)
    frame_pattern = frame_dir / "frame_%06d.jpg"
    log_path = output_path.with_name(output_path.stem + "_ffmpeg_h264.log")

    rendered = 0
    try:
        while True:
            ok, frame = capture.read()
            if not ok:
                break
            draw_tracks(frame, frame_by_index.get(rendered), run_label, rendered)
            frame_path = frame_dir / f"frame_{rendered:06d}.jpg"
            if not cv2.imwrite(str(frame_path), frame, [int(cv2.IMWRITE_JPEG_QUALITY), 92]):
                raise TestError(f"Failed to write annotated frame: {frame_path}")
            rendered += 1
    finally:
        capture.release()

    encode_h264_from_frames(frame_pattern, output_path, fps, log_path)
    shutil.rmtree(frame_dir)

    return {
        "path": str(output_path),
        "frames": rendered,
        "fps": round(fps, 3),
        "width": width,
        "height": height,
        "codec": "h264",
        "ffmpeg_log": str(log_path),
        "renderer": "opencv",
    }

def format_markdown_report(comparison: dict[str, Any]) -> str:
    runs = comparison["runs"]
    performance_by_run = comparison["performance_vs_full_onnx"]
    quality_by_run = comparison["quality_vs_full_onnx_labels"]
    run_order = comparison["run_order"]

    lines = [
        "# Full ONNX vs Dynamic/Fixed ONNX + Optical Flow",
        "",
        f"- Video: `{comparison['video']}`",
        "- Label source: full-frame ONNX detections from `full_onnx`.",
        f"- Output directory: `{comparison['output_dir']}`",
        "",
        "## Run Summary",
        "",
        "| run | stride mode | async | target detect fps | base stride | min stride | max stride | final stride | display frames | ONNX frames | high | low | ROI | ONNX calls | elapsed sec | display fps | unique tracks |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    for name in run_order:
        summary = runs[name]
        lines.append(
            f"| {name} | {format_metric(summary.get('stride_mode'))} | "
            f"{format_metric(summary.get('onnx_async'))} | "
            f"{format_metric(summary.get('target_detect_fps'))} | "
            f"{format_metric(summary.get('base_frame_stride'))} | "
            f"{format_metric(summary.get('min_frame_stride_used'))} | "
            f"{format_metric(summary.get('max_frame_stride_used'))} | "
            f"{format_metric(summary.get('final_frame_stride'))} | "
            f"{format_metric(summary.get('display_frame_count'))} | "
            f"{format_metric(summary.get('processed_frame_count'))} | "
            f"{format_metric(summary.get('high_res_detection_count'))} | "
            f"{format_metric(summary.get('low_res_detection_count'))} | "
            f"{format_metric(summary.get('roi_detection_count'))} | "
            f"{format_metric(summary.get('onnx_call_count'))} | "
            f"{format_metric(summary.get('http_elapsed_sec'))} | "
            f"{format_metric(display_fps(summary))} | "
            f"{format_metric(summary.get('unique_track_count'))} |"
        )

    lines.extend(["", "## Stage Timing", ""])
    lines.extend([
        "| run | ONNX ms | high ms | low ms | ROI ms | flow ms | postprocess ms | profiled ms | total video ms | ONNX % | flow % | postprocess % |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for name in run_order:
        summary = runs[name]
        timing = summary.get("timing_ms") or {}
        ratio = summary.get("timing_ratio") or {}
        lines.append(
            f"| {name} | {format_metric(timing.get('onnx_inference_ms'))} | "
            f"{format_metric(timing.get('high_res_onnx_ms'))} | "
            f"{format_metric(timing.get('low_res_onnx_ms'))} | "
            f"{format_metric(timing.get('roi_onnx_ms'))} | "
            f"{format_metric(timing.get('optical_flow_ms'))} | "
            f"{format_metric(timing.get('postprocess_ms'))} | "
            f"{format_metric(timing.get('profiled_stage_ms'))} | "
            f"{format_metric(timing.get('total_elapsed_ms'))} | "
            f"{format_metric(float(ratio.get('onnx_inference') or 0.0) * 100.0)} | "
            f"{format_metric(float(ratio.get('optical_flow') or 0.0) * 100.0)} | "
            f"{format_metric(float(ratio.get('postprocess') or 0.0) * 100.0)} |"
        )

    lines.extend(["", "## Performance vs Full ONNX", ""])
    lines.extend([
        "| run | speedup | elapsed saved sec | ONNX frames saved | ONNX frame reduction | run display fps | run ONNX fps |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for name in run_order:
        if name == "full_onnx":
            continue
        perf = performance_by_run[name]
        lines.append(
            f"| {name} | {format_metric(perf['elapsed_speedup_full_over_run'])}x | "
            f"{format_metric(perf['elapsed_saved_sec'])} | "
            f"{format_metric(perf['onnx_frames_saved'])} | "
            f"{format_metric(perf['onnx_frame_reduction_ratio'])} | "
            f"{format_metric(perf['run_display_fps'])} | "
            f"{format_metric(perf['run_processed_fps'])} |"
        )

    for name in run_order:
        if name == "full_onnx":
            continue
        lines.extend(["", f"## Quality: {run_title(name)}", ""])
        lines.extend([
            "| IoU threshold | precision | recall | F1 | mean matched IoU | matches | labels | predictions | FP | FN |",
            "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ])
        for item in quality_by_run[name]:
            overall = item["overall"]
            lines.append(
                f"| {format_metric(item['iou_threshold'])} | "
                f"{format_metric(overall['precision'])} | "
                f"{format_metric(overall['recall'])} | "
                f"{format_metric(overall['f1'])} | "
                f"{format_metric(overall['mean_matched_iou'])} | "
                f"{overall['matches']} | {overall['labels']} | {overall['predictions']} | "
                f"{overall['false_positives']} | {overall['false_negatives']} |"
            )

        lines.extend(["", "### Frame Sources", ""])
        lines.extend([
            "| source | frames | labels | predictions | matches | precision | recall | F1 |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ])
        primary_quality = quality_by_run[name][0]
        for source, metrics in primary_quality["by_flow_frame_source"].items():
            lines.append(
                f"| {source} | {metrics['frames']} | {metrics['labels']} | "
                f"{metrics['predictions']} | {metrics['matches']} | "
                f"{format_metric(metrics['precision'])} | "
                f"{format_metric(metrics['recall'])} | "
                f"{format_metric(metrics['f1'])} |"
            )

    lines.extend(["", "## Artifacts", ""])
    for name, artifact_path in comparison["artifacts"].items():
        lines.append(f"- `{name}`: `{artifact_path}`")

    lines.append("")
    return "\n".join(lines)

def main() -> int:
    args = parse_args()
    binary = resolve_path(args.binary)
    config = resolve_path(args.config)
    video = resolve_path(args.video)
    output_root = resolve_path(args.output_root)
    iou_thresholds = parse_iou_thresholds(args.iou_thresholds)
    fixed_flow_detect_fps = (
        args.fixed_flow_detect_fps
        if args.fixed_flow_detect_fps is not None
        else args.flow_detect_fps
    )

    if not binary.is_file():
        raise TestError(f"Server binary does not exist: {binary}")
    if not config.is_file():
        raise TestError(f"Config file does not exist: {config}")
    if not video.is_file():
        raise TestError(f"Video does not exist: {video}")
    if args.flow_detect_fps <= 0.0:
        raise TestError("--flow-detect-fps must be positive")
    if fixed_flow_detect_fps <= 0.0:
        raise TestError("--fixed-flow-detect-fps must be positive")

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    output_dir = output_root / (video.stem + "_full_dynamic_fixed_onnx_flow_" + timestamp)
    output_dir.mkdir(parents=True, exist_ok=False)

    run_specs = [
        {"name": "full_onnx", "detect_fps": 0.0, "stride_mode": "dynamic"},
        {"name": "dynamic_onnx_flow", "detect_fps": args.flow_detect_fps, "stride_mode": "dynamic"},
        {"name": "fixed_onnx_flow", "detect_fps": fixed_flow_detect_fps, "stride_mode": "fixed"},
    ]
    run_order = [str(spec["name"]) for spec in run_specs]

    payloads: dict[str, dict[str, Any]] = {}
    runtime_configs: dict[str, Path] = {}
    for spec in run_specs:
        name = str(spec["name"])
        runtime_config = make_runtime_config(
            config,
            output_dir,
            name,
            float(spec["detect_fps"]),
            str(spec["stride_mode"]),
            bool(args.use_async),
        )
        runtime_configs[name] = runtime_config
        payloads[name] = run_video_case(
            name,
            binary,
            runtime_config,
            video,
            output_dir,
            args.host,
            args.port,
            args.startup_timeout,
            args.request_timeout,
        )

    summaries = {name: summarize_run(payloads[name]) for name in run_order}
    full_payload = payloads["full_onnx"]
    full_summary = summaries["full_onnx"]
    comparison_path = output_dir / "comparison.json"
    report_path = output_dir / "comparison.md"
    pseudo_labels_path = output_dir / "full_onnx_pseudo_labels.jsonl"

    write_pseudo_labels_jsonl(full_payload, pseudo_labels_path)

    video_artifacts: dict[str, dict[str, Any]] = {}
    if not args.no_save_videos:
        for name in run_order:
            output_video = output_dir / f"{name}_detections.mp4"
            print(f"[{name}] rendering annotated video {output_video}", flush=True)
            video_artifacts[name] = render_annotated_video(
                video,
                payloads[name],
                output_video,
                run_title(name),
            )

    quality_by_run = {
        name: [evaluate_at_threshold(full_payload, payloads[name], threshold) for threshold in iou_thresholds]
        for name in run_order
        if name != "full_onnx"
    }
    performance_by_run = {
        name: performance_vs_full(full_summary, summaries[name])
        for name in run_order
        if name != "full_onnx"
    }

    artifacts: dict[str, str] = {
        "comparison_json": str(comparison_path),
        "markdown_report": str(report_path),
        "full_onnx_pseudo_labels_jsonl": str(pseudo_labels_path),
    }
    for name in run_order:
        artifacts[f"{name}_config"] = str(runtime_configs[name])
        artifacts[f"{name}_response"] = str(output_dir / f"{name}_infer_video_response.json")
        if name in video_artifacts:
            artifacts[f"{name}_detections_video"] = video_artifacts[name]["path"]

    comparison = {
        "video": str(video),
        "output_dir": str(output_dir),
        "full_onnx_as_pseudo_labels": True,
        "run_order": run_order,
        "runs": summaries,
        "full_onnx": summaries["full_onnx"],
        "onnx_flow": summaries["dynamic_onnx_flow"],
        "dynamic_onnx_flow": summaries["dynamic_onnx_flow"],
        "fixed_onnx_flow": summaries["fixed_onnx_flow"],
        "performance_vs_full_onnx": performance_by_run,
        "performance": performance_by_run["dynamic_onnx_flow"],
        "quality_vs_full_onnx_labels": quality_by_run,
        "onnx_flow_quality_vs_full_onnx_labels": quality_by_run["dynamic_onnx_flow"],
        "video_artifacts": video_artifacts,
        "artifacts": artifacts,
    }

    comparison_path.write_text(
        json.dumps(comparison, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    report_path.write_text(format_markdown_report(comparison), encoding="utf-8")
    print("COMPARISON_JSON_START")
    print(json.dumps(comparison, ensure_ascii=False, indent=2))
    print("COMPARISON_JSON_END")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"TEST_FAILED: {exc}", file=sys.stderr)
        raise
