"""Reproducible, training-free PTQ sensitivity analysis of a local ONNX detector."""
from __future__ import annotations

import argparse
import copy
import csv
import json
import math
import os
import platform
import resource
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from collections import Counter
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo
from unittest.mock import patch

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parent
sys.path = [str(REPO), str(REPO / "yolo_onnx_cpp/tools")] + [
    p for p in sys.path if not p or Path(p).resolve() != SCRIPT_DIR]

import cv2
import numpy as np
import onnx
import onnxruntime as ort
import openvino as ov
from onnx import numpy_helper
from onnxruntime.quantization import CalibrationMethod, QuantType, quantize_static, QuantFormat
from onnxruntime.quantization.calibrate import CalibraterBase, TensorsData, create_calibrator
from onnxruntime.quantization.qdq_quantizer import QDQQuantizer
from onnxruntime.quantization.quant_utils import load_model_with_shape_infer
from onnxruntime.quantization.shape_inference import quant_pre_process
from scipy.optimize import linear_sum_assignment

from model.onnx import ImageCalibrationDataReader, collect_calibration_images, load_data_yaml
from model.ptq_int8 import PTQ_OPTIONS, validate_exclusions
from model.quantization import PROFILE, file_sha256, inspect_int8, ordered_names
import evaluate_bdd100k_openvino_quantization as evaluation
from evaluate_bdd100k_tracking import ObjectRecord, box_iou


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n",
                    encoding="utf-8")


def session(path: Path, diagnostic: bool = False) -> ort.InferenceSession:
    options = ort.SessionOptions()
    options.intra_op_num_threads = 8
    options.inter_op_num_threads = 1
    if diagnostic:
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    return ort.InferenceSession(str(path), options, providers=["CPUExecutionProvider"])


def preprocess(path: Path, size: tuple[int, int]):
    image = cv2.imread(str(path))
    if image is None:
        raise ValueError(f"Unreadable image: {path}")
    tensor, transform = evaluation.letterbox_preprocess(image, size[1], size[0])
    return tensor, transform, image


class Reader(ImageCalibrationDataReader):
    def get_next(self):
        try:
            path = next(self._iterator)
        except StopIteration:
            return None
        return {self.input_name: preprocess(path, self.imgsz)[0]}


def calibrate(source: Path, images: list[Path], size: tuple[int, int], work: Path):
    calibrator = create_calibrator(source, ["Conv"], str(work / "calibration.onnx"),
                                  providers=["CPUExecutionProvider"])
    # Bound calibration threads without modifying ORT's global classes.
    calibrator.infer_session = session(work / "calibration.onnx", diagnostic=True)
    calibrator.collect_data(Reader(onnx.load(str(source)).graph.input[0].name, images, size))
    return calibrator.compute_data()


def bounded_calibration_session(calibrator) -> None:
    calibrator.infer_session = session(Path(calibrator.augmented_model_path), diagnostic=True)


def cached_quantize(source: Path, target: Path, ranges, excluded: list[str]) -> None:
    if target.exists():
        raise FileExistsError(f"Refusing to overwrite: {target}")
    model = load_model_with_shape_infer(source)
    validate_exclusions(model, excluded)
    quantizer = QDQQuantizer(model, True, False, QuantType.QInt8, QuantType.QUInt8,
                             copy.deepcopy(ranges), [], excluded, ["Conv"], dict(PTQ_OPTIONS))
    quantizer.quantize_model()
    quantizer.model.save_model_to_file(str(target), False)


class ErrorStats:
    """Accumulate scalar sufficient statistics; never retain activation batches."""
    def __init__(self):
        self.n = 0
        self.signal = self.other = self.noise = self.dot = 0.0
        self.outside = 0
        self.has_bounds = False

    def update(self, reference: np.ndarray, actual: np.ndarray, bounds=None) -> None:
        if reference.shape != actual.shape:
            raise ValueError(f"Mismatched tensors: {reference.shape} != {actual.shape}")
        # Small chunks bound FP64 temporaries even for early high-resolution features.
        left, right = reference.reshape(-1), actual.reshape(-1)
        for start in range(0, left.size, 262144):
            a = left[start:start + 262144].astype(np.float64)
            b = right[start:start + 262144].astype(np.float64)
            if not np.isfinite(a).all() or not np.isfinite(b).all():
                raise ValueError("Non-finite diagnostic tensor")
            self.n += a.size
            self.signal += float(np.dot(a, a))
            self.other += float(np.dot(b, b))
            self.dot += float(np.dot(a, b))
            delta = a - b
            self.noise += float(np.dot(delta, delta))
            if bounds is not None:
                self.has_bounds = True
                self.outside += int(np.count_nonzero((a < bounds[0]) | (a > bounds[1])))

    def result(self) -> dict:
        return {"elements": self.n, "nmse": self.noise / max(self.signal, 1e-30),
                "cosine": self.dot / max(math.sqrt(self.signal * self.other), 1e-30)
                if self.signal or self.other else 1.0,
                "sqnr_db": 10 * math.log10(max(self.signal, 1e-30) / self.noise)
                if self.noise else None,
                "outside_fraction": self.outside / max(self.n, 1) if self.has_bounds else None}


def exposed(source: Path, names: list[str], target: Path) -> None:
    graph = onnx.load(str(source))
    del graph.graph.output[:]
    for name in dict.fromkeys(names):
        graph.graph.output.append(onnx.helper.make_tensor_value_info(name, onnx.TensorProto.FLOAT, None))
    onnx.save(graph, str(target))


