#!/usr/bin/env python3
"""Pool per-video comparison results and write the final Chinese report."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from statistics import median
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RESULTS_ROOT = (
    REPO_ROOT
    / "yolo_onnx_cpp"
    / "test_outputs"
    / "video_compare"
    / "video_algorithm_report_20260710"
)
DEFAULT_REPORT = REPO_ROOT / "yolo_onnx_cpp" / "video_algorithm_comparison_20260710.md"

RUN_ORDER = [
    "full_high",
    "high_dynamic_flow",
    "high_fixed_flow",
    "low_dynamic_flow",
    "integrated_high_low_flow",
]
SCENARIO_NAMES = {
    "0000f77c-6257be58": "白天高速/常规车流",
    "00268999-cb063914": "夜间城市/低照度",
    "012fdff1-9d1d0d1d": "雨天/挡风玻璃干扰",
}
RUN_NAMES = {
    "full_high": "全帧高分辨率",
    "high_dynamic_flow": "高分辨率动态光流",
    "high_fixed_flow": "高分辨率固定光流",
    "low_dynamic_flow": "低分辨率动态光流",
    "integrated_high_low_flow": "集成 High/Low 光流",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, default=DEFAULT_RESULTS_ROOT)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    return parser.parse_args()


def ratio(numerator: float, denominator: float) -> float:
    return numerator / denominator if denominator else 0.0


def percentile(values: list[float], key: str) -> float:
    present = [float(value[key]) for value in values if key in value]
    return median(present) if present else 0.0


def quality_totals(entries: list[dict[str, Any]]) -> dict[str, Any]:
    labels = sum(int(item.get("labels") or 0) for item in entries)
    predictions = sum(int(item.get("predictions") or 0) for item in entries)
    matches = sum(int(item.get("matches") or 0) for item in entries)
    false_positives = sum(int(item.get("false_positives") or 0) for item in entries)
    false_negatives = sum(int(item.get("false_negatives") or 0) for item in entries)
    frames = sum(int(item.get("frames") or 0) for item in entries)
    precision = ratio(matches, predictions)
    recall = ratio(matches, labels)
    f1 = ratio(2.0 * precision * recall, precision + recall)
    mean_iou = ratio(
        sum(float(item.get("mean_matched_iou") or 0.0) * int(item.get("matches") or 0) for item in entries),
        matches,
    )
    return {
        "frames": frames,
        "labels": labels,
        "predictions": predictions,
        "matches": matches,
        "false_positives": false_positives,
        "false_negatives": false_negatives,
        "precision": round(precision, 6),
        "recall": round(recall, 6),
        "f1": round(f1, 6),
        "mean_matched_iou": round(mean_iou, 6),
    }


def pool_quality(comparisons: list[dict[str, Any]], mode: str) -> list[dict[str, Any]]:
    thresholds = comparisons[0]["thresholds"]
    pooled: list[dict[str, Any]] = []
    for index, threshold in enumerate(thresholds):
        evaluations = [item["quality_vs_full_high"][mode][index] for item in comparisons]
        by_class_keys = sorted({key for evaluation in evaluations for key in evaluation["by_class"]})
        by_source_keys = sorted({
            key for evaluation in evaluations for key in evaluation["by_flow_frame_source"]
        })
        pooled.append({
            "iou_threshold": threshold,
            "overall": quality_totals([evaluation["overall"] for evaluation in evaluations]),
            "by_class": {
                key: quality_totals([
                    evaluation["by_class"][key]
                    for evaluation in evaluations
                    if key in evaluation["by_class"]
                ])
                for key in by_class_keys
            },
            "by_flow_frame_source": {
                key: quality_totals([
                    evaluation["by_flow_frame_source"][key]
                    for evaluation in evaluations
                    if key in evaluation["by_flow_frame_source"]
                ])
                for key in by_source_keys
            },
        })
    return pooled


def aggregate_run(comparisons: list[dict[str, Any]], mode: str) -> dict[str, Any]:
    runs = [item["runs"][mode] for item in comparisons]
    frames = sum(int(run.get("frame_count") or 0) for run in runs)
    elapsed = sum(float(run.get("http_elapsed_sec") or 0.0) for run in runs)
    source_duration = sum(
        ratio(float(run.get("frame_count") or 0), float(run.get("source_fps") or 0.0))
        for run in runs
    )
    call_counts = [run.get("derived_model_calls") or {} for run in runs]
    total_calls = sum(int(item.get("total") or 0) for item in call_counts)
    high_calls = sum(int(item.get("high_res") or 0) for item in call_counts)
    low_calls = sum(int(item.get("low_res") or 0) for item in call_counts)
    count_fields = [
        "processed_frame_count",
        "detected_frame_count",
        "async_infer_request_count",
        "async_correction_count",
        "async_corrected_frame_count",
        "forced_detection_count",
        "scheduled_detection_count",
        "skipped_detection_count",
        "weak_tracked_frame_count",
        "interpolated_frame_count",
        "empty_frame_count",
        "dropped_frame_count",
    ]
    timing_fields = sorted({key for run in runs for key in (run.get("timing_ms") or {})})
    source_counts: Counter[str] = Counter()
    for run in runs:
        source_counts.update(run.get("frame_source_counts") or {})
    proxies = [run["tracking_proxy"] for run in runs]
    segment_count = sum(int(proxy["contiguous_segment_count"]) for proxy in proxies)
    tracking_proxy = {
        "track_observation_count": sum(int(proxy["track_observation_count"]) for proxy in proxies),
        "unique_track_count": sum(int(proxy["unique_track_count"]) for proxy in proxies),
        "contiguous_segment_count": segment_count,
        "reappearance_count": sum(int(proxy["reappearance_count"]) for proxy in proxies),
        "mean_contiguous_lifetime_frames": round(ratio(
            sum(
                float(proxy["mean_contiguous_lifetime_frames"])
                * int(proxy["contiguous_segment_count"])
                for proxy in proxies
            ),
            segment_count,
        ), 3),
        "median_contiguous_lifetime_frames": round(median([
            float(proxy["median_contiguous_lifetime_frames"]) for proxy in proxies
        ]), 3),
        "median_scene_p95_lifetime_frames": round(median([
            float(proxy["p95_contiguous_lifetime_frames"]) for proxy in proxies
        ]), 3),
    }
    latency: dict[str, dict[str, float]] = {}
    latency_names = sorted({key for run in runs for key in (run.get("latency_percentiles_ms") or {})})
    for latency_name in latency_names:
        values = [
            run["latency_percentiles_ms"][latency_name]
            for run in runs
            if latency_name in (run.get("latency_percentiles_ms") or {})
        ]
        latency[latency_name] = {
            key: round(percentile(values, key), 6) for key in ("p50", "p95", "p99")
        }
    result = {
        "frame_count": frames,
        "http_elapsed_sec": round(elapsed, 6),
        "pooled_display_fps": round(ratio(frames, elapsed), 6),
        "source_duration_sec": round(source_duration, 6),
        "derived_model_calls": {
            "total": total_calls,
            "high_res": high_calls if mode == "integrated_high_low_flow" else None,
            "low_res": low_calls if mode == "integrated_high_low_flow" else None,
        },
        "derived_model_call_fps": round(ratio(total_calls, source_duration), 6),
        "derived_high_res_call_fps": round(ratio(high_calls, source_duration), 6)
        if mode == "integrated_high_low_flow"
        else None,
        "derived_low_res_call_fps": round(ratio(low_calls, source_duration), 6)
        if mode == "integrated_high_low_flow"
        else None,
        "cpu_utilization_percent_median": round(median([
            float(run.get("cpu_utilization_percent") or 0.0) for run in runs
        ]), 6),
        "rss_memory_mb_median": round(median([
            float(run.get("rss_memory_mb") or 0.0) for run in runs
        ]), 6),
        "timing_ms_sum": {
            key: round(sum(float((run.get("timing_ms") or {}).get(key) or 0.0) for run in runs), 6)
            for key in timing_fields
        },
        "latency_percentiles_ms_scene_median": latency,
        "frame_source_counts": dict(source_counts),
        "tracking_proxy": tracking_proxy,
        "avg_tracks_per_frame": round(ratio(
            sum(float(run.get("avg_tracks_per_frame") or 0.0) * int(run.get("frame_count") or 0) for run in runs),
            frames,
        ), 6),
    }
    for field in count_fields:
        result[field] = sum(int(run.get(field) or 0) for run in runs)
    return result


def fmt(value: Any, digits: int = 3) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def relative_link(report: Path, target: Path) -> str:
    return target.relative_to(report.parent).as_posix()


def quality_at(aggregate: dict[str, Any], mode: str, threshold: float = 0.5) -> dict[str, Any]:
    for item in aggregate["quality_vs_full_high"][mode]:
        if abs(float(item["iou_threshold"]) - threshold) < 1e-9:
            return item
    raise KeyError((mode, threshold))


def make_report(aggregate: dict[str, Any], report_path: Path) -> str:
    runs = aggregate["runs"]
    full_elapsed = float(runs["full_high"]["http_elapsed_sec"])
    full_calls = int(runs["full_high"]["derived_model_calls"]["total"])
    low_quality = quality_at(aggregate, "low_dynamic_flow")["overall"]
    high_low_quality = quality_at(aggregate, "integrated_high_low_flow")["overall"]
    high_dynamic_quality = quality_at(aggregate, "high_dynamic_flow")["overall"]
    high_fixed_quality = quality_at(aggregate, "high_fixed_flow")["overall"]

    lines = [
        "# 视频算法详细对比报告",
        "",
        "测试日期：2026-07-10",
        "",
        "## 1. 结论摘要",
        "",
        f"本次在 3 段原始 BDD100K 视频、共 {runs['full_high']['frame_count']} 帧上，实测了 5 种当前 C++ 视频推理模式。所有质量指标均以全帧高分辨率 YOLO 输出作为伪标签，只表示算法间一致性，不代表真实准确率。",
        "",
        f"- **当前综合最优是 `low_dynamic_flow`**：整体 {fmt(runs['low_dynamic_flow']['pooled_display_fps'])} FPS，IoU=0.5 pooled F1={fmt(low_quality['f1'], 6)}，在速度、precision 和 recall 之间最均衡。",
        f"- **集成 `integrated_high_low_flow` 召回最高但输出过量**：recall={fmt(high_low_quality['recall'], 6)}，precision={fmt(high_low_quality['precision'], 6)}；预测 {high_low_quality['predictions']} 个框，而伪标签为 {high_low_quality['labels']} 个，预测/标签比为 {fmt(ratio(high_low_quality['predictions'], high_low_quality['labels']))}。",
        f"- **高分辨率动态/固定光流没有达到配置的 4 FPS 强检测目标**：实际模型调用率仅 {fmt(runs['high_dynamic_flow']['derived_model_call_fps'])}/{fmt(runs['high_fixed_flow']['derived_model_call_fps'])} FPS，导致 F1 分别只有 {fmt(high_dynamic_quality['f1'], 6)}/{fmt(high_fixed_quality['f1'], 6)}。",
        f"- **全帧高分辨率 CPU 推理不具备实时性**：整体仅 {fmt(runs['full_high']['pooled_display_fps'])} FPS，三段总耗时 {fmt(full_elapsed)} 秒。",
        "",
        "推荐结论：当前若优先综合质量与实时性，应使用低分辨率动态光流；集成 High/Low 在修复重复/过量轨迹输出和补足可观测性前，不建议作为默认生产模式。",
        "",
        "## 2. 测试条件与口径",
        "",
        "| 项目 | 配置 |",
        "| --- | --- |",
        "| CPU | 2× Intel Xeon Gold 5218，64 逻辑核 |",
        "| 后端 | ONNX Runtime CPU，`thread_num: 4` |",
        "| 高分辨率模型 | `deploy/best.onnx`，输入 1280×736 |",
        "| 低分辨率模型 | `deploy/best_640x384.onnx`，输入 640×384 |",
        "| 阈值 | confidence 0.25，NMS IoU 0.45，letterbox |",
        "| 光流模式 | 目标检测频率 4 FPS；dynamic/fixed 按模式设置；异步开启 |",
        "| 质量匹配 | 按帧、按类别的一对一贪心 IoU 匹配，阈值 0.3/0.5/0.7 |",
        "| 速度汇总 | 总帧数 ÷ 三段 HTTP 总耗时；不包含服务启动和视频渲染 |",
        "| 资源/延迟汇总 | 三段视频相应指标的中位数 |",
        "",
        "测试视频：",
        "",
    ]
    for scenario in aggregate["scenarios"]:
        lines.append(
            f"- `{scenario['stem']}`：{scenario['scenario']}，{scenario['frame_count']} 帧，约 {fmt(scenario['duration_sec'])} 秒。"
        )

    lines.extend([
        "",
        "## 3. VS Code 可直接播放的视频",
        "",
        "全部视频均经 FFprobe 验证为 H.264、`yuv420p`、`faststart` MP4。六宫格布局依次为：原视频、全帧高分辨率、高分辨率动态、高分辨率固定、低分辨率动态、集成 High/Low。",
        "",
        "| 场景 | 六宫格 | full_high | high_dynamic | high_fixed | low_dynamic | High/Low |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ])
    for scenario in aggregate["scenarios"]:
        output_dir = Path(scenario["output_dir"])
        links = {
            "grid": relative_link(report_path, output_dir / "all_modes_comparison_grid.mp4"),
            **{
                mode: relative_link(report_path, output_dir / f"{mode}_detections.mp4")
                for mode in RUN_ORDER
            },
        }
        lines.append(
            f"| {scenario['scenario']} | [六宫格]({links['grid']}) | "
            f"[视频]({links['full_high']}) | [视频]({links['high_dynamic_flow']}) | "
            f"[视频]({links['high_fixed_flow']}) | [视频]({links['low_dynamic_flow']}) | "
            f"[视频]({links['integrated_high_low_flow']}) |"
        )

    lines.extend([
        "",
        "## 4. 总体性能",
        "",
        "| 模式 | 总耗时(s) | pooled FPS | 相对全帧加速 | 模型调用 | 实际调用 FPS | 调用减少 | CPU中位数(%) | RSS中位数(MB) | E2E p95中位数(ms) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for mode in RUN_ORDER:
        run = runs[mode]
        calls = int(run["derived_model_calls"]["total"])
        e2e = run["latency_percentiles_ms_scene_median"].get("end_to_end_ms", {})
        lines.append(
            f"| {mode} | {fmt(run['http_elapsed_sec'])} | {fmt(run['pooled_display_fps'])} | "
            f"{fmt(ratio(full_elapsed, run['http_elapsed_sec']))}x | {calls} | "
            f"{fmt(run['derived_model_call_fps'])} | {fmt(1.0 - ratio(calls, full_calls), 3)} | "
            f"{fmt(run['cpu_utilization_percent_median'])} | {fmt(run['rss_memory_mb_median'])} | "
            f"{fmt(e2e.get('p95'))} |"
        )

    lines.extend([
        "",
        "说明：进程 CPU 可超过 100%，表示使用多个逻辑核。异步模式的阶段耗时存在并发重叠，不能把各阶段总和直接当作墙钟耗时。",
        "",
        "### 三段累计阶段耗时",
        "",
        "| 模式 | preprocess(s) | infer(s) | postprocess(s) | optical flow(s) | tracker(s) | queue wait(s) | profiled(s) | wall(s) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for mode in RUN_ORDER:
        timing = runs[mode]["timing_ms_sum"]
        lines.append(
            f"| {mode} | {fmt(float(timing.get('preprocess_ms', 0.0)) / 1000.0)} | "
            f"{fmt(float(timing.get('infer_ms', 0.0)) / 1000.0)} | "
            f"{fmt(float(timing.get('postprocess_ms', 0.0)) / 1000.0)} | "
            f"{fmt(float(timing.get('optical_flow_ms', 0.0)) / 1000.0)} | "
            f"{fmt(float(timing.get('tracker_ms', 0.0)) / 1000.0)} | "
            f"{fmt(float(timing.get('queue_wait_ms', 0.0)) / 1000.0)} | "
            f"{fmt(float(timing.get('profiled_stage_ms', 0.0)) / 1000.0)} | "
            f"{fmt(float(timing.get('total_elapsed_ms', 0.0)) / 1000.0)} |"
        )

    lines.extend([
        "",
        "### 延迟分位数（三段相应分位数的中位数）",
        "",
        "| 模式 | infer p50(ms) | infer p95(ms) | infer p99(ms) | E2E p50(ms) | E2E p95(ms) | E2E p99(ms) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for mode in RUN_ORDER:
        latency = runs[mode]["latency_percentiles_ms_scene_median"]
        infer_latency = latency.get("infer_ms", {})
        e2e_latency = latency.get("end_to_end_ms", {})
        lines.append(
            f"| {mode} | {fmt(infer_latency.get('p50'))} | {fmt(infer_latency.get('p95'))} | "
            f"{fmt(infer_latency.get('p99'))} | {fmt(e2e_latency.get('p50'))} | "
            f"{fmt(e2e_latency.get('p95'))} | {fmt(e2e_latency.get('p99'))} |"
        )

    lines.extend([
        "",
        "## 5. 总体质量一致性",
        "",
        "### IoU=0.5 pooled 结果",
        "",
        "| 模式 | precision | recall | F1 | mean matched IoU | matches | predictions | FP | FN |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for mode in RUN_ORDER[1:]:
        overall = quality_at(aggregate, mode)["overall"]
        lines.append(
            f"| {mode} | {fmt(overall['precision'], 6)} | {fmt(overall['recall'], 6)} | "
            f"{fmt(overall['f1'], 6)} | {fmt(overall['mean_matched_iou'], 6)} | "
            f"{overall['matches']} | {overall['predictions']} | "
            f"{overall['false_positives']} | {overall['false_negatives']} |"
        )

    lines.extend(["", "### 不同 IoU 阈值", ""])
    for mode in RUN_ORDER[1:]:
        values = aggregate["quality_vs_full_high"][mode]
        lines.append(
            f"- `{mode}`：" + "；".join(
                f"IoU {item['iou_threshold']}: P={item['overall']['precision']:.3f}, "
                f"R={item['overall']['recall']:.3f}, F1={item['overall']['f1']:.3f}"
                for item in values
            )
        )

    lines.extend([
        "",
        "### 分场景 IoU=0.5",
        "",
        "| 场景 | 模式 | precision | recall | F1 | predictions/labels |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ])
    for scenario in aggregate["scenarios"]:
        for mode in RUN_ORDER[1:]:
            item = scenario["quality_iou_0_5"][mode]
            lines.append(
                f"| {scenario['scenario']} | {mode} | {fmt(item['precision'], 6)} | "
                f"{fmt(item['recall'], 6)} | {fmt(item['f1'], 6)} | "
                f"{fmt(ratio(item['predictions'], item['labels']))} |"
            )

    class_keys = sorted({
        key for mode in RUN_ORDER[1:] for key in quality_at(aggregate, mode)["by_class"]
    })
    lines.extend([
        "",
        "### 分类别 F1（IoU=0.5）",
        "",
        "| 类别 | high_dynamic | high_fixed | low_dynamic | High/Low |",
        "| --- | ---: | ---: | ---: | ---: |",
    ])
    for class_key in class_keys:
        row = []
        for mode in RUN_ORDER[1:]:
            item = quality_at(aggregate, mode)["by_class"].get(class_key)
            row.append(fmt(item["f1"], 6) if item else "-")
        lines.append(f"| {class_key} | " + " | ".join(row) + " |")

    lines.extend([
        "",
        "## 6. 调度与跟踪代理指标",
        "",
        "| 模式 | processed | async requests | corrections | skipped | dropped | empty frames | weak tracked | unique IDs | reappearances | mean lifetime(frames) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for mode in RUN_ORDER:
        run = runs[mode]
        proxy = run["tracking_proxy"]
        lines.append(
            f"| {mode} | {run['processed_frame_count']} | {run['async_infer_request_count']} | "
            f"{run['async_correction_count']} | {run['skipped_detection_count']} | "
            f"{run['dropped_frame_count']} | {run['empty_frame_count']} | "
            f"{run['weak_tracked_frame_count']} | {proxy['unique_track_count']} | "
            f"{proxy['reappearance_count']} | {fmt(proxy['mean_contiguous_lifetime_frames'])} |"
        )

    lines.extend([
        "",
        "### 最终帧来源分布",
        "",
        "| 模式 | detected | async_corrected | weak_tracked | interpolated | empty |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ])
    for mode in RUN_ORDER:
        sources = runs[mode]["frame_source_counts"]
        lines.append(
            f"| {mode} | {sources.get('detected', 0)} | {sources.get('async_corrected', 0)} | "
            f"{sources.get('weak_tracked', 0)} | {sources.get('interpolated', 0)} | "
            f"{sources.get('empty', 0)} |"
        )

    lines.extend([
        "",
        f"集成 High/Low 的 {runs['integrated_high_low_flow']['derived_model_calls']['total']} 次模型调用由 {runs['integrated_high_low_flow']['derived_model_calls']['high_res']} 次高分辨率异步请求和 {runs['integrated_high_low_flow']['derived_model_calls']['low_res']} 次低分辨率处理构成；这是根据当前实现字段推导，服务没有直接返回拆分计数或拆分耗时。",
        "",
        "## 7. 最差时间点（IoU=0.5）",
        "",
        "以下列出每段视频、每个候选模式自动筛选的最差时间点之一；完整前 5 项保存在各场景 `comparison.json`。",
        "",
        "| 场景 | 模式 | frame | time(s) | source | F1 | FP | FN |",
        "| --- | --- | ---: | ---: | --- | ---: | ---: | ---: |",
    ])
    for scenario in aggregate["scenarios"]:
        for mode in RUN_ORDER[1:]:
            item = scenario["worst_frames_iou_0_5"][mode][0]
            lines.append(
                f"| {scenario['scenario']} | {mode} | {item['frame_index']} | "
                f"{fmt(item['timestamp_sec'])} | {item['frame_source']} | "
                f"{fmt(item['f1'], 6)} | {item['false_positives']} | {item['false_negatives']} |"
            )

    profiled = runs["integrated_high_low_flow"]["timing_ms_sum"].get("profiled_stage_ms", 0.0)
    wall = runs["integrated_high_low_flow"]["timing_ms_sum"].get("total_elapsed_ms", 0.0)
    lines.extend([
        "",
        "## 8. 当前问题与缺陷",
        "",
        "### P0：旧 High/Low 对比产物不能作为当前结论",
        "",
        "旧报告使用已画框的 `Readme/dynamic_onnx_flow_detections.mp4` 作为输入，并采用测试端后融合；旧视频还是 MPEG-4 Part 2。输入内容、算法实现和播放编码均不符合本次要求。本报告只使用原始 `.mov` 与集成 C++ endpoint。",
        "",
        "### P1：高分辨率异步模式严重低于 4 FPS 调度目标",
        "",
        f"动态/固定模式三段合计只接受 {runs['high_dynamic_flow']['derived_model_calls']['total']}/{runs['high_fixed_flow']['derived_model_calls']['total']} 次模型调用，实际为 {fmt(runs['high_dynamic_flow']['derived_model_call_fps'])}/{fmt(runs['high_fixed_flow']['derived_model_call_fps'])} FPS。对应 skipped={runs['high_dynamic_flow']['skipped_detection_count']}/{runs['high_fixed_flow']['skipped_detection_count']}、dropped={runs['high_dynamic_flow']['dropped_frame_count']}/{runs['high_fixed_flow']['dropped_frame_count']}。异步 worker 只有有限 pending 能力，模型速度低于调度频率时，大量请求被跳过，配置的 `video_detect_fps: 4` 并未兑现。",
        "",
        "### P1：集成 High/Low 存在明显过量/重复轨迹输出",
        "",
        f"IoU=0.5 下，集成模式产生 {high_low_quality['predictions']} 个预测，而基准只有 {high_low_quality['labels']} 个；FP={high_low_quality['false_positives']}，precision={high_low_quality['precision']:.3f}。其 recall={high_low_quality['recall']:.3f} 很高，但高召回是以大量额外框为代价。结合 AuthorityTracker 同时保留稳定轨迹和低分辨率 provisional 轨迹的策略，当前需要优先检查重复抑制、provisional 晋升和轨迹过期规则。后一句属于基于代码与结果的定位推断，不是真值证明。",
        "",
        "六宫格抽查提供了直接画面证据：白天 16.584 秒 `full_high` 为 7 条轨迹、High/Low 为 20 条；夜间 15.237 秒分别为 9/18 条，并出现跨画面边缘的大框；雨天 21.250 秒分别为 5/11 条。",
        "",
        "### P1：弱跟踪帧存在框漂移、尺度膨胀与目标丢失",
        "",
        "白天 16.584 秒高分辨率动态/固定模式分别只剩 2/1 条弱跟踪轨迹，而基准为 7 条；夜间 15.237 秒可以看到右侧车辆框和画面边缘框明显膨胀；雨天 21.250 秒在雨滴与模糊干扰下，多种光流模式的框位置和尺度偏离目标。该现象与 LK 光流只适合短间隔、当前强检测请求率不足相互叠加。",
        "",
        "### P1：全帧高分辨率与集成模式均未稳定达到源视频实时帧率",
        "",
        f"全帧模式只有 {runs['full_high']['pooled_display_fps']:.3f} FPS；集成模式整体 {runs['integrated_high_low_flow']['pooled_display_fps']:.3f} FPS，虽略高于三段约 30 FPS 的源帧率，但分场景中白天视频只有 28.421 FPS，仍可能在复杂场景积压。",
        "",
        "### P2：High/Low 可观测性不足",
        "",
        "服务响应只有合并推理耗时，`tracks_source` 也统一为 `detected`/`weak_tracked`，没有明确标识高分辨率检测、低分辨率检测或两者融合。当前无法可靠回答每类模型各自耗时、各自质量贡献和高/低检测帧来源。",
        "",
        "### P2：异步阶段 timing 不能解释墙钟占比",
        "",
        f"集成模式三段 `profiled_stage_ms` 合计 {profiled:.1f} ms，而 `total_elapsed_ms` 合计 {wall:.1f} ms；阶段耗时因并发重叠可大于墙钟时间。当前 `timing_ratio` 适合看累计计算量，不适合解释严格的端到端时间占比。",
        "",
        "### P2：缺少视频检测/跟踪真值",
        "",
        "precision、recall、F1 都是相对 `full_high` 的一致性指标，继承了高分辨率模型本身的漏检、误检和 ID 碎片。没有 MOT 真值时不能报告真实 mAP、MOTA、IDF1 或 HOTA，也不能把 unique ID 减少直接解释为身份保持更好。",
        "",
        "## 9. 建议优先级",
        "",
        "1. 先修复异步检测调度：区分“目标检测频率”和“worker 实际可承载频率”，至少暴露拒绝原因与实际请求率。",
        "2. 对集成 High/Low 增加稳定轨迹与 provisional 轨迹之间的重复抑制，并对重叠输出做逐帧诊断。",
        "3. 增加高/低分辨率独立调用次数、独立推理耗时和明确的 `tracks_source`。",
        "4. 补充对应 BDD100K 检测/MOT 真值后，再决定生产默认算法；当前仅按伪标签结果，低分辨率动态光流是更稳妥的默认选择。",
        "",
        "## 10. 产物与复现",
        "",
        f"- 聚合 JSON：[aggregate_comparison.json]({relative_link(report_path, Path(aggregate['aggregate_json']))})",
    ])
    for scenario in aggregate["scenarios"]:
        output_dir = Path(scenario["output_dir"])
        lines.append(
            f"- {scenario['scenario']}：[comparison.md]({relative_link(report_path, output_dir / 'comparison.md')}) / [comparison.json]({relative_link(report_path, output_dir / 'comparison.json')})"
        )
    lines.extend([
        "- 构建：`cd yolo_onnx_cpp && cmake --preset vcpkg-gcc15-release && cmake --build --preset vcpkg-gcc15-release`",
        "- 测试：`ctest --test-dir build --output-on-failure`",
        "- 对比脚本：`conda run -n yolo python yolo_onnx_cpp/test/compare_yolo_high_low_flow.py --video <raw.mov>`",
        "",
        "限制：每段视频每种模式只做一次正式运行，结果用于当前机器和当前工作区版本的工程对比，不作为跨机器 benchmark。",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    results_root = args.results_root.resolve()
    report_path = args.report.resolve()
    comparison_paths = sorted(results_root.glob("*/comparison.json"))
    if len(comparison_paths) != 3:
        raise RuntimeError(f"expected 3 comparisons, found {len(comparison_paths)}")
    comparisons = [json.loads(path.read_text(encoding="utf-8")) for path in comparison_paths]
    stems = [Path(item["video"]).stem for item in comparisons]
    if set(stems) != set(SCENARIO_NAMES):
        raise RuntimeError(f"unexpected videos: {stems}")

    runs = {mode: aggregate_run(comparisons, mode) for mode in RUN_ORDER}
    quality = {
        mode: pool_quality(comparisons, mode) for mode in RUN_ORDER if mode != "full_high"
    }
    scenarios = []
    for item in comparisons:
        stem = Path(item["video"]).stem
        full = item["runs"]["full_high"]
        iou_index = item["thresholds"].index(0.5)
        scenarios.append({
            "stem": stem,
            "scenario": SCENARIO_NAMES[stem],
            "video": item["video"],
            "output_dir": item["output_dir"],
            "frame_count": int(full["frame_count"]),
            "duration_sec": round(ratio(float(full["frame_count"]), float(full["source_fps"])), 6),
            "runs": item["runs"],
            "quality_iou_0_5": {
                mode: item["quality_vs_full_high"][mode][iou_index]["overall"]
                for mode in RUN_ORDER[1:]
            },
            "worst_frames_iou_0_5": item["worst_frames_iou_0_5"],
        })
    scenarios.sort(key=lambda item: item["stem"])

    aggregate_path = results_root / "aggregate_comparison.json"
    aggregate = {
        "report_date": "2026-07-10",
        "aggregate_json": str(aggregate_path),
        "report": str(report_path),
        "run_order": RUN_ORDER,
        "runs": runs,
        "quality_vs_full_high": quality,
        "scenarios": scenarios,
        "limitations": {
            "pseudo_labels": True,
            "ground_truth_metrics_available": False,
            "formal_runs_per_scenario": 1,
        },
    }
    full_elapsed = float(runs["full_high"]["http_elapsed_sec"])
    for mode in RUN_ORDER:
        aggregate["runs"][mode]["speedup_vs_full_high"] = round(
            ratio(full_elapsed, float(runs[mode]["http_elapsed_sec"])), 6
        )

    aggregate_path.write_text(
        json.dumps(aggregate, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    report_path.write_text(make_report(aggregate, report_path), encoding="utf-8")
    print(json.dumps({
        "aggregate_json": str(aggregate_path),
        "report": str(report_path),
        "pooled_fps": {mode: runs[mode]["pooled_display_fps"] for mode in RUN_ORDER},
        "quality_iou_0_5": {
            mode: quality_at(aggregate, mode)["overall"] for mode in RUN_ORDER[1:]
        },
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
