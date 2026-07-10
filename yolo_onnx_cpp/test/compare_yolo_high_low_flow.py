#!/usr/bin/env python3
"""Compare five production-relevant YOLO video inference modes.

The full-frame high-resolution run is used only as pseudo-labels. The
integrated high/low result is produced by the C++ /infer_video_high_low
endpoint; no test-side fusion is performed.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean, median
from typing import Any

import compare_full_onnx_vs_flow as common


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = REPO_ROOT / "build" / "yolo_api"
DEFAULT_CONFIG = REPO_ROOT / "yolo_onnx_cpp" / "config.yaml"
DEFAULT_VIDEO = (
    REPO_ROOT
    / "datasets"
    / "bdd100k_tracking_video"
    / "bdd100k_videos_train_00"
    / "bdd100k"
    / "videos"
    / "train"
    / "0000f77c-6257be58.mov"
)
DEFAULT_HIGH_MODEL = REPO_ROOT / "yolo_onnx_cpp" / "deploy" / "best.onnx"
DEFAULT_LOW_MODEL = REPO_ROOT / "yolo_onnx_cpp" / "deploy" / "best_640x384.onnx"
DEFAULT_OUTPUT_ROOT = (
    REPO_ROOT
    / "yolo_onnx_cpp"
    / "test_outputs"
    / "video_compare"
    / "video_algorithm_report_20260710"
)

RUN_TITLES = {
    "full_high": "Full high-resolution YOLO",
    "high_dynamic_flow": "High-resolution dynamic stride + flow",
    "high_fixed_flow": "High-resolution fixed stride + flow",
    "low_dynamic_flow": "Low-resolution dynamic stride + flow",
    "integrated_high_low_flow": "Integrated high/low YOLO + flow",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare full high-resolution inference, high-resolution dynamic/fixed "
            "flow, low-resolution dynamic flow, and integrated high/low flow."
        )
    )
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--video", type=Path, default=DEFAULT_VIDEO)
    parser.add_argument("--high-model", type=Path, default=DEFAULT_HIGH_MODEL)
    parser.add_argument("--low-model", type=Path, default=DEFAULT_LOW_MODEL)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--flow-detect-fps", type=float, default=4.0)
    parser.add_argument("--high-width", type=int, default=1280)
    parser.add_argument("--high-height", type=int, default=736)
    parser.add_argument("--low-width", type=int, default=640)
    parser.add_argument("--low-height", type=int, default=384)
    parser.add_argument("--startup-timeout", type=float, default=120.0)
    parser.add_argument("--request-timeout", type=float, default=3600.0)
    parser.add_argument("--iou-thresholds", default="0.3,0.5,0.7")
    parser.add_argument("--no-save-videos", action="store_true")
    return parser.parse_args()


def resolve_path(path: Path) -> Path:
    if path.is_absolute():
        return path
    return (REPO_ROOT / path).resolve()


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise common.TestError(f"{label} does not exist: {path}")


def make_runtime_config(
    base_config: Path,
    output_dir: Path,
    name: str,
    primary_model: Path,
    primary_width: int,
    primary_height: int,
    low_model: Path,
    low_width: int,
    low_height: int,
    enable_low_model: bool,
    detect_fps: float,
    stride_mode: str,
    model_async: bool,
) -> Path:
    lines = base_config.read_text(encoding="utf-8").splitlines()
    values = {
        "model_path": str(primary_model),
        "model_backend": "onnx",
        "input_width": str(primary_width),
        "input_height": str(primary_height),
        "low_res_model_path": str(low_model) if enable_low_model else "",
        "low_res_input_width": str(low_width),
        "low_res_input_height": str(low_height),
        "conf_threshold": "0.25",
        "low_res_conf_threshold": "0.25",
        "iou_threshold": "0.45",
        "low_res_iou_threshold": "0.45",
        "thread_num": "4",
        "use_letterbox": "true",
        "video_detect_fps": f"{detect_fps:g}",
        "video_stride_mode": stride_mode,
        "video_model_async": "true" if model_async else "false",
        "video_onnx_async": "true" if model_async else "false",
    }
    for key, value in values.items():
        lines = common.update_scalar_config(lines, key, value)

    runtime_config = output_dir / f"{name}_config.yaml"
    runtime_config.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return runtime_config


def nearest_percentile(values: list[int], ratio: float) -> int:
    if not values:
        return 0
    ordered = sorted(values)
    index = round((len(ordered) - 1) * ratio)
    return ordered[index]


def tracking_proxy(payload: dict[str, Any]) -> dict[str, Any]:
    observations: dict[int, list[int]] = {}
    observation_count = 0
    for frame in payload.get("frames") or []:
        if not isinstance(frame, dict):
            continue
        frame_index = int(frame.get("frame_index", -1))
        for track in frame.get("tracks") or []:
            if not isinstance(track, dict) or "track_id" not in track:
                continue
            track_id = int(track["track_id"])
            observations.setdefault(track_id, []).append(frame_index)
            observation_count += 1

    segment_lengths: list[int] = []
    reappearance_count = 0
    for indices in observations.values():
        ordered = sorted(set(indices))
        if not ordered:
            continue
        current_length = 1
        for previous, current in zip(ordered, ordered[1:]):
            if current == previous + 1:
                current_length += 1
            else:
                segment_lengths.append(current_length)
                current_length = 1
                reappearance_count += 1
        segment_lengths.append(current_length)

    return {
        "track_observation_count": observation_count,
        "unique_track_count": len(observations),
        "contiguous_segment_count": len(segment_lengths),
        "reappearance_count": reappearance_count,
        "mean_contiguous_lifetime_frames": round(mean(segment_lengths), 3)
        if segment_lengths
        else 0.0,
        "median_contiguous_lifetime_frames": round(float(median(segment_lengths)), 3)
        if segment_lengths
        else 0.0,
        "p95_contiguous_lifetime_frames": nearest_percentile(segment_lengths, 0.95),
        "max_contiguous_lifetime_frames": max(segment_lengths, default=0),
    }


def derived_model_calls(name: str, summary: dict[str, Any]) -> dict[str, int | None]:
    processed = int(summary.get("processed_frame_count") or 0)
    async_requests = int(summary.get("async_infer_request_count") or 0)
    if name == "integrated_high_low_flow":
        return {
            "total": processed + async_requests,
            "high_res": async_requests,
            "low_res": processed,
        }
    total = async_requests if summary.get("onnx_async") else processed
    return {"total": total, "high_res": None, "low_res": None}


def summarize_payload(
    name: str,
    payload: dict[str, Any],
    spec: dict[str, Any],
    config_path: Path,
) -> dict[str, Any]:
    summary = common.summarize_run(payload)
    summary["display_fps"] = common.display_fps(summary)
    summary["model"] = spec["model"]
    summary["endpoint"] = spec["endpoint"]
    summary["detect_fps"] = spec["detect_fps"]
    summary["configured_stride_mode"] = spec["stride_mode"]
    summary["config"] = str(config_path)
    summary["tracking_proxy"] = tracking_proxy(payload)
    summary["derived_model_calls"] = derived_model_calls(name, summary)
    return summary


def evaluate_runs(
    full_payload: dict[str, Any],
    payloads: dict[str, dict[str, Any]],
    run_order: list[str],
    thresholds: list[float],
) -> dict[str, list[dict[str, Any]]]:
    return {
        name: [
            common.evaluate_at_threshold(full_payload, payloads[name], threshold)
            for threshold in thresholds
        ]
        for name in run_order
        if name != "full_high"
    }


def worst_frames(
    full_payload: dict[str, Any],
    candidate_payload: dict[str, Any],
    iou_threshold: float = 0.5,
    limit: int = 5,
) -> list[dict[str, Any]]:
    labels_by_frame = common.frame_tracks_by_index(full_payload)
    predictions_by_frame = common.frame_tracks_by_index(candidate_payload)
    sources = common.frame_source_by_index(candidate_payload)
    source_fps = float(candidate_payload.get("source_fps") or 30.0)
    candidates: list[dict[str, Any]] = []

    for frame_index in sorted(set(labels_by_frame) | set(predictions_by_frame)):
        labels = labels_by_frame.get(frame_index, [])
        predictions = predictions_by_frame.get(frame_index, [])
        if not labels and not predictions:
            continue
        matches = common.greedy_matches(labels, predictions, iou_threshold)
        matched = len(matches)
        precision = matched / len(predictions) if predictions else 0.0
        recall = matched / len(labels) if labels else 0.0
        f1 = (
            2.0 * precision * recall / (precision + recall)
            if precision + recall
            else 0.0
        )
        false_positives = len(predictions) - matched
        false_negatives = len(labels) - matched
        candidates.append({
            "frame_index": frame_index,
            "timestamp_sec": round(frame_index / source_fps, 3),
            "frame_source": sources.get(frame_index, "missing"),
            "labels": len(labels),
            "predictions": len(predictions),
            "matches": matched,
            "false_positives": false_positives,
            "false_negatives": false_negatives,
            "f1": round(f1, 6),
            "error_count": false_positives + false_negatives,
        })

    candidates.sort(
        key=lambda item: (
            item["f1"],
            -item["error_count"],
            -item["false_negatives"],
            item["frame_index"],
        )
    )
    separation_frames = max(1, round(source_fps * 2.0))
    selected: list[dict[str, Any]] = []
    for item in candidates:
        if all(
            abs(int(item["frame_index"]) - int(existing["frame_index"]))
            >= separation_frames
            for existing in selected
        ):
            selected.append(item)
        if len(selected) >= limit:
            break
    return selected


def render_comparison_grid(
    source_video: Path,
    videos: list[Path],
    output_path: Path,
    fps: float,
    frame_count: int,
) -> dict[str, Any]:
    if len(videos) != 5:
        raise common.TestError("comparison grid requires exactly five annotated videos")
    ffmpeg = common.find_ffmpeg()
    if ffmpeg is None:
        raise common.TestError("ffmpeg with libx264 is required for the comparison grid")

    inputs = [source_video, *videos]
    command = [str(ffmpeg), "-y"]
    for input_path in inputs:
        command.extend(["-i", str(input_path)])

    filters = [
        (
            f"[{index}:v]scale=640:360:force_original_aspect_ratio=decrease,"
            f"pad=640:360:(ow-iw)/2:(oh-ih)/2:black,setsar=1,setpts=PTS-STARTPTS[v{index}]"
        )
        for index in range(6)
    ]
    filters.append(
        "[v0][v1][v2][v3][v4][v5]"
        "xstack=inputs=6:layout=0_0|640_0|1280_0|0_360|640_360|1280_360:fill=black[stack]"
    )
    filters.append("[stack]scale=in_range=pc:out_range=tv,format=yuv420p[out]")
    log_path = output_path.with_name(output_path.stem + "_ffmpeg_h264.log")
    command.extend([
        "-filter_complex",
        ";".join(filters),
        "-map",
        "[out]",
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
        "-r",
        f"{fps:g}",
        "-frames:v",
        str(frame_count),
        "-shortest",
        str(output_path),
    ])
    with log_path.open("w", encoding="utf-8") as log_file:
        process = subprocess.run(
            command,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    if process.returncode != 0:
        raise common.TestError(f"ffmpeg failed while creating {output_path}; see {log_path}")
    return {
        "path": str(output_path),
        "codec": "h264",
        "pix_fmt": "yuv420p",
        "width": 1920,
        "height": 720,
        "fps": round(fps, 6),
        "frames": frame_count,
        "layout": ["source", *RUN_TITLES],
        "ffmpeg_log": str(log_path),
    }


def fmt(value: Any, digits: int = 3) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def markdown_report(comparison: dict[str, Any]) -> str:
    runs = comparison["runs"]
    run_order = comparison["run_order"]
    quality = comparison["quality_vs_full_high"]
    performance = comparison["performance_vs_full_high"]
    lines = [
        "# 五种视频推理模式对比",
        "",
        f"- 原始视频：`{comparison['video']}`",
        f"- 输出目录：`{comparison['output_dir']}`",
        "- 质量参照：`full_high` 全帧高分辨率结果（伪标签，不是真值）。",
        "- 后端：ONNX Runtime CPU；4 线程；高/低分辨率置信度阈值均为 0.25。",
        "",
        "## 运行摘要",
        "",
        "| 模式 | endpoint | stride | async | 帧数 | 模型调用(推导) | 耗时(s) | FPS | CPU(%) | RSS(MB) | 唯一ID |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for name in run_order:
        run = runs[name]
        lines.append(
            f"| {name} | `{run['endpoint']}` | {fmt(run.get('stride_mode'))} | "
            f"{fmt(run.get('onnx_async'))} | {fmt(run.get('frame_count'))} | "
            f"{fmt((run.get('derived_model_calls') or {}).get('total'))} | "
            f"{fmt(run.get('http_elapsed_sec'))} | {fmt(run.get('display_fps'))} | "
            f"{fmt(run.get('cpu_utilization_percent'))} | {fmt(run.get('rss_memory_mb'))} | "
            f"{fmt((run.get('tracking_proxy') or {}).get('unique_track_count'))} |"
        )

    lines.extend([
        "",
        "## 阶段耗时与延迟",
        "",
        "| 模式 | infer总计(ms) | preprocess(ms) | postprocess(ms) | flow(ms) | tracker(ms) | E2E p50(ms) | E2E p95(ms) | E2E p99(ms) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for name in run_order:
        run = runs[name]
        timing = run.get("timing_ms") or {}
        end_to_end = (run.get("latency_percentiles_ms") or {}).get("end_to_end_ms") or {}
        lines.append(
            f"| {name} | {fmt(timing.get('infer_ms'))} | "
            f"{fmt(timing.get('preprocess_ms'))} | {fmt(timing.get('postprocess_ms'))} | "
            f"{fmt(timing.get('optical_flow_ms'))} | {fmt(timing.get('tracker_ms'))} | "
            f"{fmt(end_to_end.get('p50'))} | {fmt(end_to_end.get('p95'))} | "
            f"{fmt(end_to_end.get('p99'))} |"
        )

    lines.extend([
        "",
        "## 相对全帧基准的性能",
        "",
        "| 模式 | 加速比 | 节省耗时(s) | 减少处理帧 | 处理帧减少比例 |",
        "| --- | ---: | ---: | ---: | ---: |",
    ])
    for name in run_order:
        if name == "full_high":
            continue
        item = performance[name]
        lines.append(
            f"| {name} | {fmt(item['elapsed_speedup_full_over_run'])}x | "
            f"{fmt(item['elapsed_saved_sec'])} | {fmt(item['onnx_frames_saved'])} | "
            f"{fmt(item['onnx_frame_reduction_ratio'])} |"
        )

    for name in run_order:
        if name == "full_high":
            continue
        lines.extend([
            "",
            f"## 质量一致性：{name}",
            "",
            "| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |",
            "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ])
        for item in quality[name]:
            overall = item["overall"]
            lines.append(
                f"| {fmt(item['iou_threshold'])} | {fmt(overall['precision'], 6)} | "
                f"{fmt(overall['recall'], 6)} | {fmt(overall['f1'], 6)} | "
                f"{fmt(overall['mean_matched_iou'], 6)} | {overall['matches']} | "
                f"{overall['labels']} | {overall['predictions']} | "
                f"{overall['false_positives']} | {overall['false_negatives']} |"
            )

    lines.extend([
        "",
        "## 跟踪代理指标",
        "",
        "| 模式 | observations | unique IDs | segments | reappearances | mean lifetime | median lifetime | p95 lifetime |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for name in run_order:
        proxy = runs[name]["tracking_proxy"]
        lines.append(
            f"| {name} | {proxy['track_observation_count']} | {proxy['unique_track_count']} | "
            f"{proxy['contiguous_segment_count']} | {proxy['reappearance_count']} | "
            f"{fmt(proxy['mean_contiguous_lifetime_frames'])} | "
            f"{fmt(proxy['median_contiguous_lifetime_frames'])} | "
            f"{proxy['p95_contiguous_lifetime_frames']} |"
        )

    lines.extend(["", "## IoU=0.5 最差时间点", ""])
    for name, items in comparison["worst_frames_iou_0_5"].items():
        lines.extend([
            f"### {name}",
            "",
            "| frame | time(s) | source | F1 | FP | FN | labels | predictions |",
            "| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |",
        ])
        for item in items:
            lines.append(
                f"| {item['frame_index']} | {fmt(item['timestamp_sec'])} | "
                f"{item['frame_source']} | {fmt(item['f1'], 6)} | "
                f"{item['false_positives']} | {item['false_negatives']} | "
                f"{item['labels']} | {item['predictions']} |"
            )
        lines.append("")

    lines.extend(["## 视频与数据产物", ""])
    for name, path in comparison["artifacts"].items():
        lines.append(f"- `{name}`：`{path}`")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    binary = resolve_path(args.binary)
    base_config = resolve_path(args.config)
    video = resolve_path(args.video)
    high_model = resolve_path(args.high_model)
    low_model = resolve_path(args.low_model)
    output_root = resolve_path(args.output_root)
    thresholds = common.parse_iou_thresholds(args.iou_thresholds)

    require_file(binary, "yolo_api binary")
    require_file(base_config, "config")
    require_file(video, "video")
    require_file(high_model, "high model")
    require_file(low_model, "low model")
    if args.flow_detect_fps <= 0.0:
        raise common.TestError("--flow-detect-fps must be positive")

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    output_dir = output_root / f"{video.stem}_{timestamp}"
    output_dir.mkdir(parents=True, exist_ok=False)

    specs: list[dict[str, Any]] = [
        {
            "name": "full_high",
            "model": "high",
            "primary_model": high_model,
            "width": args.high_width,
            "height": args.high_height,
            "detect_fps": 0.0,
            "stride_mode": "dynamic",
            "model_async": False,
            "endpoint": "/infer_video",
            "enable_low_model": False,
        },
        {
            "name": "high_dynamic_flow",
            "model": "high",
            "primary_model": high_model,
            "width": args.high_width,
            "height": args.high_height,
            "detect_fps": args.flow_detect_fps,
            "stride_mode": "dynamic",
            "model_async": True,
            "endpoint": "/infer_video",
            "enable_low_model": False,
        },
        {
            "name": "high_fixed_flow",
            "model": "high",
            "primary_model": high_model,
            "width": args.high_width,
            "height": args.high_height,
            "detect_fps": args.flow_detect_fps,
            "stride_mode": "fixed",
            "model_async": True,
            "endpoint": "/infer_video",
            "enable_low_model": False,
        },
        {
            "name": "low_dynamic_flow",
            "model": "low",
            "primary_model": low_model,
            "width": args.low_width,
            "height": args.low_height,
            "detect_fps": args.flow_detect_fps,
            "stride_mode": "dynamic",
            "model_async": True,
            "endpoint": "/infer_video",
            "enable_low_model": False,
        },
        {
            "name": "integrated_high_low_flow",
            "model": "high+low",
            "primary_model": high_model,
            "width": args.high_width,
            "height": args.high_height,
            "detect_fps": args.flow_detect_fps,
            "stride_mode": "dynamic",
            "model_async": True,
            "endpoint": "/infer_video_high_low",
            "enable_low_model": True,
        },
    ]
    run_order = [str(spec["name"]) for spec in specs]

    payloads: dict[str, dict[str, Any]] = {}
    runtime_configs: dict[str, Path] = {}
    for spec in specs:
        name = str(spec["name"])
        runtime_config = make_runtime_config(
            base_config,
            output_dir,
            name,
            Path(spec["primary_model"]),
            int(spec["width"]),
            int(spec["height"]),
            low_model,
            args.low_width,
            args.low_height,
            bool(spec["enable_low_model"]),
            float(spec["detect_fps"]),
            str(spec["stride_mode"]),
            bool(spec["model_async"]),
        )
        runtime_configs[name] = runtime_config
        payloads[name] = common.run_video_case(
            name=name,
            binary=binary,
            runtime_config=runtime_config,
            video_path=video,
            output_dir=output_dir,
            host=args.host,
            port=args.port,
            startup_timeout=args.startup_timeout,
            request_timeout=args.request_timeout,
            endpoint=str(spec["endpoint"]),
        )

    pseudo_labels_path = output_dir / "full_high_pseudo_labels.jsonl"
    common.write_pseudo_labels_jsonl(payloads["full_high"], pseudo_labels_path)
    runs = {
        str(spec["name"]): summarize_payload(
            str(spec["name"]),
            payloads[str(spec["name"])],
            spec,
            runtime_configs[str(spec["name"])],
        )
        for spec in specs
    }
    quality = evaluate_runs(payloads["full_high"], payloads, run_order, thresholds)
    performance = {
        name: common.performance_vs_full(runs["full_high"], runs[name])
        for name in run_order
        if name != "full_high"
    }
    worst = {
        name: worst_frames(payloads["full_high"], payloads[name])
        for name in run_order
        if name != "full_high"
    }

    artifacts: dict[str, str] = {
        "full_high_pseudo_labels": str(pseudo_labels_path),
    }
    video_artifacts: dict[str, dict[str, Any]] = {}
    if not args.no_save_videos:
        for name in run_order:
            output_video = output_dir / f"{name}_detections.mp4"
            print(f"[{name}] rendering H.264 video {output_video}", flush=True)
            video_artifacts[name] = common.render_annotated_video(
                video,
                payloads[name],
                output_video,
                RUN_TITLES[name],
            )
            artifacts[f"{name}_video"] = str(output_video)

        grid_path = output_dir / "all_modes_comparison_grid.mp4"
        grid_info = render_comparison_grid(
            video,
            [Path(video_artifacts[name]["path"]) for name in run_order],
            grid_path,
            float(payloads["full_high"].get("source_fps") or 30.0),
            int(payloads["full_high"].get("frame_count") or 0),
        )
        artifacts["comparison_grid_video"] = str(grid_path)
    else:
        grid_info = {}

    comparison_path = output_dir / "comparison.json"
    report_path = output_dir / "comparison.md"
    for name in run_order:
        artifacts[f"{name}_config"] = str(runtime_configs[name])
        artifacts[f"{name}_response"] = str(
            output_dir / f"{name}_infer_video_response.json"
        )
    artifacts["comparison_json"] = str(comparison_path)
    artifacts["comparison_markdown"] = str(report_path)

    comparison = {
        "video": str(video),
        "output_dir": str(output_dir),
        "full_high_as_pseudo_labels": True,
        "thresholds": thresholds,
        "run_order": run_order,
        "runs": runs,
        "performance_vs_full_high": performance,
        "quality_vs_full_high": quality,
        "worst_frames_iou_0_5": worst,
        "video_artifacts": video_artifacts,
        "comparison_grid": grid_info,
        "artifacts": artifacts,
    }
    comparison_path.write_text(
        json.dumps(comparison, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    report_path.write_text(markdown_report(comparison), encoding="utf-8")
    print(json.dumps({
        "output_dir": str(output_dir),
        "comparison_json": str(comparison_path),
        "comparison_md": str(report_path),
        "run_summary": {
            name: {
                "elapsed_sec": runs[name]["http_elapsed_sec"],
                "display_fps": runs[name]["display_fps"],
                "model_calls": runs[name]["derived_model_calls"],
                "unique_tracks": runs[name]["tracking_proxy"]["unique_track_count"],
            }
            for name in run_order
        },
        "quality": {
            name: [item["overall"] for item in items]
            for name, items in quality.items()
        },
        "artifacts": artifacts,
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except common.TestError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