def diagnose(fp32: Path, int8: Path, ranges, images: list[Path], size: tuple[int, int],
             out: Path) -> list[dict]:
    graph, quantized = onnx.load(str(fp32)), onnx.load(str(int8))
    convs = [n for n in graph.graph.node if n.op_type == "Conv"]
    qnodes = {n.name: n for n in quantized.graph.node if n.op_type == "Conv"}
    producers = {name: n for n in quantized.graph.node for name in n.output}
    weights = {i.name: numpy_helper.to_array(i) for i in graph.graph.initializer}
    packed = {i.name: numpy_helper.to_array(i) for i in quantized.graph.initializer}
    rows = []
    with tempfile.TemporaryDirectory(prefix="ptq_diagnostic_") as directory:
        root = Path(directory)
        for start in range(0, len(convs), 8):
            batch = convs[start:start + 8]
            mappings, stats = {}, {}
            for node in batch:
                qnode = qnodes[node.name]
                dq = producers[qnode.input[0]]
                pre = producers[dq.input[0]].input[0]
                mappings[node.name] = (pre, qnode.input[0], qnode.output[0])
                stats[node.name] = {k: ErrorStats() for k in ("input", "output", "local_qdq", "weight")}
                wdq = producers[qnode.input[1]]
                w, scale, zero = (packed[n] for n in wdq.input[:3])
                axis = next((a.i for a in wdq.attribute if a.name == "axis"), 1)
                shape = [1] * w.ndim
                shape[axis] = -1
                restored = (w.astype(np.float32) - zero.reshape(shape)) * scale.reshape(shape)
                stats[node.name]["weight"].update(weights[node.input[1]], restored)
            fpath, qpath = root / "float.onnx", root / "quantized.onnx"
            exposed(fp32, [name for n in batch for name in (n.input[0], n.output[0])], fpath)
            exposed(int8, [name for values in mappings.values() for name in values], qpath)
            fs, qs = session(fpath, True), session(qpath, True)
            fnames = [n.name for n in fs.get_outputs()]
            qnames = [n.name for n in qs.get_outputs()]
            for image in images:
                tensor = preprocess(image, size)[0]
                fvalues = dict(zip(fnames, fs.run(None, {fs.get_inputs()[0].name: tensor})))
                qvalues = dict(zip(qnames, qs.run(None, {qs.get_inputs()[0].name: tensor})))
                for node in batch:
                    pre, post, output = mappings[node.name]
                    lo, hi = ranges[node.input[0]].range_value
                    bounds = (float(np.min(lo)), float(np.max(hi)))
                    stats[node.name]["input"].update(fvalues[node.input[0]], qvalues[post], bounds)
                    stats[node.name]["output"].update(fvalues[node.output[0]], qvalues[output])
                    stats[node.name]["local_qdq"].update(qvalues[pre], qvalues[post], bounds)
                del fvalues, qvalues
            for node in batch:
                rows.append({"node": node.name, "input_tensor": node.input[0],
                             "output_tensor": node.output[0],
                             **{k: v.result() for k, v in stats[node.name].items()}})
            del fs, qs
            print(f"diagnostic Conv {min(start + 8, len(convs))}/{len(convs)}", flush=True)
    write_json(out / "layer_errors.json", rows)
    write_layer_csv(out, rows)
    return rows


def write_layer_csv(out: Path, rows: list[dict]) -> None:
    with (out / "layer_errors.csv").open("w", newline="") as handle:
        keys = [f"{kind}_{metric}" for kind in ("weight", "input", "output", "local_qdq")
                for metric in ("nmse", "cosine", "sqnr_db", "outside_fraction")]
        writer = csv.DictWriter(handle, fieldnames=["node", *keys])
        writer.writeheader()
        for row in rows:
            writer.writerow({"node": row["node"], **{f"{k}_{m}": row[k][m]
                              for k in ("weight", "input", "output", "local_qdq")
                              for m in ("nmse", "cosine", "sqnr_db", "outside_fraction")}})


def select_data(data: Path, out: Path) -> dict:
    rng = np.random.default_rng(42)
    train = collect_calibration_images(data, "train", 0)
    val = collect_calibration_images(data, "val", 0)
    if len(train) < 232 or len(val) < 1256:
        raise ValueError("Need at least 232 train and 1256 validation images")
    trains = rng.permutation(len(train))[:232]
    vals = rng.permutation(len(val))[:1256]
    selected = {"calibration": [train[i] for i in trains[:200]],
                "diagnostic": [train[i] for i in trains[200:]],
                "tuning": [val[i] for i in vals[:256]], "validation": [val[i] for i in vals[256:]]}
    config = load_data_yaml(data)
    base = Path(config.get("path", data.parent)).expanduser()
    if not base.is_absolute():
        base = data.parent / base
    names = ordered_names(config["names"])
    records = {}
    for group, images in selected.items():
        split = "train" if group in ("calibration", "diagnostic") else "val"
        distribution = Counter({name: 0 for name in names})
        entries = []
        for image in images:
            label = base / "labels" / split / (image.stem + ".txt")
            if not label.is_file():
                raise FileNotFoundError(label)
            for line in label.read_text().splitlines():
                fields = line.split()
                if fields:
                    if len(fields) != 5 or not 0 <= int(fields[0]) < len(names):
                        raise ValueError(f"Invalid detection label: {label}")
                    distribution[names[int(fields[0])]] += 1
            entries.append({"path": str(image), "sha256": file_sha256(image),
                            "label": str(label), "label_sha256": file_sha256(label)})
        records[group] = {"count": len(images), "classes": dict(distribution), "images": entries}
    for a, b in (("calibration", "diagnostic"), ("tuning", "validation")):
        if {r["sha256"] for r in records[a]["images"]} & {r["sha256"] for r in records[b]["images"]}:
            raise ValueError(f"Duplicate image content across {a}/{b}")
    write_json(out / "data_selection.json", {"seed": 42, "data": str(data),
                                             "data_sha256": file_sha256(data), "groups": records})
    return records


def labels_for_image(entry: dict, width: int, height: int, names: list[str], index: int):
    items = []
    for j, line in enumerate(Path(entry["label"]).read_text().splitlines()):
        if not line.strip():
            continue
        cls, x, y, w, h = map(float, line.split())
        items.append(ObjectRecord(index, str(j), names[int(cls)],
                                  ((x-w/2)*width, (y-h/2)*height, (x+w/2)*width, (y+h/2)*height)))
    return items


