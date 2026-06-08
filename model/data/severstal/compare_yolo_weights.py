#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
compare_yolo_weights.py

传入多个 YOLO / YOLO-seg 权重，在同一个 data.yaml 的 val/test 上评估，
输出整体指标对比 CSV、Markdown，以及逐类别 mAP 对比 CSV。

示例：
python compare_yolo_weights.py \
  --weights \
    runs/segment/severstal_yolo11n_seg_pretrained/weights/best.pt \
    runs/segment/severstal_yolo11n_seg_hard_aug_v1/weights/best.pt \
  --names base hard_aug_v1 \
  --data /home/ubuntu/YOLO/model/data/severstal_yolo_seg_hard_aug/data.yaml \
  --splits val test \
  --imgsz 640 256 \
  --batch 96 \
  --device 4,5 \
  --out-dir runs/compare/yolo_seg_compare_v1
"""

import argparse
import csv
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

import yaml


def get_yolo():
    from ultralytics import YOLO
    return YOLO


def safe_float(value: Any, default: Optional[float] = None) -> Optional[float]:
    try:
        if value is None:
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def round_or_none(value: Any, ndigits: int = 6) -> Optional[float]:
    value = safe_float(value)
    if value is None:
        return None
    return round(value, ndigits)


def read_yaml(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def resolve_data_yaml(data: Optional[str], dataset_dir: Optional[str]) -> Path:
    if data:
        data_yaml = Path(data).expanduser().resolve()
    elif dataset_dir:
        data_yaml = Path(dataset_dir).expanduser().resolve() / "data.yaml"
    else:
        raise ValueError("必须提供 --data 或 --dataset-dir")

    if not data_yaml.exists():
        raise FileNotFoundError(f"data.yaml 不存在: {data_yaml}")
    return data_yaml


def parse_imgsz_arg(values: Optional[Sequence[int]]):
    if values is None:
        return 640
    if len(values) == 1:
        return values[0]
    if len(values) == 2:
        width, height = values
        return height, width
    raise ValueError("--imgsz 只支持一个值，或两个值：宽 高，例如 640 256")


def parse_names_from_data_yaml(data_yaml: Path) -> Dict[int, str]:
    data = read_yaml(data_yaml)
    names = data.get("names", {})
    if isinstance(names, dict):
        return {int(k): str(v) for k, v in names.items()}
    if isinstance(names, list):
        return {i: str(v) for i, v in enumerate(names)}
    return {}


def short_weight_name(weight_path: str) -> str:
    p = Path(weight_path)
    if p.parent.name == "weights" and p.parent.parent.name:
        return p.parent.parent.name
    return p.stem


def make_unique_names(weights: Sequence[str]) -> Dict[str, str]:
    used = set()
    mapping = {}
    for w in weights:
        base = short_weight_name(w)
        name = base
        idx = 2
        while name in used:
            name = f"{base}_{idx}"
            idx += 1
        used.add(name)
        mapping[w] = name
    return mapping


def flatten_results_dict(results_dict: Dict[str, Any]) -> Dict[str, Optional[float]]:
    """
    Ultralytics 常见 results_dict key:
      metrics/precision(B), metrics/recall(B), metrics/mAP50(B), metrics/mAP50-95(B)
      metrics/precision(M), metrics/recall(M), metrics/mAP50(M), metrics/mAP50-95(M)
      fitness
    B = box, M = mask。
    """
    key_map = {
        "metrics/precision(B)": "box_precision",
        "metrics/recall(B)": "box_recall",
        "metrics/mAP50(B)": "box_map50",
        "metrics/mAP50-95(B)": "box_map50_95",
        "metrics/precision(M)": "mask_precision",
        "metrics/recall(M)": "mask_recall",
        "metrics/mAP50(M)": "mask_map50",
        "metrics/mAP50-95(M)": "mask_map50_95",
        "fitness": "fitness",
    }
    return {dst: round_or_none(results_dict.get(src)) for src, dst in key_map.items()}


def extract_from_metric_obj(metric_obj: Any, prefix: str) -> Dict[str, Optional[float]]:
    """兼容 metrics.box / metrics.seg。"""
    attr_map = {
        "mp": f"{prefix}_precision",
        "mr": f"{prefix}_recall",
        "map50": f"{prefix}_map50",
        "map75": f"{prefix}_map75",
        "map": f"{prefix}_map50_95",
    }
    return {key: round_or_none(getattr(metric_obj, attr, None)) for attr, key in attr_map.items()}


def extract_speed(metrics: Any) -> Dict[str, Optional[float]]:
    speed = getattr(metrics, "speed", None)
    if not isinstance(speed, dict):
        return {}
    return {
        "speed_preprocess_ms": round_or_none(speed.get("preprocess")),
        "speed_inference_ms": round_or_none(speed.get("inference")),
        "speed_loss_ms": round_or_none(speed.get("loss")),
        "speed_postprocess_ms": round_or_none(speed.get("postprocess")),
    }


def extract_metrics(metrics: Any) -> Dict[str, Optional[float]]:
    """优先 results_dict，缺失时从 metrics.box / metrics.seg 补。"""
    out: Dict[str, Optional[float]] = {}

    results_dict = getattr(metrics, "results_dict", None)
    if isinstance(results_dict, dict):
        out.update(flatten_results_dict(results_dict))

    box_obj = getattr(metrics, "box", None)
    if box_obj is not None:
        for k, v in extract_from_metric_obj(box_obj, "box").items():
            if out.get(k) is None:
                out[k] = v

    seg_obj = getattr(metrics, "seg", None)
    if seg_obj is not None:
        for k, v in extract_from_metric_obj(seg_obj, "mask").items():
            if out.get(k) is None:
                out[k] = v

    out.update(extract_speed(metrics))
    return out


def to_float_list(x: Any) -> List[Optional[float]]:
    if x is None:
        return []
    if hasattr(x, "detach"):
        x = x.detach().cpu().numpy()
    if hasattr(x, "tolist"):
        x = x.tolist()
    if not isinstance(x, list):
        return []
    return [safe_float(v) for v in x]


def extract_per_class_maps(
    metrics: Any,
    model_name: str,
    weight: str,
    split: str,
    names: Dict[int, str],
) -> List[Dict[str, Any]]:
    """
    提取逐类别 mAP50-95。
    注意：Ultralytics 的 .maps 通常是 per-class mAP50-95。
    """
    box_obj = getattr(metrics, "box", None)
    seg_obj = getattr(metrics, "seg", None)

    box_maps = to_float_list(getattr(box_obj, "maps", None) if box_obj is not None else None)
    mask_maps = to_float_list(getattr(seg_obj, "maps", None) if seg_obj is not None else None)

    n = max(len(box_maps), len(mask_maps), len(names))
    rows = []
    for class_id in range(n):
        rows.append({
            "model": model_name,
            "weight": weight,
            "split": split,
            "class_id": class_id,
            "class_name": names.get(class_id, str(class_id)),
            "box_map50_95": round_or_none(box_maps[class_id]) if class_id < len(box_maps) else None,
            "mask_map50_95": round_or_none(mask_maps[class_id]) if class_id < len(mask_maps) else None,
        })
    return rows


def compute_delta_columns(rows: List[Dict[str, Any]], baseline_model: str) -> None:
    metric_keys = [
        "box_precision", "box_recall", "box_map50", "box_map50_95",
        "mask_precision", "mask_recall", "mask_map50", "mask_map50_95",
        "fitness",
    ]

    baseline_by_split: Dict[str, Dict[str, Any]] = {}
    for row in rows:
        if row.get("model") == baseline_model:
            baseline_by_split[row.get("split", "")] = row

    for row in rows:
        base = baseline_by_split.get(row.get("split", ""), {})
        for key in metric_keys:
            cur = safe_float(row.get(key))
            ref = safe_float(base.get(key))
            row[f"delta_{key}"] = None if cur is None or ref is None else round(cur - ref, 6)


def write_csv(path: Path, rows: List[Dict[str, Any]], fieldnames: Optional[List[str]] = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return

    if fieldnames is None:
        fieldnames = []
        seen = set()
        for row in rows:
            for key in row.keys():
                if key not in seen:
                    seen.add(key)
                    fieldnames.append(key)

    with open(path, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def fmt_md(v: Any) -> str:
    if v is None:
        return "-"
    if isinstance(v, float):
        return f"{v:.6f}"
    return str(v)


def markdown_table(rows: List[Dict[str, Any]], columns: List[str]) -> str:
    lines = ["| " + " | ".join(columns) + " |", "| " + " | ".join(["---"] * len(columns)) + " |"]
    for row in rows:
        lines.append("| " + " | ".join(fmt_md(row.get(c)) for c in columns) + " |")
    return "\n".join(lines)


def write_markdown_summary(path: Path, rows: List[Dict[str, Any]], baseline_model: str) -> None:
    columns = [
        "split", "model",
        "mask_map50", "mask_map50_95", "mask_precision", "mask_recall",
        "box_map50", "box_map50_95", "fitness",
        "delta_mask_map50", "delta_mask_map50_95", "delta_mask_recall", "delta_mask_precision",
    ]

    def sort_key(row: Dict[str, Any]):
        return (
            str(row.get("split", "")),
            -(safe_float(row.get("mask_map50_95"), -1.0) or -1.0),
            str(row.get("model", "")),
        )

    sorted_rows = sorted(rows, key=sort_key)
    text = []
    text.append("# YOLO 权重指标对比\n")
    text.append(f"- Baseline: `{baseline_model}`")
    text.append("- `mask_*` 对应 YOLO segmentation mask 指标。")
    text.append("- `box_*` 对应检测框指标。")
    text.append("- `delta_*` 表示相对 baseline 的变化值。\n")
    text.append(markdown_table(sorted_rows, columns))
    text.append("")
    path.write_text("\n".join(text), encoding="utf-8")


def evaluate_one(
    weight: str,
    model_name: str,
    data_yaml: Path,
    split: str,
    args: argparse.Namespace,
    names: Dict[int, str],
) -> Tuple[Dict[str, Any], List[Dict[str, Any]], Dict[str, Any]]:
    YOLO = get_yolo()

    weight_path = Path(weight).expanduser().resolve()
    if not weight_path.exists():
        raise FileNotFoundError(f"权重不存在: {weight_path}")

    print("\n========== Evaluate ==========")
    print(f"model:  {model_name}")
    print(f"weight: {weight_path}")
    print(f"split:  {split}")

    model = YOLO(str(weight_path))
    metrics = model.val(
        data=str(data_yaml),
        split=split,
        imgsz=args.imgsz,
        batch=args.batch,
        device=args.device,
        workers=args.workers,
        conf=args.conf,
        iou=args.iou,
        project=str(args.out_dir / "ultralytics_val"),
        name=f"{model_name}_{split}",
        exist_ok=True,
        plots=args.plots,
        verbose=args.verbose,
    )

    row: Dict[str, Any] = {
        "model": model_name,
        "weight": str(weight_path),
        "split": split,
    }
    row.update(extract_metrics(metrics))

    per_class_rows = extract_per_class_maps(
        metrics=metrics,
        model_name=model_name,
        weight=str(weight_path),
        split=split,
        names=names,
    )

    raw = {
        "model": model_name,
        "weight": str(weight_path),
        "split": split,
        "results_dict": getattr(metrics, "results_dict", {}),
        "speed": getattr(metrics, "speed", {}),
    }
    return row, per_class_rows, raw


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Compare multiple YOLO / YOLO-seg weights on val/test metrics.")

    parser.add_argument("--weights", nargs="+", required=True, help="多个权重路径")
    parser.add_argument("--names", nargs="+", default=None, help="每个权重的显示名，数量必须和 --weights 一致")

    parser.add_argument("--data", type=str, default=None, help="data.yaml 路径")
    parser.add_argument("--dataset-dir", type=str, default=None, help="数据集目录，脚本会使用 dataset-dir/data.yaml")

    parser.add_argument("--splits", nargs="+", default=["val", "test"], choices=["train", "val", "test"])

    parser.add_argument("--imgsz", type=int, nargs="+", default=[640], help="一个值表示方形；两个值按 宽 高，例如 640 256")
    parser.add_argument("--batch", type=int, default=32)
    parser.add_argument("--device", type=str, default="0")
    parser.add_argument("--workers", type=int, default=min(8, os.cpu_count() or 8))
    parser.add_argument("--conf", type=float, default=0.001, help="val 评估置信度，建议保持较低")
    parser.add_argument("--iou", type=float, default=0.7)

    parser.add_argument("--out-dir", type=Path, default=Path("runs/compare/yolo_weights_compare"))
    parser.add_argument("--plots", action="store_true", help="保存 PR 曲线、混淆矩阵等图")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--continue-on-error", action="store_true", help="某个权重失败时继续评估其他权重")

    return parser


def main(argv: Optional[Sequence[str]] = None) -> None:
    args = build_parser().parse_args(argv)
    args.imgsz = parse_imgsz_arg(args.imgsz)
    args.out_dir = Path(args.out_dir).expanduser().resolve()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    data_yaml = resolve_data_yaml(args.data, args.dataset_dir)
    class_names = parse_names_from_data_yaml(data_yaml)

    if args.names is not None and len(args.names) != len(args.weights):
        raise ValueError("--names 的数量必须和 --weights 一致")

    weight_to_name = {w: n for w, n in zip(args.weights, args.names)} if args.names else make_unique_names(args.weights)
    baseline_model = weight_to_name[args.weights[0]]

    summary_rows: List[Dict[str, Any]] = []
    per_class_rows: List[Dict[str, Any]] = []
    raw_results: List[Dict[str, Any]] = []

    for split in args.splits:
        for weight in args.weights:
            model_name = weight_to_name[weight]
            try:
                row, cls_rows, raw = evaluate_one(
                    weight=weight,
                    model_name=model_name,
                    data_yaml=data_yaml,
                    split=split,
                    args=args,
                    names=class_names,
                )
                summary_rows.append(row)
                per_class_rows.extend(cls_rows)
                raw_results.append(raw)
            except Exception as exc:
                if not args.continue_on_error:
                    raise
                print(f"[ERROR] weight={weight}, split={split}, error={exc}", file=sys.stderr)
                summary_rows.append({
                    "model": model_name,
                    "weight": str(Path(weight).expanduser()),
                    "split": split,
                    "error": str(exc),
                })

    compute_delta_columns(summary_rows, baseline_model=baseline_model)

    summary_fields = [
        "model", "weight", "split", "error",
        "box_precision", "box_recall", "box_map50", "box_map50_95",
        "mask_precision", "mask_recall", "mask_map50", "mask_map50_95",
        "fitness",
        "delta_box_precision", "delta_box_recall", "delta_box_map50", "delta_box_map50_95",
        "delta_mask_precision", "delta_mask_recall", "delta_mask_map50", "delta_mask_map50_95",
        "delta_fitness",
        "speed_preprocess_ms", "speed_inference_ms", "speed_loss_ms", "speed_postprocess_ms",
    ]

    summary_csv = args.out_dir / "metrics_summary.csv"
    per_class_csv = args.out_dir / "per_class_map.csv"
    summary_md = args.out_dir / "metrics_summary.md"
    raw_yaml = args.out_dir / "raw_results_dict.yaml"

    write_csv(summary_csv, summary_rows, fieldnames=summary_fields)
    write_csv(per_class_csv, per_class_rows)
    write_markdown_summary(summary_md, summary_rows, baseline_model=baseline_model)

    with open(raw_yaml, "w", encoding="utf-8") as f:
        yaml.safe_dump(raw_results, f, allow_unicode=True, sort_keys=False)

    print("\n========== Compare Finished ==========")
    print(f"data_yaml:      {data_yaml}")
    print(f"baseline:       {baseline_model}")
    print(f"summary_csv:    {summary_csv}")
    print(f"summary_md:     {summary_md}")
    print(f"per_class_csv:  {per_class_csv}")
    print(f"raw_yaml:       {raw_yaml}")

    print("\n========== Quick Summary ==========")
    quick_cols = [
        "split", "model",
        "mask_map50", "mask_map50_95", "mask_precision", "mask_recall",
        "delta_mask_map50", "delta_mask_map50_95",
    ]
    print(markdown_table(summary_rows, quick_cols))


if __name__ == "__main__":
    main()
