#!/usr/bin/env python3
"""Run C++ ONNX Runtime and OpenVINO thread benchmarks for both YOLO models."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import random
import statistics
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


SCRIPT_PATH = Path(__file__).resolve()
CPP_ROOT = SCRIPT_PATH.parents[1]
REPO_ROOT = CPP_ROOT.parent
RESULT_PREFIX = "BENCHMARK_RESULT="
BACKENDS = ("onnx", "openvino")
BACKEND_NAMES = {"onnx": "ONNX Runtime", "openvino": "OpenVINO"}


@dataclass(frozen=True)
class ModelSpec:
    key: str
    display_name: str
    path: Path
    width: int
    height: int


def positive_int(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def non_negative_int(value: str) -> int:
    number = int(value)
    if number < 0:
        raise argparse.ArgumentTypeError("must be a non-negative integer")
    return number


def parse_args() -> argparse.Namespace:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S_utc")
    parser = argparse.ArgumentParser(
        description="Benchmark C++ ONNX Runtime and OpenVINO thread counts for both models."
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=REPO_ROOT / "build-openvino" / "onnx_thread_benchmark",
    )
    parser.add_argument(
        "--large-model",
        type=Path,
        default=CPP_ROOT / "deploy" / "best.onnx",
    )
    parser.add_argument(
        "--small-model",
        type=Path,
        default=CPP_ROOT / "deploy" / "best_640x384.onnx",
    )
    parser.add_argument(
        "--video",
        type=Path,
        default=REPO_ROOT / "Readme" / "dynamic_onnx_flow_detections.mp4",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=REPO_ROOT / "docs" / "test-results" / "onnx_thread_benchmark" / timestamp,
    )
    parser.add_argument(
        "--threads",
        type=positive_int,
        nargs="+",
        default=[1, 2, 4, 8, 16],
    )
    parser.add_argument("--warmup", type=non_negative_int, default=10)
    parser.add_argument("--iterations", type=positive_int, default=50)
    parser.add_argument("--trials", type=positive_int, default=3)
    parser.add_argument("--seed", type=int, default=20260903)
    parser.add_argument("--timeout", type=positive_int, default=600)
    return parser.parse_args()


def checked_path(path: Path, description: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"{description} does not exist: {resolved}")
    return resolved


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_output(command: list[str]) -> str:
    try:
        return subprocess.run(
            command,
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
            timeout=30,
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unavailable"


def percentile(samples: list[float], ratio: float) -> float:
    ordered = sorted(samples)
    if len(ordered) == 1:
        return ordered[0]
    position = ratio * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def sample_summary(samples: list[float]) -> dict[str, float]:
    return {
        "mean_ms": statistics.fmean(samples),
        "median_ms": statistics.median(samples),
        "p95_ms": percentile(samples, 0.95),
        "p99_ms": percentile(samples, 0.99),
        "min_ms": min(samples),
        "max_ms": max(samples),
        "stdev_ms": statistics.stdev(samples) if len(samples) > 1 else 0.0,
    }


def run_job(
    binary: Path,
    model: ModelSpec,
    video: Path,
    backend: str,
    thread_num: int,
    warmup: int,
    iterations: int,
    timeout: int,
) -> dict[str, Any]:
    command = [
        str(binary),
        "--model",
        str(model.path),
        "--video",
        str(video),
        "--backend",
        backend,
        "--input-width",
        str(model.width),
        "--input-height",
        str(model.height),
        "--threads",
        str(thread_num),
        "--warmup",
        str(warmup),
        "--iterations",
        str(iterations),
    ]
    completed = subprocess.run(
        command,
        cwd=CPP_ROOT,
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"benchmark exited with {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )

    for line in reversed(completed.stdout.splitlines()):
        if line.startswith(RESULT_PREFIX):
            return json.loads(line[len(RESULT_PREFIX) :])
    raise RuntimeError(
        f"benchmark result marker not found\n"
        f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
    )


def aggregate_results(
    raw_results: list[dict[str, Any]], models: list[ModelSpec], threads: list[int]
) -> list[dict[str, Any]]:
    aggregates: list[dict[str, Any]] = []
    for model in models:
        model_results = [item for item in raw_results if item["model_key"] == model.key]
        for backend in BACKENDS:
            backend_results = [
                item for item in model_results if item["backend"] == backend
            ]
            baseline_samples = [
                float(sample)
                for item in backend_results
                if item["thread_num"] == threads[0]
                for sample in item["inference_ms_samples"]
            ]
            baseline_mean = statistics.fmean(baseline_samples)

            for thread_num in threads:
                matches = [
                    item for item in backend_results if item["thread_num"] == thread_num
                ]
                inference_samples = [
                    float(sample)
                    for item in matches
                    for sample in item["inference_ms_samples"]
                ]
                pipeline_samples = [
                    float(sample)
                    for item in matches
                    for sample in item["pipeline_ms_samples"]
                ]
                inference = sample_summary(inference_samples)
                pipeline = sample_summary(pipeline_samples)
                inference_mean = inference["mean_ms"]
                aggregates.append(
                    {
                        "model_key": model.key,
                        "model_name": model.display_name,
                        "model_path": str(model.path),
                        "backend": backend,
                        "input_width": model.width,
                        "input_height": model.height,
                        "thread_num": thread_num,
                        "trials": len(matches),
                        "sample_count": len(inference_samples),
                        "inference": inference,
                        "pipeline": pipeline,
                        "throughput_fps": 1000.0 / inference_mean,
                        "speedup_vs_baseline": baseline_mean / inference_mean,
                        "cpu_utilization_percent": statistics.fmean(
                            float(item["cpu_utilization_percent"]) for item in matches
                        ),
                        "rss_memory_mb": statistics.fmean(
                            float(item["rss_memory_mb"]) for item in matches
                        ),
                        "model_load_mean_ms": statistics.fmean(
                            float(item["model_load_ms"]) for item in matches
                        ),
                    }
                )
    return aggregates


def write_csv(path: Path, aggregates: list[dict[str, Any]]) -> None:
    fields = [
        "model_key",
        "model_name",
        "model_path",
        "backend",
        "input_width",
        "input_height",
        "thread_num",
        "trials",
        "sample_count",
        "inference_mean_ms",
        "inference_median_ms",
        "inference_p95_ms",
        "inference_p99_ms",
        "inference_min_ms",
        "inference_max_ms",
        "inference_stdev_ms",
        "pipeline_mean_ms",
        "throughput_fps",
        "speedup_vs_baseline",
        "cpu_utilization_percent",
        "rss_memory_mb",
        "model_load_mean_ms",
    ]
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(
            file, fieldnames=fields, lineterminator="\n"
        )
        writer.writeheader()
        for item in aggregates:
            inference = item["inference"]
            writer.writerow(
                {
                    "model_key": item["model_key"],
                    "model_name": item["model_name"],
                    "model_path": item["model_path"],
                    "backend": item["backend"],
                    "input_width": item["input_width"],
                    "input_height": item["input_height"],
                    "thread_num": item["thread_num"],
                    "trials": item["trials"],
                    "sample_count": item["sample_count"],
                    "inference_mean_ms": inference["mean_ms"],
                    "inference_median_ms": inference["median_ms"],
                    "inference_p95_ms": inference["p95_ms"],
                    "inference_p99_ms": inference["p99_ms"],
                    "inference_min_ms": inference["min_ms"],
                    "inference_max_ms": inference["max_ms"],
                    "inference_stdev_ms": inference["stdev_ms"],
                    "pipeline_mean_ms": item["pipeline"]["mean_ms"],
                    "throughput_fps": item["throughput_fps"],
                    "speedup_vs_baseline": item["speedup_vs_baseline"],
                    "cpu_utilization_percent": item["cpu_utilization_percent"],
                    "rss_memory_mb": item["rss_memory_mb"],
                    "model_load_mean_ms": item["model_load_mean_ms"],
                }
            )


def combined_report_markdown(
    models: list[ModelSpec],
    aggregates: list[dict[str, Any]],
    environment: dict[str, Any],
    args: argparse.Namespace,
) -> str:
    best_rows: list[tuple[ModelSpec, dict[str, Any], dict[str, Any]]] = []
    for model in models:
        model_rows = [item for item in aggregates if item["model_key"] == model.key]
        ort_best = min(
            (item for item in model_rows if item["backend"] == "onnx"),
            key=lambda item: item["inference"]["mean_ms"],
        )
        openvino_best = min(
            (item for item in model_rows if item["backend"] == "openvino"),
            key=lambda item: item["inference"]["mean_ms"],
        )
        best_rows.append((model, ort_best, openvino_best))

    lines = [
        "# C++ ONNX Runtime 与 OpenVINO 线程性能对比报告",
        "",
        "## 结论",
        "",
    ]
    for model, ort_best, openvino_best in best_rows:
        winner = min(
            (ort_best, openvino_best),
            key=lambda item: item["inference"]["mean_ms"],
        )
        other = openvino_best if winner is ort_best else ort_best
        backend_gain = other["inference"]["mean_ms"] / winner["inference"]["mean_ms"]
        latency_reduction = (
            1.0 - winner["inference"]["mean_ms"] / other["inference"]["mean_ms"]
        ) * 100.0
        lines.append(
            f"- {model.display_name}：{BACKEND_NAMES[winner['backend']]} "
            f"{winner['thread_num']} 线程最快，平均 {winner['inference']['mean_ms']:.2f} ms，"
            f"P95 {winner['inference']['p95_ms']:.2f} ms，"
            f"等效 {winner['throughput_fps']:.2f} FPS。相对另一后端各自最优配置，"
            f"速度为 {backend_gain:.2f}x、延迟降低 {latency_reduction:.1f}%。"
        )
    for model in models:
        rows = [
            item for item in aggregates
            if item["model_key"] == model.key and item["backend"] == "openvino"
        ]
        eight_threads = next(
            (item for item in rows if item["thread_num"] == 8),
            None,
        )
        sixteen_threads = next(
            (item for item in rows if item["thread_num"] == 16),
            None,
        )
        if (
            eight_threads is not None
            and sixteen_threads is not None
            and eight_threads["inference"]["p95_ms"]
            < sixteen_threads["inference"]["p95_ms"]
        ):
            lines.append(
                f"- {model.display_name}尾延迟：OpenVINO 8 线程 P95 "
                f"{eight_threads['inference']['p95_ms']:.2f} ms，低于 16 线程的 "
                f"{sixteen_threads['inference']['p95_ms']:.2f} ms；需要稳定尾延迟时，"
                "8 线程是更保守的候选。"
            )
    lines.extend(
        [
            "",
            "本结论针对单实例、batch=1、同步 CPU 推理。当前 High/Low 模式可能并行运行两个 Session，并发请求也会共享 CPU，因此生产配置还需结合并发吞吐测试，不能仅按单实例最低延迟选择线程数。",
            "",
            "### 最优配置汇总",
            "",
            "| 模型 | ONNX Runtime 最优 | OpenVINO 最优 | 全局最快 | 最快后端优势 |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    for model, ort_best, openvino_best in best_rows:
        winner = min(
            (ort_best, openvino_best),
            key=lambda item: item["inference"]["mean_ms"],
        )
        other = openvino_best if winner is ort_best else ort_best
        lines.append(
            f"| {model.display_name} | {ort_best['inference']['mean_ms']:.2f} ms "
            f"({ort_best['thread_num']} 线程) | {openvino_best['inference']['mean_ms']:.2f} ms "
            f"({openvino_best['thread_num']} 线程) | {BACKEND_NAMES[winner['backend']]} "
            f"{winner['thread_num']} 线程 | "
            f"{other['inference']['mean_ms'] / winner['inference']['mean_ms']:.2f}x |"
        )

    lines.extend(
        [
            "",
            "### 当前 thread_num=4 的同线程对比",
            "",
            "| 模型 | ONNX Runtime (ms) | OpenVINO (ms) | 更快后端 | 加速比 |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    for model in models:
        rows = [item for item in aggregates if item["model_key"] == model.key]
        ort = next(
            item for item in rows
            if item["backend"] == "onnx" and item["thread_num"] == 4
        )
        openvino = next(
            item for item in rows
            if item["backend"] == "openvino" and item["thread_num"] == 4
        )
        winner = min((ort, openvino), key=lambda item: item["inference"]["mean_ms"])
        other = openvino if winner is ort else ort
        lines.append(
            f"| {model.display_name} | {ort['inference']['mean_ms']:.2f} | "
            f"{openvino['inference']['mean_ms']:.2f} | {BACKEND_NAMES[winner['backend']]} | "
            f"{other['inference']['mean_ms'] / winner['inference']['mean_ms']:.2f}x |"
        )

    lines.extend(
        [
            "",
            "## 测试方法",
            "",
            "被测路径全部是 C++：OpenCV 读取和预处理首帧，YoloEngine 创建后端并执行推理，decode 与 NMS 也使用项目 C++ 实现。Python 脚本只负责启动隔离进程、交错任务顺序和生成 JSON/CSV/Markdown，不加载模型，也不进入计时区间。",
            "",
            "两个后端直接读取完全相同的 ONNX 文件，不使用 Python Runtime，也不使用预先转换的 OpenVINO IR。thread_num 对 ONNX Runtime 映射为 SetIntraOpNumThreads，对 OpenVINO 映射为 ov::inference_num_threads。",
            "",
            f"每个“模型 × 后端 × 线程数”组合都使用独立进程，先预热 {args.warmup} 次，再测量 {args.iterations} 次，共 {args.trials} 轮；每个组合 {args.iterations * args.trials} 个正式样本，总计 {len(models) * len(BACKENDS) * len(args.threads) * args.iterations * args.trials} 个样本。任务使用固定随机种子 {args.seed} 交错执行。未设置 CPU 亲和性或 NUMA 绑核，输入帧只预处理一次，模型加载与预热均不计入结果。",
            "",
            "纯后端推理分别统计 Ort::Session::Run() 与 ov::InferRequest::infer()；完整 infer 还包含输入 Tensor 准备、输出 shape、decode 和 NMS。CPU 利用率为单进程口径，100% 约等于占满一个逻辑 CPU。",
            "",
            "## 测试对象与环境",
            "",
            f"- 测试视频：{environment['video_path']}（首帧固定复用）",
            f"- ONNX Runtime：{environment['onnxruntime_version']}",
            f"- OpenVINO：{environment['openvino_version']}",
        ]
    )
    for model in models:
        artifact = environment["artifacts"][model.key]
        lines.append(
            f"- {model.display_name}：{model.path}，"
            f"{artifact['size_bytes'] / (1024 * 1024):.2f} MiB，"
            f"输入 [1, 3, {model.height}, {model.width}]，SHA-256 {artifact['sha256']}"
        )
    lines.extend(
        [
            f"- 时间：{environment['timestamp_utc']}",
            f"- 主机：{environment['platform']}",
            f"- CPU：{environment['cpu_model']}",
            f"- 逻辑 CPU：{environment['logical_cpu_count']}；物理核心：{environment['physical_core_count']}",
            f"- 仓库提交：{environment['git_commit']}",
            f"- 工作区状态：{environment['git_status_summary']}",
        ]
    )

    for model in models:
        lines.extend(
            [
                "",
                f"## {model.display_name}详细结果",
                "",
                "| 后端 | 线程 | 纯后端均值 (ms) | 中位数 (ms) | P95 (ms) | 标准差 (ms) | 等效 FPS | 相对本后端 1 线程 | 完整 infer (ms) | CPU |",
                "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
            ]
        )
        rows = [item for item in aggregates if item["model_key"] == model.key]
        for item in rows:
            inference = item["inference"]
            lines.append(
                f"| {BACKEND_NAMES[item['backend']]} | {item['thread_num']} | "
                f"{inference['mean_ms']:.2f} | {inference['median_ms']:.2f} | "
                f"{inference['p95_ms']:.2f} | {inference['stdev_ms']:.2f} | "
                f"{item['throughput_fps']:.2f} | {item['speedup_vs_baseline']:.2f}x | "
                f"{item['pipeline']['mean_ms']:.2f} | "
                f"{item['cpu_utilization_percent']:.1f}% |"
            )

    lines.extend(
        [
            "",
            "完整逐次样本见同目录 raw_results.json，汇总数据见 results.csv，环境元数据见 environment.json。",
            "",
        ]
    )
    return "\n".join(lines)


def cpu_details() -> tuple[str, int]:
    model_name = "unknown"
    physical_cores: set[tuple[str, str]] = set()
    physical_id = ""
    core_id = ""
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if not line.strip():
                if physical_id or core_id:
                    physical_cores.add((physical_id, core_id))
                physical_id = ""
                core_id = ""
                continue
            key, _, value = line.partition(":")
            key = key.strip()
            value = value.strip()
            if key == "model name" and model_name == "unknown":
                model_name = value
            elif key == "physical id":
                physical_id = value
            elif key == "core id":
                core_id = value
        if physical_id or core_id:
            physical_cores.add((physical_id, core_id))
    except OSError:
        pass
    return model_name, len(physical_cores)


def main() -> int:
    args = parse_args()
    args.binary = checked_path(args.binary, "benchmark binary")
    args.large_model = checked_path(args.large_model, "large model")
    args.small_model = checked_path(args.small_model, "small model")
    args.video = checked_path(args.video, "test video")
    args.threads = list(dict.fromkeys(args.threads))
    args.output_dir = args.output_dir.expanduser().resolve()
    args.output_dir.mkdir(parents=True, exist_ok=False)

    models = [
        ModelSpec("large", "大尺寸模型（1280×736）", args.large_model, 1280, 736),
        ModelSpec("small", "小尺寸模型（640×384）", args.small_model, 640, 384),
    ]
    jobs = [
        (trial, model, backend, thread_num)
        for trial in range(1, args.trials + 1)
        for model in models
        for backend in BACKENDS
        for thread_num in args.threads
    ]
    random.Random(args.seed).shuffle(jobs)

    raw_results: list[dict[str, Any]] = []
    for order, (trial, model, backend, thread_num) in enumerate(jobs, start=1):
        print(
            f"[{order:02d}/{len(jobs):02d}] {model.key}/{backend}: "
            f"threads={thread_num}, trial={trial}",
            flush=True,
        )
        result = run_job(
            args.binary,
            model,
            args.video,
            backend,
            thread_num,
            args.warmup,
            args.iterations,
            args.timeout,
        )
        result["model_key"] = model.key
        result["model_name"] = model.display_name
        result["trial"] = trial
        result["execution_order"] = order
        raw_results.append(result)

    aggregates = aggregate_results(raw_results, models, args.threads)
    cpu_model, physical_core_count = cpu_details()
    git_status = command_output(["git", "status", "--short"])
    environment = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "cpu_model": cpu_model,
        "logical_cpu_count": os.cpu_count(),
        "physical_core_count": physical_core_count,
        "onnxruntime_version": raw_results[0]["onnxruntime_version"],
        "openvino_version": raw_results[0]["openvino_version"],
        "git_commit": command_output(["git", "rev-parse", "HEAD"]),
        "git_status_summary": "clean" if not git_status else git_status.replace("\n", "; "),
        "binary_path": str(args.binary),
        "video_path": str(args.video),
        "video_sha256": file_sha256(args.video),
        "parameters": {
            "backends": list(BACKENDS),
            "threads": args.threads,
            "warmup": args.warmup,
            "iterations": args.iterations,
            "trials": args.trials,
            "seed": args.seed,
        },
        "artifacts": {
            model.key: {
                "path": str(model.path),
                "size_bytes": model.path.stat().st_size,
                "sha256": file_sha256(model.path),
            }
            for model in models
        },
    }

    (args.output_dir / "raw_results.json").write_text(
        json.dumps(raw_results, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (args.output_dir / "environment.json").write_text(
        json.dumps(environment, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_csv(args.output_dir / "results.csv", aggregates)
    (args.output_dir / "combined_report.md").write_text(
        combined_report_markdown(models, aggregates, environment, args),
        encoding="utf-8",
    )

    print(f"Reports written to: {args.output_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"run_onnx_thread_benchmark: {error}", file=sys.stderr)
        raise SystemExit(1)