def quality(predictions, labels, crowds) -> dict:
    results = {}
    for iou in np.arange(0.5, 0.951, 0.05):
        result = {"ap_floor": 0.001, "operating_points": {}}
        filtered = {}
        for threshold in ([0.001, 0.25] if iou == 0.5 else [0.001]):
            ignored = 0
            for source in sorted(labels):
                selected = {index: [p for p in items if p.score >= threshold]
                            for index, items in predictions[source].items()}
                if any(crowds[source].values()):
                    selected, count = evaluation.suppress_crowd_predictions(
                        selected, labels[source], crowds[source], float(iou))
                    ignored += count
                filtered[source] = selected
            pooled, per_class = evaluation.detection_metrics(
                evaluation.combine_frames(labels), evaluation.combine_frames(filtered), float(iou))
            result["operating_points"][f"{threshold:.3f}"] = {
                "threshold": threshold, "pooled": pooled, "per_class": per_class,
                "ignored_crowd_predictions": ignored}
        results[f"{iou:.2f}"] = result
    primary = results["0.50"]["operating_points"]
    per_class = copy.deepcopy(primary["0.001"]["per_class"])
    for name, row in per_class.items():
        aps = [r["operating_points"]["0.001"]["per_class"][name]["ap50"] for r in results.values()]
        row["ap50_95"] = float(np.mean([v for v in aps if v is not None])) if any(v is not None for v in aps) else None
    return {"map50": primary["0.001"]["pooled"]["map50"],
            "map50_95": float(np.mean([r["operating_points"]["0.001"]["pooled"]["map50"] for r in results.values()])),
            "operating_point": primary["0.250"]["pooled"], "per_class": per_class,
            "iou_results": results}


def worker(job_path: Path) -> None:
    job = json.loads(job_path.read_text())
    cv2.setNumThreads(4)
    names, size = job["classes"], tuple(job["size"])
    core = ov.Core()
    compiled = core.compile_model(job["model"], "CPU", {
        "INFERENCE_NUM_THREADS": 8, "NUM_STREAMS": 1, "PERFORMANCE_HINT": "LATENCY",
        "INFERENCE_PRECISION_HINT": ov.Type.f32, "ENABLE_CPU_PINNING": False})
    request = compiled.create_infer_request()
    def infer(tensor):
        start = time.perf_counter_ns()
        output = np.array(request.infer({compiled.input(0): tensor})[compiled.output(0)], copy=True)
        elapsed = (time.perf_counter_ns() - start) / 1e6
        if not np.isfinite(output).all():
            raise ValueError("Non-finite inference output")
        return output, elapsed
    first = preprocess(Path(job["images"][0]["path"]), size)[0]
    for _ in range(10):
        infer(first)
    timings, predictions, labels, crowds, snapshots = [], {"images": {}}, {"images": {}}, {"images": {}}, []
    for index, entry in enumerate(job["images"]):
        tensor, transform, image = preprocess(Path(entry["path"]), size)
        output, elapsed = infer(tensor)
        timings.append(elapsed)
        if job["mode"] == "benchmark":
            continue
        detections = evaluation.decode_output(output, transform, names, 0.001, 0.45)
        predictions["images"][index] = [ObjectRecord(index, str(j), names[cls], box, score)
                                       for j, (cls, score, box) in enumerate(detections)]
        labels["images"][index] = labels_for_image(entry, image.shape[1], image.shape[0], names, index)
        crowds["images"][index] = []
        if index < 8:
            snapshots.append({"image": entry["path"], "boxes": detections})
        if (index + 1) % 100 == 0:
            print(f"{job_path.stem}: {index + 1}/{len(job['images'])}", flush=True)
    # Read runtime precisions to distinguish real INT8 kernels from QDQ storage.
    precisions = Counter()
    convolution_precisions = []
    for node in compiled.get_runtime_model().get_ops():
        info = node.get_rt_info()
        if "runtimePrecision" in info:
            precisions[str(info["runtimePrecision"].value)] += 1
            if "layerType" in info and "Convolution" in str(info["layerType"].value):
                convolution_precisions.append({"name": node.get_friendly_name(),
                    "precision": str(info["runtimePrecision"].value),
                    "primitive": str(info["primitiveType"].value) if "primitiveType" in info else None,
                    "original_layers": str(info["originalLayersNames"].value) if "originalLayersNames" in info else None})
    runtime = {"mean_ms": float(np.mean(timings)), "p95_ms": float(np.percentile(timings, 95)),
               "throughput": 1000 / float(np.mean(timings)), "samples_ms": timings,
               "rss_mib": int(Path("/proc/self/statm").read_text().split()[1]) * os.sysconf("SC_PAGE_SIZE") / 2**20,
               "peak_rss_mib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024,
               "runtime_precisions": dict(precisions), "convolution_precisions": convolution_precisions,
               "version": ov.__version__}
    result = {"runtime": runtime, "snapshots": snapshots}
    if job["mode"] != "benchmark":
        result["quality"] = quality(predictions, labels, crowds)
    if job.get("manifest"):
        video_labels, video_crowds, strides, manifest = evaluation.load_ground_truth(Path(job["manifest"]))
        video_predictions = {}
        for name, meta in manifest["videos"].items():
            path = Path(meta["video"]["path"])
            if file_sha256(path) != meta["video"]["sha256"]:
                raise ValueError(f"Video changed: {path}")
            capture = cv2.VideoCapture(str(path))
            source_index = 0
            targets = {index * strides[name]: index for index in video_labels[name]}
            video_predictions[name] = {}
            while source_index <= max(targets):
                ok, image = capture.read()
                if not ok:
                    raise ValueError(f"Missing video frame: {name}:{source_index}")
                if source_index in targets:
                    tensor, transform = evaluation.letterbox_preprocess(image, size[1], size[0])
                    output, _ = infer(tensor)
                    index = targets[source_index]
                    detections = evaluation.decode_output(output, transform, names, 0.001, 0.45)
                    video_predictions[name][index] = [ObjectRecord(index, str(j), evaluation.canonical_class(names[cls]), box, score)
                                                     for j, (cls, score, box) in enumerate(detections)
                                                     if evaluation.canonical_class(names[cls]) is not None]
                source_index += 1
            capture.release()
            print(f"{job_path.stem}: video {name} complete", flush=True)
        result["video_quality"] = quality(video_predictions, video_labels, video_crowds)
        result["video_frames"] = sum(len(v) for v in video_labels.values())
    write_json(Path(job["result"]), result)


