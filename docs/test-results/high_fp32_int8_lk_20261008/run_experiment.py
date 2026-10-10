"""Run serial, alternating FP32/INT8 High+LK pairs and evaluate labeled output."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo


def stats(values: list[float]) -> dict:
    import numpy as np
    if not values:
        raise ValueError("Missing samples")
    return {"n": len(values), "mean": statistics.mean(values), "median": statistics.median(values),
            "p95": float(np.percentile(values, 95)), "min": min(values), "max": max(values)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--models", type=Path, required=True, help="quantization.json")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--classes", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.repo / "yolo_onnx_cpp/tools"))
    import evaluate_bdd100k_openvino_quantization as evaluation
    from evaluate_bdd100k_tracking import ObjectRecord, canonical_class, sha256_file
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    model_info = json.loads(args.models.read_text())
    labels, crowds, strides, manifest = evaluation.load_ground_truth(args.manifest)
    classes = evaluation.load_classes(args.classes)
    videos = {name: Path(meta["video"]["path"]) for name, meta in manifest["videos"].items()}
    for name, path in videos.items():
        if sha256_file(path) != manifest["videos"][name]["video"]["sha256"]:
            raise ValueError(f"Source video changed: {name}")
    for name in ("fp32", "int8"):
        if sha256_file(Path(model_info[name]["path"])) != model_info[name]["sha256"]:
            raise ValueError(f"Model changed: {name}")
    inputs = {"manifest_path": str(args.manifest.resolve()), "manifest_sha256": sha256_file(args.manifest),
              "classes_path": str(args.classes.resolve()), "classes_sha256": sha256_file(args.classes),
              "videos": manifest["videos"], "label_frames": sum(len(v) for v in labels.values()),
              "ground_truth_boxes": sum(len(v) for video in labels.values() for v in video.values())}
    env = dict(os.environ)
    env["CUDA_VISIBLE_DEVICES"] = "4,5"
    # This is only a child process environment, never a system configuration change.
    toolchain_lib = "/root/vcpkg/.toolchains/gcc15/lib"
    env["LD_LIBRARY_PATH"] = toolchain_lib + ":" + env.get("LD_LIBRARY_PATH", "")
    records: dict[str, list[dict]] = {"fp32": [], "int8": []}
    commands = []

    def run(name: str, mode: str, round_index: int, video_name: str) -> dict:
        stem = f"{mode}_{name}_r{round_index}_{video_name}"
        output = out / f"{stem}.json"
        cmd = [str(args.runner.resolve()), model_info[name]["path"], str(output), mode, "10", str(videos[video_name])]
        print(f"RUN {mode} {name} round={round_index} video={video_name}", flush=True)
        commands.append(cmd)
        with (out / f"{stem}.log").open("x") as log:
            completed = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
        if completed.returncode:
            raise RuntimeError(f"Runner failed ({completed.returncode}); see {stem}.log")
        record = json.loads(output.read_text())
        record["round"] = round_index
        record["output_path"] = str(output)
        record["video_name"] = video_name
        video = record["videos"][video_name]
        expected_frames = manifest["videos"][video_name]["video"]["decoded_frame_count"]
        if video["frame_count"] != expected_frames:
            raise ValueError("Frame count changed")
        if mode == "high_lk":
            if video["stride_mode"] != "sync_fixed" or video["stride"] != 8:
                raise ValueError(f"Unexpected detection policy: {video['stride_mode']} / {video['stride']}")
            indices = [f["index"] for f in video["frames"] if f["detection_frame"]]
            if indices != list(range(0, expected_frames, 8)):
                raise ValueError("Detection frame schedule changed")
            if video["model_calls"] != len(indices):
                raise ValueError("Model call count changed")
        return record

    for round_index in range(1, 4):
        order = ("fp32", "int8") if round_index % 2 else ("int8", "fp32")
        for video_name in sorted(videos):
            for name in order:
                records[name].append(run(name, "high_lk", round_index, video_name))
    # Model-only AP uses the same native backend and a lower candidate score floor.
    detector_records = {name: [] for name in records}
    for video_name in sorted(videos):
        for name in ("int8", "fp32"):
            detector_records[name].append(run(name, "detector", 1, video_name))

    def predictions(raw_records: list[dict]) -> dict:
        result = {}
        for record in raw_records:
            video_name = record["video_name"]
            source = {f["index"]: f for f in record["videos"][video_name]["frames"]}
            result[video_name] = {}
            for label_index in labels[video_name]:
                source_index = label_index * strides[video_name]
                if source_index not in source:
                    raise ValueError(f"Missing labeled frame {video_name}:{source_index}")
                objects = []
                for item in source[source_index]["boxes"]:
                    class_id = item["class_id"]
                    if not 0 <= class_id < len(classes):
                        raise ValueError("Invalid class ID")
                    class_name = canonical_class(classes[class_id])
                    if class_name:
                        objects.append(ObjectRecord(label_index, str(item["id"]), class_name,
                                                    tuple(item["box"]), item["score"]))
                result[video_name][label_index] = objects
        return result

    summaries = {}
    accuracy = {}
    for name, items in records.items():
        stage_samples = {stage: [sample for record in items
                                for sample in record["videos"][record["video_name"]][stage]]
                         for stage in ("preprocess_ms", "infer_ms", "postprocess_ms")}
        memory_means = [statistics.mean(r["videos"][r["video_name"]]["rss_samples_mib"]) for r in items]
        memory_peaks = [max(r["videos"][r["video_name"]]["rss_samples_mib"]) for r in items]
        round_stats = []
        for round_index in range(1, 4):
            round_items = [r for r in items if r["round"] == round_index]
            round_stats.append({
                "round": round_index,
                "infer_mean_ms": statistics.mean([v for r in round_items for v in r["videos"][r["video_name"]]["infer_ms"]]),
                "elapsed_ms": sum(r["videos"][r["video_name"]]["elapsed_ms"] for r in round_items),
                "model_calls": sum(r["videos"][r["video_name"]]["model_calls"] for r in round_items),
                "lk_total_ms": sum(r["videos"][r["video_name"]]["lk_total_ms"] for r in round_items),
                "rss_mean_mib": statistics.mean([statistics.mean(r["videos"][r["video_name"]]["rss_samples_mib"]) for r in round_items]),
            })
        summaries[name] = {
            "stages": {stage: stats(values) for stage, values in stage_samples.items()},
            "rounds": round_stats,
            "memory": {"running_mean_mib": stats(memory_means), "running_peak_mib": stats(memory_peaks),
                       "after_warmup_mib": stats([r["rss_after_warmup_mib"] for r in items]),
                       "process_peak_mib": stats([r["process_peak_rss_mib"] for r in items])},
            "native_versions": {k: items[0][k] for k in ("openvino_version", "opencv_version")},
        }
        first_round = [r for r in items if r["round"] == 1]
        lk_predictions = predictions(first_round)
        groups = {"all_labeled_frames": labels,
                  "detection_labeled_frames": {n: {i: v for i, v in frames.items() if i*strides[n] % 8 == 0} for n, frames in labels.items()},
                  "lk_labeled_frames": {n: {i: v for i, v in frames.items() if i*strides[n] % 8 != 0} for n, frames in labels.items()}}
        accuracy[name] = {}
        for group, selected_labels in groups.items():
            selected_predictions = {n: {i: lk_predictions[n][i] for i in frames} for n, frames in selected_labels.items()}
            selected_crowds = {n: {i: crowds[n][i] for i in frames} for n, frames in selected_labels.items()}
            accuracy[name][group] = evaluation.evaluate_predictions(
                selected_predictions, selected_labels, selected_crowds, [], 0.25, 0.5)
        accuracy[name]["detector_ap"] = evaluation.evaluate_predictions(
            predictions(detector_records[name]), labels, crowds, [0.25], 0.001, 0.5)
        detector_predictions = predictions(detector_records[name])
        iou_ap = {}
        for i in range(10):
            iou = 0.5 + i*0.05
            metrics = evaluation.evaluate_predictions(
                detector_predictions, labels, crowds, [], 0.001, iou)
            iou_ap[f"{iou:.2f}"] = metrics["operating_points"]["0.001"]["pooled"]["map50"]
        accuracy[name]["detector_map50_95"] = statistics.mean(iou_ap.values())
        accuracy[name]["detector_ap_by_iou"] = iou_ap
        accuracy[name]["prediction_repeatability"] = []
        for video_name in sorted(videos):
            hashes = [hashlib.sha256(json.dumps(r["videos"][video_name]["frames"], sort_keys=True).encode()).hexdigest()
                      for r in items if r["video_name"] == video_name]
            accuracy[name]["prediction_repeatability"].append({"video": video_name, "identical_across_three_rounds": len(set(hashes)) == 1,
                                                            "sha256": hashes})
    source_files = ["config/app_config.cpp", "image/image_processing.cpp", "model/yolo_engine.cpp",
                    "model/inference_scheduler.cpp", "stream/stream_processor.cpp", "tracking/authority_tracker.cpp",
                    "tracking/byte_tracker.cpp", "video/optical_flow_tracker.cpp", "video/video_inference_detail.cpp",
                    "video/video_inference.cpp"]
    source_hashes = {p: sha256_file(args.repo / "yolo_onnx_cpp" / p) for p in source_files}
    summary = {"generated_at": datetime.now(ZoneInfo("Asia/Shanghai")).isoformat(),
               "models": model_info, "inputs": inputs, "summaries": summaries, "accuracy": accuracy,
               "environment": {"platform": platform.platform(), "cpu_affinity": sorted(os.sched_getaffinity(0)),
                               "lscpu": subprocess.check_output(["lscpu", "-J"], text=True),
                               "git_head": subprocess.check_output(["git", "-C", str(args.repo), "rev-parse", "HEAD"], text=True).strip()},
               "source_hashes": source_hashes, "runner_sha256": sha256_file(args.runner),
               "commands": commands,
               "configuration": {"backend": "OpenVINO CPU", "model_threads": 8, "opencv_threads": 4,
                                 "model_input": [1,3,736,1280], "detection_stride": 8, "sync": True,
                                 "warmup_calls": 10, "repeat_pairs": 3, "rss_interval_ms": 10,
                                 "pipeline_score": 0.25, "detector_ap_floor": 0.001, "nms_iou": 0.45,
                                 "match_iou": 0.5}}
    (out / "summary.json").write_text(json.dumps(summary, indent=2)+"\n")
    (out / "commands.json").write_text(json.dumps(commands, indent=2)+"\n")
    print(f"Saved {out / 'summary.json'}", flush=True)


if __name__ == "__main__":
    main()
