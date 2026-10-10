"""Serial matched FP32/INT8 High+LK stage and CPU measurements."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import statistics
import subprocess
from pathlib import Path

STAGES = {"Decode": "video_decode_ms", "Preprocess": "preprocess_ms", "YOLO": "infer_ms",
          "LK Flow": "lk_flow_ms", "ByteTrack": "byte_track_ms", "Postprocess": "postprocess_ms"}


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def stats(values: list[float]) -> dict:
    ordered = sorted(values)
    position = (len(ordered) - 1) * 0.95
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return {"n": len(values), "mean_ms": statistics.mean(values), "total_ms": sum(values),
            "p95_ms": ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)}


def summarize(records: list[dict]) -> dict:
    videos = [record["data"]["videos"][record["video"]] for record in records]
    frame_count = sum(video["frame_count"] for video in videos)
    wall_ms = sum(video["elapsed_ms"] for video in videos)
    cpu_wall_ms = sum(video["cpu_wall_ms"] for video in videos)
    cpu_seconds = sum(video["cpu_seconds"] for video in videos)
    stages = {}
    for stage, key in STAGES.items():
        values = [value for video in videos for value in video[key]]
        stages[stage] = stats(values)
        stages[stage]["amortized_ms_per_frame"] = sum(values) / frame_count
        stages[stage]["wall_percent"] = 100 * sum(values) / wall_ms
    other_ms = wall_ms - sum(stage["total_ms"] for stage in stages.values())
    if other_ms < 0:
        raise ValueError("Stage measurements overlap or exceed wall time")
    return {"frame_count": frame_count, "model_calls": sum(v["model_calls"] for v in videos),
            "wall_ms": wall_ms, "amortized_wall_ms": wall_ms / frame_count,
            "throughput_fps": 1000 * frame_count / wall_ms, "stages": stages,
            "other_ms": other_ms, "other_ms_per_frame": other_ms / frame_count,
            "other_wall_percent": 100 * other_ms / wall_ms,
            "cpu_seconds": cpu_seconds, "cpu_wall_ms": cpu_wall_ms,
            "cpu_process_percent": 100000 * cpu_seconds / cpu_wall_ms,
            "cpu_host_percent": 100000 * cpu_seconds / cpu_wall_ms / records[0]["data"]["logical_cpus"],
            "equivalent_busy_cores": 1000 * cpu_seconds / cpu_wall_ms,
            "cpu_ms_per_frame": 1000 * cpu_seconds / frame_count,
            "logical_cpus": records[0]["data"]["logical_cpus"],
            "affinity_cpus": records[0]["data"]["affinity_cpus"],
            "rss_mean_mib": statistics.mean(statistics.mean(v["rss_samples_mib"]) for v in videos)}


def write_report(summary: dict, output: Path) -> None:
    fp, quant = summary["models"]["fp32"], summary["models"]["int8"]
    rows = ["# High FP32 / INT8 + LK：六阶段耗时与 CPU 利用率", "",
            "计时边界补全后重新实测；所有数值来自本次运行，不与旧结果拼接。", "",
            "## 条件与验证", "",
            "- 与上一轮相同的两个模型、三个完整本地视频；每组 3 轮，交替执行、每个视频独立进程。",
            "- CPU OpenVINO，输入 1280×736，模型 8 线程、OpenCV 4 线程、单请求、同步固定 stride=8；预热 10 次。",
            f"- 每组 {fp['frame_count']:,} 源帧、{fp['model_calls']:,} 次检测调用；机器 {fp['logical_cpus']} 个逻辑 CPU，进程亲和性允许 {fp['affinity_cpus']} 个。",
            "- 全部输出帧的类别、坐标、分数、ID、检测帧位置与旧实验逐项一致，计时改动未改变结果。原精度结果仍适用。",
            "- 模型加载、预热、JSON 输出不计入表内；RSS 采样线程包含在运行期进程 CPU 时间内。", "",
            "## 单次阶段耗时", "",
            "各阶段调用频率不同，不能直接把这一表的均值相加作为每帧耗时。", "",
            "| 阶段 | FP32 均值 ms | INT8 均值 ms | FP32 P95 ms | INT8 P95 ms | 每组采样数 |",
            "|---|---:|---:|---:|---:|---:|"]
    for stage in STAGES:
        a, b = fp["stages"][stage], quant["stages"][stage]
        rows.append(f"| {stage} | {a['mean_ms']:.4f} | {b['mean_ms']:.4f} | {a['p95_ms']:.4f} | {b['p95_ms']:.4f} | {a['n']:,} |")
    rows += ["", "## 按源帧摊销与端到端占比", "",
             "将所有阶段的累计墙钟耗时除以全部源帧数，可与整条管线比较。", "",
             "| 阶段 | FP32 ms/源帧 | INT8 ms/源帧 | FP32 占比 | INT8 占比 |",
             "|---|---:|---:|---:|---:|"]
    for stage in STAGES:
        a, b = fp["stages"][stage], quant["stages"][stage]
        rows.append(f"| {stage} | {a['amortized_ms_per_frame']:.4f} | {b['amortized_ms_per_frame']:.4f} | {a['wall_percent']:.2f}% | {b['wall_percent']:.2f}% |")
    rows += [f"| Other | {fp['other_ms_per_frame']:.4f} | {quant['other_ms_per_frame']:.4f} | {fp['other_wall_percent']:.2f}% | {quant['other_wall_percent']:.2f}% |",
             f"| 合计 | {fp['amortized_wall_ms']:.4f} | {quant['amortized_wall_ms']:.4f} | 100% | 100% |", "",
             f"端到端吞吐：FP32 **{fp['throughput_fps']:.2f} FPS**，INT8 **{quant['throughput_fps']:.2f} FPS**，加速 **{quant['throughput_fps']/fp['throughput_fps']:.3f}×**。", "",
             "## CPU 利用率", "",
             "进程利用率 =（用户态 CPU 秒 + 内核态 CPU 秒）/ 墙钟秒 × 100%，100% 约等于一个核持续忙碌。整机归一化利用率为进程利用率 / 逻辑 CPU 数，并非整机所有进程的总负载。按所有视频 CPU 时间与墙钟时间的总和计算。", "",
             "| 指标 | FP32 | INT8 |", "|---|---:|---:|",
             f"| 运行期平均进程 CPU | {fp['cpu_process_percent']:.2f}% | {quant['cpu_process_percent']:.2f}% |",
             f"| 占整机逻辑 CPU 容量 | {fp['cpu_host_percent']:.2f}% | {quant['cpu_host_percent']:.2f}% |",
             f"| 等效持续忙碌核数 | {fp['equivalent_busy_cores']:.2f} | {quant['equivalent_busy_cores']:.2f} |",
             f"| CPU 消耗 ms/源帧 | {fp['cpu_ms_per_frame']:.2f} | {quant['cpu_ms_per_frame']:.2f} |",
             f"| 三轮累计 CPU 秒 | {fp['cpu_seconds']:.2f} | {quant['cpu_seconds']:.2f} |",
             f"| 运行期平均 RSS MiB | {fp['rss_mean_mib']:.2f} | {quant['rss_mean_mib']:.2f} |", "",
             "CPU 利用率是整个进程的平均值，包含 OpenVINO、OpenCV、视频解码器及采样线程；阶段表记录墙钟耗时，不是逐算子的 CPU 时间。", "",
             "## 每轮结果", "", "| 模型/轮 | 整轮墙钟秒 | YOLO 均值 ms | 进程 CPU | CPU ms/源帧 |",
             "|---|---:|---:|---:|---:|"]
    for name in ("fp32", "int8"):
        for index, item in enumerate(summary["rounds"][name], 1):
            rows.append(f"| {name} / {index} | {item['wall_ms']/1000:.3f} | {item['stages']['YOLO']['mean_ms']:.3f} | {item['cpu_process_percent']:.2f}% | {item['cpu_ms_per_frame']:.2f} |")
    rows += ["", "## 计时边界", "",
             "- **Decode**：成功的 `VideoCapture::read`，包含读取、视频解码和 BGR 帧交付等待。不是模型输出解码，也不等同于解码器后台线程累计 CPU 时间。",
             "- **Preprocess**：检测帧的 `preprocessImageMat`，包括 letterbox/resize、颜色转换、归一化、布局转换。",
             "- **YOLO**：后端模型推理调用；输入张量准备等计入 Other。模型图内的 TopK 等运算仍属于 YOLO。",
             "- **LK Flow**：整个 `prepareFrame`，包含灰度转换、特征点/金字塔、LK 光流与全局运动估计；首帧无历史轨迹时也保留一次样本。",
             "- **ByteTrack**：`applyFlow` 和 `applyDetections`，包括跟踪更新及必要的 StreamProcessor 封装；源帧每帧一次光流更新，检测帧额外一次检测关联。",
             "- **Postprocess**：模型输出形状解析、检测框解析/坐标恢复、外部 class-aware NMS（IoU=0.45）。本模型输出 TopK 300×6，模型图内没有 NMS 算子。",
             "- **Other**：视频打开/末次 EOF 读取、输入张量准备、结果拷贝与存储、速度和轨迹质量计算、缓存提交、统计等未列入六阶段的工作。六阶段彼此不重叠，Other 显式保留。", "",
             "## 结果解读", ""]
    biggest = max(quant["stages"], key=lambda stage: quant["stages"][stage]["total_ms"])
    rows += [f"INT8 下累计耗时最多的阶段是 **{biggest}**（{quant['stages'][biggest]['wall_percent']:.2f}%）。后续性能优化应按源帧摊销占比选目标。",
             f"本轮 YOLO 加速 {fp['stages']['YOLO']['mean_ms']/quant['stages']['YOLO']['mean_ms']:.3f}×；Decode、LK、跟踪等仍在原有 CPU 路径上，因此端到端加速低于模型加速。",
             "不同轮次受线程调度、CPU 频率与机器其他任务影响；请结合每轮数据和 P95 解读差异。本次完善测量，没有修改检测/跟踪算法。", "",
             "## 产物与复现", "",
             "`measured/*.json` 保存逐阶段原始样本、CPU 时间和逐帧输出；`summary.json`、`metrics.csv`、`cpu.csv` 为汇总。`instrumented/instrumentation.diff` 与 `provenance.json` 记录插桩位置和源码哈希。",
             "运行 `bash reproduce.sh NEW_OUTPUT_DIRECTORY`，使用原实验中既有的两个模型，生成新结果；拒绝覆盖已有输出。", ""]
    with output.open("x") as stream:
        stream.write("\n".join(rows))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    out = args.output_dir / "measured"
    out.mkdir()
    models = json.loads((args.baseline / "models_u8s8/quantization.json").read_text())
    manifest = json.loads((args.repo / "docs/test-results/bdd100k_long_tail/gt_3videos_stride6_20260911/manifest.json").read_text())
    for name in ("fp32", "int8"):
        if digest(Path(models[name]["path"])) != models[name]["sha256"]:
            raise ValueError("Model hash changed")
    for meta in manifest["videos"].values():
        if digest(Path(meta["video"]["path"])) != meta["video"]["sha256"]:
            raise ValueError("Video hash changed")
    env = dict(os.environ, CUDA_VISIBLE_DEVICES="4,5")
    env["LD_LIBRARY_PATH"] = "/root/vcpkg/.toolchains/gcc15/lib:" + env.get("LD_LIBRARY_PATH", "")
    records = {"fp32": [], "int8": []}
    commands = []
    for round_index in range(1, 4):
        order = ("fp32", "int8") if round_index % 2 else ("int8", "fp32")
        for video_name, meta in sorted(manifest["videos"].items()):
            for name in order:
                stem = f"high_lk_{name}_r{round_index}_{video_name}"
                output = out / f"{stem}.json"
                command = [str(args.runner.resolve()), models[name]["path"], str(output.resolve()),
                           "high_lk", "10", meta["video"]["path"]]
                commands.append(command)
                print(f"RUN {stem}", flush=True)
                with (out / f"{stem}.log").open("x") as log:
                    subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
                data = json.loads(output.read_text())
                video = data["videos"][video_name]
                previous = json.loads((args.baseline / "measured" / f"{stem}.json").read_text())["videos"][video_name]
                if video["frames"] != previous["frames"]:
                    raise ValueError(f"Prediction changed: {stem}")
                count = meta["video"]["decoded_frame_count"]
                calls = len(range(0, count, 8))
                if video["frame_count"] != count or video["model_calls"] != calls or video["stride_mode"] != "sync_fixed":
                    raise ValueError("Detection policy changed")
                expected = {"Decode": count, "LK Flow": count, "ByteTrack": count + calls,
                            "Preprocess": calls, "YOLO": calls, "Postprocess": calls}
                for stage, key in STAGES.items():
                    if len(video[key]) != expected[stage] or any(value < 0 for value in video[key]):
                        raise ValueError(f"Invalid samples: {stage}")
                if video["cpu_seconds"] <= 0 or video["cpu_wall_ms"] <= 0:
                    raise ValueError("Invalid CPU measurement")
                records[name].append({"round": round_index, "video": video_name, "data": data})
                print(f"PASS {stem}: frames identical; CPU={video['cpu_process_percent']:.1f}%", flush=True)
    summary = {"models": {name: summarize(items) for name, items in records.items()},
               "rounds": {name: [summarize([r for r in items if r['round'] == index]) for index in range(1, 4)]
                          for name, items in records.items()},
               "model_metadata": {name: models[name] for name in ("fp32", "int8")},
               "runner_sha256": digest(args.runner), "prediction_parity": "all 18 outputs equal baseline",
               "commands": commands}
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    with (args.output_dir / "metrics.csv").open("x", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["model", "stage", "samples", "mean_ms", "p95_ms", "ms_per_source_frame", "wall_percent"])
        for name, item in summary["models"].items():
            for stage, value in item["stages"].items():
                writer.writerow([name, stage, value["n"], value["mean_ms"], value["p95_ms"], value["amortized_ms_per_frame"], value["wall_percent"]])
    with (args.output_dir / "cpu.csv").open("x", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["model", "round", "video", "wall_ms", "cpu_seconds", "cpu_process_percent", "logical_cpus"])
        for name, items in records.items():
            for record in items:
                video = record["data"]["videos"][record["video"]]
                writer.writerow([name, record["round"], record["video"], video["cpu_wall_ms"], video["cpu_seconds"], video["cpu_process_percent"], record["data"]["logical_cpus"]])
    write_report(summary, args.output_dir / "report.md")
    print(f"COMPLETE {args.output_dir / 'report.md'}", flush=True)


if __name__ == "__main__":
    main()