def run_job(out: Path, name: str, model: Path, images: list[dict], classes: list[str],
            size: tuple[int, int], mode: str = "evaluate", manifest: Path | None = None) -> dict:
    job_path, result = out / f"{name}_job.json", out / f"{name}.json"
    job = {"model": str(model), "images": images, "classes": classes, "size": size,
           "model_sha256": file_sha256(model),
           "mode": mode, "result": str(result), "manifest": str(manifest) if manifest else None}
    if result.exists():
        if not job_path.is_file() or json.loads(job_path.read_text()) != json.loads(json.dumps(job)):
            raise ValueError(f"Saved job inputs changed: {job_path}")
        print(f"REUSE {name}", flush=True)
        return json.loads(result.read_text())
    write_json(job_path, job)
    command = [sys.executable, str(Path(__file__).resolve()), "--worker", str(job_path)]
    print(f"RUN {name}", flush=True)
    with (out / f"{name}.log").open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    return json.loads(result.read_text())


def head_nodes(graph) -> list[str]:
    nodes = [n for n in graph.graph.node if n.op_type == "Conv" and "one2one_cv" in n.name]
    terminals = [n.name for n in nodes if n.name.endswith(".2/Conv")]
    if len(terminals) != 6:
        raise ValueError(f"Expected six YOLO26 terminal head Conv nodes, got {terminals}")
    return terminals


def speed_retention(fp32_ms: float, baseline_ms: float, candidate_ms: float):
    saving = fp32_ms - baseline_ms
    return (fp32_ms - candidate_ms) / saving if saving > 0 else None


def matched_boxes(left: list, right: list) -> dict:
    left, right = [b for b in left if b[1] >= 0.25], [b for b in right if b[1] >= 0.25]
    costs = np.ones((len(left), len(right)))
    for i, a in enumerate(left):
        for j, b in enumerate(right):
            if a[0] == b[0]:
                costs[i, j] = 1 - box_iou(a[2], b[2])
    rows, cols = linear_sum_assignment(costs)
    pairs = [(int(i), int(j)) for i, j in zip(rows, cols) if costs[i, j] <= 0.5]
    return {"matched": len(pairs), "fp32_unmatched": len(left)-len(pairs),
            "quantized_unmatched": len(right)-len(pairs),
            "mean_iou": float(np.mean([1-costs[i, j] for i, j in pairs])) if pairs else None,
            "mean_score_delta": float(np.mean([right[j][1]-left[i][1] for i, j in pairs])) if pairs else None}


def plots_and_report(out: Path, report: dict, layers: list[dict], results: dict) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from ultralytics.utils.plotting import colors
    assets = out / "assets"
    assets.mkdir(exist_ok=True)
    plt.rcParams["font.family"] = "DejaVu Sans"
    fig, ax = plt.subplots(figsize=(12, 4))
    ax.plot([r["output"]["nmse"] for r in layers], label="Cumulative Conv output NMSE")
    ax.plot([r["local_qdq"]["nmse"] for r in layers], label="Local activation QDQ NMSE")
    ax.set(xlabel="Conv index in graph", ylabel="NMSE", yscale="log")
    ax.legend()
    fig.tight_layout()
    fig.savefig(assets / "layer_errors.png", dpi=160)
    plt.close(fig)
    fig, ax = plt.subplots(figsize=(7, 5))
    for row in report["ablations"]:
        ax.scatter(row["mean_ms"], row["map50_95"]*100, s=20, color="#777777")
    for name in ("fp32", "direct_int8", "optimized_int8"):
        row = report["models"][name]
        ax.scatter(row["tuning_ms"], row["tuning_map50_95"]*100, s=65, label=name)
    ax.set(xlabel="OpenVINO inference latency (ms)", ylabel="Tuning mAP50-95 (%)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(assets / "tradeoff.png", dpi=160)
    plt.close(fig)
    comparisons = []
    for index in range(4):
        snapshots = [results[n]["snapshots"][index] for n in results]
        panels = []
        for name, snapshot in zip(results, snapshots):
            image = cv2.imread(snapshot["image"])
            labels_placed = []
            for cls, score, box in snapshot["boxes"]:
                if score < 0.25:
                    continue
                x1, y1, x2, y2 = map(int, box)
                color = colors(cls, bgr=True)
                cv2.rectangle(image, (x1, y1), (x2, y2), color, 2)
                if x2-x1 < 50 or y2-y1 < 28:
                    continue
                text = f"{report['classes'][cls]} {score:.2f}"
                (width, height), baseline = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 1)
                left = max(0, min(x1, image.shape[1]-width-6))
                top = max(0, y1-height-baseline-7)
                rect = (left, top, left+width+6, top+height+baseline+6)
                if any(rect[0] < r[2] and rect[2] > r[0] and rect[1] < r[3] and rect[3] > r[1]
                       for r in labels_placed):
                    continue
                labels_placed.append(rect)
                cv2.rectangle(image, rect[:2], rect[2:], color, -1)
                text_color = (20,20,20) if sum(color) > 420 else (255,255,255)
                cv2.putText(image, text, (left+3, top+height+2), cv2.FONT_HERSHEY_SIMPLEX,
                            0.55, text_color, 1, cv2.LINE_AA)
            image = cv2.copyMakeBorder(image, 50, 0, 0, 0, cv2.BORDER_CONSTANT, value=(248,248,248))
            cv2.putText(image, name, (20, 35), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (30,30,30), 2)
            panels.append(image)
        cv2.imwrite(str(assets / f"comparison_{index}.jpg"), np.concatenate(panels, axis=0))
        comparisons.append({"image": snapshots[0]["image"],
                            **{n: matched_boxes(snapshots[0]["boxes"], results[n]["snapshots"][index]["boxes"])
                               for n in ("direct_int8", "optimized_int8")}})
    write_json(out / "matched_detections.json", comparisons)
    lines = ["# 高模型 PTQ 逐层诊断与优化报告", "",
             f"生成时间：{report['created_at']}。本报告三组数据均为本次实测。", "",
             f"优化状态：**{report['status']}**。最终模型是 INT8/FP32 混合精度 PTQ，没有训练或权重更新。", "",
             "## 三组验证结果", "",
             "| 模型 | mAP50 % | mAP50-95 % | P % | R % | F1 % | 平均 ms | P95 ms | FPS | 文件 MB | RSS MiB | INT8/FP32 Conv |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for name, row in report["models"].items():
        q, b = row["validation"], row["benchmark"]
        op = q["operating_point"]
        lines.append(f"| {name} | {q['map50']*100:.3f} | {q['map50_95']*100:.3f} | {op['precision']*100:.2f} | {op['recall']*100:.2f} | {op['f1']*100:.2f} | {b['mean_ms']:.3f} | {b['p95_ms']:.3f} | {b['throughput']:.2f} | {row['bytes']/1e6:.3f} | {b['rss_mib']:.2f} | {row['int8_conv_count']}/{row['fp32_conv_count']} |")
    original_ap, direct_ap, optimized_ap = [report["models"][n]["validation"]["map50_95"]
                                          for n in ("fp32", "direct_int8", "optimized_int8")]
    loss, restored = original_ap-direct_ap, optimized_ap-direct_ap
    lines += ["", f"独立验证集的mAP50-95损失由{loss*100:.3f}个百分点缩小到{(original_ap-optimized_ap)*100:.3f}个百分点；相对直接INT8提升{restored*100:.3f}个百分点。"]
    if loss > 0:
        lines += [f"恢复了{restored/loss*100:.2f}%的直接PTQ损失。该比例以同一独立验证集的FP32基线计算，不来自调优集。"]
    retention = report["speed_retention"]
    lines += ["", f"推理时间节省保留比例：{f'{retention*100:.2f}%' if retention is not None else '不适用（直接 INT8 未加速）'}。",
              "延迟仅覆盖同步 infer 和输出读取，不含图片读取、预处理、后处理、模型编译；吞吐为单请求 1000/平均延迟。RSS 为三轮推理结束时的独立进程驻留内存均值，不是运行期连续采样均值，也不是模型独占内存。", "",
              "## 数据与方法", "",
              "输入固定为 [1,3,736,1280]；种子42，训练集200张校准、另32张逐层诊断；验证集256张调优、另1000张最终评测。路径、图片与标签 SHA-256、逐类真值数见 data_selection.json。最终评测集未用于选择排除层。",
              "这不是完整1万张验证集验收；各类别样本量见逐类表，稀有类样本不足时不能据此证明该类的泛化精度恢复。",
              "原始部署模型 opset12 转为13并预处理，转换一致性记录见 parity.json。直接与优化 PTQ 共享 MinMax 统计；Conv输入U8、权重逐通道S8；bias、Conv输出及其他算子保持浮点。",
              "三组均为 OpenVINO CPU、8线程、单请求、LATENCY、浮点精度FP32。各模型独立进程预热10次，用相同100张图片执行三轮，轮换顺序；编译后实际算子精度见各轮JSON。",
              "mAP采用仓库AP101匹配，在IoU 0.50:0.05:0.95上平均；候选下限0.001，P/R/F1置信度0.25，外部class-aware NMS IoU0.45。TopK输出先解码再匹配，逐层比较发生于TopK之前。",
              "SQNR为无误差时记null；零参考张量的NMSE使用1e-30分母。局部QDQ误差来自同一量化图内Q前/Q后张量，累计误差包含上游传播。仅输入激活统计超出校准范围的比例，权重与浮点Conv输出的该字段为null（不适用）。",
              "逐层诊断使用关闭图优化的ORT CPU，会与OpenVINO融合后的真实内核有数值差异；最终精度与速度结论以OpenVINO实测为准。诊断方法参考[ORT官方量化文档](https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html)。", "",
              "## 敏感层与消融", "", "![逐层误差](assets/layer_errors.png)", "",
              "| 节点 | 累计输出 NMSE | 局部 QDQ NMSE | 权重 NMSE | 单层排除 mAP50-95 增益百分点 |",
              "|---|---:|---:|---:|---:|"]
    gains = {r["excluded"][0]: r["gain"] for r in report["ablations"] if len(r["excluded"]) == 1}
    for row in sorted(layers, key=lambda r: r["output"]["nmse"], reverse=True)[:15]:
        gain = gains.get(row["node"])
        lines.append(f"| `{row['node']}` | {row['output']['nmse']:.6g} | {row['local_qdq']['nmse']:.6g} | {row['weight']['nmse']:.6g} | {f'{gain*100:+.3f}' if gain is not None else '未入选单层实验'} |")
    lines += ["", "![精度延迟权衡](assets/tradeoff.png)", "",
              "以下为调优集精度最高的五个候选；选择规则是先满足至少80%的时间节省保留约束，再按mAP50-95排序。这里的延迟来自调优阶段，不能替代三轮正式延迟结果。", "",
              "| 候选文件 | FP32 Conv | 调优mAP50-95 % | 相对直接INT8增益百分点 | 平均ms | 时间节省保留 % |",
              "|---|---:|---:|---:|---:|---:|"]
    for row in sorted(report["ablations"], key=lambda r: r["map50_95"], reverse=True)[:5]:
        retained = row["speed_retention"]
        lines.append(f"| `{Path(row['model']).name}` | {len(row['excluded'])} | {row['map50_95']*100:.3f} | {row['gain']*100:+.3f} | {row['mean_ms']:.3f} | {f'{retained*100:.2f}' if retained is not None else '不适用'} |")
    lines += ["", "候选之间的微小分数差异没有做统计显著性检验；完整方案及排除节点见ablations.json/CSV。最终验证结果不参与重选模型。", "", "最终保留FP32的Conv：", ""]
    lines += [f"- `{name}`" for name in report["models"]["optimized_int8"]["excluded_nodes"]]
    if not report["models"]["optimized_int8"]["excluded_nodes"]:
        lines += ["无满足条件的排除组合，优化组沿用直接PTQ模型作为未改善对照。"]
    lines += ["", "被排除层保留FP32权重及浮点卷积，但仍受上游量化误差影响；若与其他Conv共享输入QDQ，具体节点见summary.json。不能把所有误差较大的层直接解释为误差源。", "",
              "## 逐类验证精度", "", "| 类别 | 真值数 | FP32 AP50/AP50-95 % | 直接INT8 AP50/AP50-95 % | 优化INT8 AP50/AP50-95 % |", "|---|---:|---:|---:|---:|"]
    for name in report["classes"]:
        cells = []
        count = 0
        for row in report["models"].values():
            c = row["validation"]["per_class"].get(name, {})
            count = c.get("ground_truth", count)
            cells.append(f"{c['ap50']*100:.2f}/{c['ap50_95']*100:.2f}" if c.get("ap50") is not None else "无真值")
        lines.append(f"| {name} | {count} | " + " | ".join(cells) + " |")
    lines += ["", "## 历史视频复测", "", "| 模型 | 标注帧 | mAP50 % | mAP50-95 % | P % | R % | F1 % |", "|---|---:|---:|---:|---:|---:|---:|"]
    for name, row in report["models"].items():
        q = row["video"]
        p = q["operating_point"]
        lines.append(f"| {name} | {results[name]['video_frames']} | {q['map50']*100:.3f} | {q['map50_95']*100:.3f} | {p['precision']*100:.2f} | {p['recall']*100:.2f} | {p['f1']*100:.2f} |")
    lines += ["", "视频为历史定向选择的training视频，仅含八类真值；忽略区域按既有manifest处理，不作为独立泛化验收。2026-10-08历史mAP50为32.064%/28.669%，使用不同校准集及量化配置，不能当作本次基线。", "",
              "## 检测对照", "", "各图由上至下为FP32、直接INT8、优化INT8，同一置信度0.25；保留全部检测框，仅为较大目标显示不重叠的类别和分数标签。同类IoU≥0.5的匈牙利匹配统计见matched_detections.json，完整样例框见measured/final_*.json中的snapshots。"]
    for index in range(4):
        lines += ["", f"![对照{index+1}](assets/comparison_{index}.jpg)"]
    lines += ["", "## 复现与原始数据", "", "```bash", report["command"], "```", "",
              "复现时必须使用一个不存在的新输出目录。代码与源模型哈希、校准统计、候选结果、三轮延迟样本、软件版本均保存在本目录。完整消融见ablations.csv，三组汇总见metrics.csv与summary.json。", "",
              "本次只尝试敏感层排除；若未达标，结论限定于已测试候选，不声称其他PTQ方法无效。"]
    if (out / "implementation_validation.md").exists():
        lines += ["", "实现回归及校准线程差异的处理过程见[验证记录](implementation_validation.md)。"]
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def finish_report(out: Path) -> None:
    """Package measured results and immutable input provenance without rerunning inference."""
    report = json.loads((out / "summary.json").read_text())
    report["quantization"] = {"profile": PROFILE, "extra_options": PTQ_OPTIONS,
                              "calibration": "MinMax", "calibration_threads": 8,
                              "ranges_sha256": file_sha256(out/"calibration_ranges.json"),
                              "selection_sha256": file_sha256(out/"data_selection.json")}
    for name, model in report["models"].items():
        if file_sha256(Path(model["path"])) != model["sha256"]:
            raise ValueError(f"Measured model changed: {name}")
        runtime_counts = []
        for run in model["benchmark"]["rounds"]:
            convs = run.get("convolution_precisions", [])
            if not convs:
                continue
            counts = Counter(c["precision"] for c in convs)
            if (len(convs) != model["conv_count"] or counts["f32"] != model["fp32_conv_count"]
                    or counts["u8"] + counts["i8"] != model["int8_conv_count"]):
                raise ValueError(f"Runtime Conv precision differs from graph: {name}: {counts}")
            if name == "optimized_int8":
                floating = {n for n in model["excluded_nodes"] for c in convs
                            if c["precision"] == "f32" and n in (c["original_layers"] or "").split(",")}
                if floating != set(model["excluded_nodes"]):
                    raise ValueError("Excluded nodes did not map to runtime FP32 Convs")
            runtime_counts.append(dict(counts))
        model["runtime_conv_precisions"] = runtime_counts
    if file_sha256(Path(report["source"]["path"])) != report["models"]["fp32"]["sha256"]:
        raise ValueError("Source model changed during analysis")
    report["code_sha256"] = {str(p.relative_to(REPO)): file_sha256(p) for p in (
        Path(__file__), SCRIPT_DIR/"ptq_int8.py", SCRIPT_DIR/"quantization.py", SCRIPT_DIR/"onnx.py",
        REPO/"yolo_onnx_cpp/tools/evaluate_bdd100k_openvino_quantization.py",
        REPO/"yolo_onnx_cpp/tools/evaluate_bdd100k_tracking.py")}
    write_json(out / "models/classes.json", dict(enumerate(report["classes"])))
    layers = json.loads((out / "layer_errors.json").read_text())
    for row in layers:
        for kind in ("weight", "output"):
            row[kind]["outside_fraction"] = None
    write_json(out / "layer_errors.json", layers)
    write_layer_csv(out, layers)
    job = json.loads((out / "measured/final_fp32_job.json").read_text())
    manifest = Path(job["manifest"])
    report["video_manifest"] = {"path": str(manifest), "sha256": file_sha256(manifest),
                                "metadata": json.loads(manifest.read_text())}
    cpu_lines = Path("/proc/cpuinfo").read_text().splitlines()
    report["hardware"] = {"platform": platform.platform(), "logical_cpus": os.cpu_count(),
                          "cpu_model": next((s.split(":",1)[1].strip() for s in cpu_lines if s.startswith("model name")), "unknown")}
    report["versions"].update(python=platform.python_version(), opencv=cv2.__version__)
    write_json(out / "summary.json", report)
    final = {name: json.loads((out / f"measured/final_{name}.json").read_text()) for name in report["models"]}
    plots_and_report(out, report, layers, final)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=REPO / "yolo_onnx_cpp/deploy/best.onnx")
    parser.add_argument("--data", type=Path, default=REPO / "model/data/bdd100k_yolo_det/data.yaml")
    parser.add_argument("--classes", type=Path, default=REPO / "yolo_onnx_cpp/deploy/classes.json")
    parser.add_argument("--manifest", type=Path, default=REPO / "docs/test-results/bdd100k_long_tail/gt_3videos_stride6_20260911/manifest.json")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--resume", action="store_true", help="Resume an incomplete analysis in --output-dir")
    parser.add_argument("--worker", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.worker:
        worker(args.worker)
        return
    out = (args.output_dir or REPO / "docs/test-results" / datetime.now(ZoneInfo("Asia/Shanghai")).strftime("high_ptq_analysis_%Y%m%d_%H%M%S")).resolve()
    if args.resume:
        if not args.output_dir or not out.is_dir() or (out / "report.md").exists():
            raise ValueError("--resume requires an existing incomplete output directory")
    else:
        out.mkdir(parents=True, exist_ok=False)
    print(f"OUTPUT {out}", flush=True)
    size = (736, 1280)
    cv2.setNumThreads(4)
    models, jobs = out / "models", out / "measured"
    models.mkdir(exist_ok=args.resume)
    jobs.mkdir(exist_ok=args.resume)
    classes = evaluation.load_classes(args.classes)
    if classes != ordered_names(load_data_yaml(args.data)["names"]):
        raise ValueError("Dataset and deployed model class orders differ")
    source = onnx.load(str(args.source))
    shape = [d.dim_value for d in source.graph.input[0].type.tensor_type.shape.dim]
    if shape != [1, 3, *size]:
        raise ValueError(f"Expected high model input, got {shape}")
    if args.resume:
        saved = json.loads((out / "data_selection.json").read_text())
        if saved["data_sha256"] != file_sha256(args.data) or file_sha256(models / "fp32.onnx") != file_sha256(args.source):
            raise ValueError("Resume source model or dataset YAML changed")
        selection = saved["groups"]
        for group in selection.values():
            for entry in group["images"]:
                if file_sha256(Path(entry["path"])) != entry["sha256"] or file_sha256(Path(entry["label"])) != entry["label_sha256"]:
                    raise ValueError(f"Resume image/label changed: {entry['path']}")
    else:
        selection = select_data(args.data.resolve(), out)
    calibration = [Path(r["path"]) for r in selection["calibration"]["images"]]
    diagnostic = [Path(r["path"]) for r in selection["diagnostic"]["images"]]
    original, converted, prepared = models / "fp32.onnx", models / "fp32_opset13.onnx", models / "fp32_prepared.onnx"
    if not original.exists():
        shutil.copyfile(args.source, original)
    if not converted.exists():
        onnx.save(onnx.version_converter.convert_version(source, 13), str(converted))
    if not prepared.exists():
        quant_pre_process(str(converted), str(prepared), auto_merge=True)
    sessions = [session(p) for p in (original, converted, prepared)]
    parity = []
    for image in diagnostic[:3]:
        tensor = preprocess(image, size)[0]
        values = [s.run(None, {s.get_inputs()[0].name: tensor})[0] for s in sessions]
        for name, value in zip(("opset13", "preprocessed"), values[1:]):
            np.testing.assert_allclose(values[0], value, rtol=1e-5, atol=1e-4)
            parity.append({"image": str(image), "variant": name, "max_abs": float(np.max(np.abs(values[0]-value)))})
    del sessions
    write_json(out / "parity.json", parity)
    if (out / "calibration_ranges.json").exists():
        saved_ranges = json.loads((out / "calibration_ranges.json").read_text())
        ranges = TensorsData(CalibrationMethod.MinMax, {k: (np.asarray(v["min"], dtype=np.float32), np.asarray(v["max"], dtype=np.float32)) for k,v in saved_ranges.items()})
    else:
        with tempfile.TemporaryDirectory(prefix="ptq_calibration_") as directory:
            ranges = calibrate(prepared, calibration, size, Path(directory))
        write_json(out / "calibration_ranges.json", {k: {"min": np.asarray(v.range_value[0]).tolist(),
                                                       "max": np.asarray(v.range_value[1]).tolist()} for k,v in ranges.items()})
    direct = models / "direct_int8.onnx"
    if not direct.exists():
        cached_quantize(prepared, direct, ranges, [])
    # Audit the reusable path against the public API on the exact same images.
    with tempfile.TemporaryDirectory(prefix="ptq_standard_") as directory:
        standard = Path(directory) / "standard.onnx"
        with patch.object(CalibraterBase, "create_inference_session", bounded_calibration_session):
            quantize_static(prepared, standard, Reader(source.graph.input[0].name, calibration, size),
                            quant_format=QuantFormat.QDQ, activation_type=QuantType.QUInt8,
                            weight_type=QuantType.QInt8, per_channel=True,
                            calibrate_method=CalibrationMethod.MinMax, op_types_to_quantize=["Conv"],
                            extra_options=PTQ_OPTIONS)
        left, right = onnx.load(str(direct)), onnx.load(str(standard))
        li = {i.name: numpy_helper.to_array(i) for i in left.graph.initializer}
        ri = {i.name: numpy_helper.to_array(i) for i in right.graph.initializer}
        if li.keys() != ri.keys():
            raise ValueError("Cached/public quantizer initializer names differ")
        for key in li:
            np.testing.assert_array_equal(li[key], ri[key])
        ls, rs = session(direct), session(standard)
        for path in diagnostic[:3]:
            tensor = preprocess(path, size)[0]
            np.testing.assert_array_equal(ls.run(None, {ls.get_inputs()[0].name: tensor})[0],
                                          rs.run(None, {rs.get_inputs()[0].name: tensor})[0])
        write_json(out / "cache_parity.json", {"initializers_equal": True, "outputs_equal": True,
                                                "checked_images": 3, "calibration_threads": 8, "ort_version": ort.__version__})
        del ls, rs
    print("calibration and cache parity complete", flush=True)
    layers = json.loads((out / "layer_errors.json").read_text()) if (out / "layer_errors.json").exists() else diagnose(prepared, direct, ranges, diagnostic, size, out)
    heads = head_nodes(onnx.load(str(prepared)))
    candidates = list(dict.fromkeys([r["node"] for r in sorted(layers, key=lambda r: r["output"]["nmse"], reverse=True)[:10]] +
                                   [r["node"] for r in sorted(layers, key=lambda r: r["local_qdq"]["nmse"], reverse=True)[:10]] + heads))
    tuning = selection["tuning"]["images"]
    fp_result = run_job(jobs, "tuning_fp32", original, tuning, classes, size)
    direct_result = run_job(jobs, "tuning_direct", direct, tuning, classes, size)
    ablations = []
    def evaluate_exclusion(excluded: list[str]):
        index = len(ablations)
        path = models / f"candidate_{index:02d}.onnx"
        if not path.exists():
            cached_quantize(prepared, path, ranges, excluded)
        details = inspect_int8(path, size, excluded)
        result = run_job(jobs, f"candidate_{index:02d}", path, tuning, classes, size)
        row = {"model": str(path), "excluded": excluded, "map50_95": result["quality"]["map50_95"],
               "map50": result["quality"]["map50"], "mean_ms": result["runtime"]["mean_ms"],
               "gain": result["quality"]["map50_95"] - direct_result["quality"]["map50_95"],
               "details": details}
        row["speed_retention"] = speed_retention(fp_result["runtime"]["mean_ms"], direct_result["runtime"]["mean_ms"], row["mean_ms"])
        ablations.append(row)
        write_json(out / "ablations.json", ablations)
        print(f"candidate {index}: gain {row['gain']*100:+.3f} pp, latency {row['mean_ms']:.2f} ms", flush=True)
    for name in candidates:
        evaluate_exclusion([name])
    positive = sorted([r for r in ablations if r["gain"] > 0], key=lambda r: (-r["gain"], r["mean_ms"], r["excluded"]))
    tested = {tuple(sorted(r["excluded"])) for r in ablations}
    groups = [[r["excluded"][0] for r in positive[:k]] for k in (1,2,4,8)] + [heads]
    for group in groups:
        key = tuple(sorted(group))
        if key and key not in tested:
            tested.add(key)
            evaluate_exclusion(group)
    valid = [r for r in ablations if r["gain"] > 0 and r["speed_retention"] is not None and r["speed_retention"] >= 0.8]
    best = min(valid, key=lambda r: (-r["map50_95"], r["mean_ms"], len(r["excluded"]))) if valid else None
    optimized = models / "optimized_int8.onnx"
    if not optimized.exists():
        shutil.copyfile(best["model"] if best else direct, optimized)
    exclusions = best["excluded"] if best else []
    write_json(out / "excluded_nodes.json", exclusions)
    paths = {"fp32": original, "direct_int8": direct, "optimized_int8": optimized}
    validation = selection["validation"]["images"]
    final = {name: run_job(jobs, f"final_{name}", path, validation, classes, size,
                            manifest=args.manifest.resolve()) for name, path in paths.items()}
    rounds = {name: [] for name in paths}
    for round_index in range(3):
        order = list(paths)[round_index:] + list(paths)[:round_index]
        for name in order:
            rounds[name].append(run_job(jobs, f"benchmark_r{round_index+1}_{name}", paths[name],
                                        validation[:100], classes, size, "benchmark")["runtime"])
    report = {"created_at": datetime.now(ZoneInfo("Asia/Shanghai")).isoformat(),
              "classes": classes, "ablations": ablations, "models": {},
              "versions": {"onnx": onnx.__version__, "onnxruntime": ort.__version__, "openvino": ov.__version__, "numpy": np.__version__},
              "source": {"path": str(args.source.resolve()), "sha256": file_sha256(args.source)},
              "code_sha256": {str(p.relative_to(REPO)): file_sha256(p) for p in (Path(__file__), SCRIPT_DIR / "ptq_int8.py", SCRIPT_DIR / "quantization.py")},
              "command": shlex.join(["conda", "run", "-n", "yolo", "python", "model/ptq_analysis.py",
                  "--source", str(args.source), "--data", str(args.data), "--classes", str(args.classes),
                  "--manifest", str(args.manifest), "--output-dir", "/path/to/new-output"])}
    for name, path in paths.items():
        timing = [t for r in rounds[name] for t in r["samples_ms"]]
        benchmark = {"mean_ms": float(np.mean(timing)), "p95_ms": float(np.percentile(timing,95)),
                     "throughput": 1000 / float(np.mean(timing)), "rss_mib": float(np.mean([r["rss_mib"] for r in rounds[name]])),
                     "rounds": rounds[name]}
        detail = inspect_int8(path, size, exclusions if name == "optimized_int8" else []) if name != "fp32" else {
            "conv_count": 102, "int8_conv_count": 0, "fp32_conv_count": 102, "excluded_nodes": []}
        tune = best if name == "optimized_int8" and best else None
        tr = fp_result if name == "fp32" else direct_result
        report["models"][name] = {"path": str(path), "bytes": path.stat().st_size, "sha256": file_sha256(path),
                                  **detail, "validation": final[name]["quality"], "video": final[name]["video_quality"],
                                  "benchmark": benchmark, "tuning_ms": tune["mean_ms"] if tune else tr["runtime"]["mean_ms"],
                                  "tuning_map50_95": tune["map50_95"] if tune else tr["quality"]["map50_95"]}
    report["speed_retention"] = speed_retention(*[report["models"][n]["benchmark"]["mean_ms"] for n in paths])
    improved = final["optimized_int8"]["quality"]["map50_95"] > final["direct_int8"]["quality"]["map50_95"]
    report["status"] = "达到精度与速度目标" if best and improved and report["speed_retention"] is not None and report["speed_retention"] >= 0.8 else "未同时达到精度与速度目标，详见实测权衡"
    write_json(out / "summary.json", report)
    with (out / "ablations.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["model", "excluded", "map50", "map50_95", "gain", "mean_ms", "speed_retention"])
        writer.writeheader()
        for row in ablations:
            writer.writerow({k: json.dumps(row[k]) if k == "excluded" else row[k] for k in writer.fieldnames})
    with (out / "metrics.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["model", "map50", "map50_95", "precision", "recall", "f1", "mean_ms", "p95_ms", "throughput", "bytes", "rss_mib", "int8_convs", "fp32_convs"])
        for name, row in report["models"].items():
            q, b = row["validation"], row["benchmark"]
            writer.writerow([name, q["map50"], q["map50_95"], *[q["operating_point"][k] for k in ("precision", "recall", "f1")],
                             b["mean_ms"], b["p95_ms"], b["throughput"], row["bytes"], b["rss_mib"], row["int8_conv_count"], row["fp32_conv_count"]])
    finish_report(out)
    print(f"REPORT {out / 'report.md'}", flush=True)


if __name__ == "__main__":
    main()
