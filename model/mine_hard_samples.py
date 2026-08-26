from __future__ import annotations

import argparse
import csv
import math
from collections import Counter
from pathlib import Path
from typing import Iterable

import yaml


ROOT = Path(__file__).resolve().parent
DEFAULT_DATASET_DIR = ROOT / "data" / "bdd100k_yolo_det"
DEFAULT_IMGSZ = (384, 640)
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}
BDD100K_NAMES = (
    "person",
    "rider",
    "car",
    "truck",
    "bus",
    "train",
    "motor",
    "bike",
    "traffic light",
    "traffic sign",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Mine hard BDD100K train images with a YOLO detection model.",
    )
    parser.add_argument("--weights", required=True, help="第一阶段 best.pt")
    parser.add_argument(
        "--dataset-dir",
        default=str(DEFAULT_DATASET_DIR),
        help="BDD100K YOLO 数据集根目录；脚本固定只扫描 images/train",
    )
    parser.add_argument(
        "--output-dir",
        help="输出目录；默认位于权重所属 run 下的 hard_samples",
    )
    parser.add_argument(
        "--imgsz",
        nargs=2,
        type=int,
        default=DEFAULT_IMGSZ,
        metavar=("HEIGHT", "WIDTH"),
    )
    parser.add_argument("--confidence", type=float, default=0.25)
    parser.add_argument("--match-iou", type=float, default=0.5)
    parser.add_argument("--batch", type=int, default=32)
    parser.add_argument("--device", default="0")
    parser.add_argument("--top-ratio", type=float, default=0.2)
    parser.add_argument("--max-samples", type=int, default=15_000)
    parser.add_argument("--tail-frequency-threshold", type=float, default=0.05)
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if args.imgsz[0] <= 0 or args.imgsz[1] <= 0:
        raise ValueError("--imgsz 的高度和宽度必须为正整数")
    if not 0.0 <= args.confidence <= 1.0:
        raise ValueError("--confidence 必须在 [0, 1] 范围内")
    if not 0.0 < args.match_iou <= 1.0:
        raise ValueError("--match-iou 必须在 (0, 1] 范围内")
    if args.batch < 1:
        raise ValueError("--batch 不能小于 1")
    if not 0.0 < args.top_ratio <= 1.0:
        raise ValueError("--top-ratio 必须在 (0, 1] 范围内")
    if args.max_samples < 1:
        raise ValueError("--max-samples 不能小于 1")
    if not 0.0 < args.tail_frequency_threshold <= 1.0:
        raise ValueError("--tail-frequency-threshold 必须在 (0, 1] 范围内")


def image_files(image_dir: Path) -> list[Path]:
    if not image_dir.is_dir():
        raise FileNotFoundError(f"train 图片目录不存在: {image_dir}")
    images = sorted(
        path.resolve()
        for path in image_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    )
    if not images:
        raise FileNotFoundError(f"train 图片目录为空: {image_dir}")
    return images


def label_path_for_image(image_path: Path, train_dir: Path, label_dir: Path) -> Path:
    relative_path = image_path.relative_to(train_dir)
    return label_dir / relative_path.with_suffix(".txt")


def read_yolo_labels(
    label_path: Path,
    image_width: int,
    image_height: int,
) -> list[tuple[int, tuple[float, float, float, float]]]:
    boxes = []
    if not label_path.is_file():
        raise FileNotFoundError(f"train 图片缺少对应标签: {label_path}")

    with label_path.open("r", encoding="utf-8") as f:
        for line_number, raw_line in enumerate(f, start=1):
            line = raw_line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 5:
                raise ValueError(f"标签字段数量错误: {label_path}:{line_number}")
            try:
                class_id = int(parts[0])
                center_x, center_y, width, height = (float(value) for value in parts[1:])
            except ValueError as exc:
                raise ValueError(f"标签数值解析失败: {label_path}:{line_number}") from exc

            if not 0 <= class_id < len(BDD100K_NAMES):
                raise ValueError(f"标签类别越界: {label_path}:{line_number}")
            if width <= 0.0 or height <= 0.0:
                raise ValueError(f"标签宽高非法: {label_path}:{line_number}")

            x1 = (center_x - width / 2.0) * image_width
            y1 = (center_y - height / 2.0) * image_height
            x2 = (center_x + width / 2.0) * image_width
            y2 = (center_y + height / 2.0) * image_height
            boxes.append((class_id, (x1, y1, x2, y2)))

    return boxes


def read_label_classes(label_path: Path) -> set[int]:
    classes = set()
    if not label_path.is_file():
        raise FileNotFoundError(f"train 图片缺少对应标签: {label_path}")
    with label_path.open("r", encoding="utf-8") as f:
        for line_number, raw_line in enumerate(f, start=1):
            parts = raw_line.strip().split()
            if not parts:
                continue
            if len(parts) != 5:
                raise ValueError(f"标签字段数量错误: {label_path}:{line_number}")
            try:
                class_id = int(parts[0])
            except ValueError as exc:
                raise ValueError(f"标签类别解析失败: {label_path}:{line_number}") from exc
            if not 0 <= class_id < len(BDD100K_NAMES):
                raise ValueError(f"标签类别越界: {label_path}:{line_number}")
            classes.add(class_id)
    return classes


def class_statistics(
    images: Iterable[Path],
    train_dir: Path,
    label_dir: Path,
    frequency_threshold: float,
) -> tuple[dict[int, int], dict[int, float], dict[int, float]]:
    image_counts = Counter()
    image_total = 0
    for image_path in images:
        image_total += 1
        label_path = label_path_for_image(image_path, train_dir, label_dir)
        image_counts.update(read_label_classes(label_path))

    frequencies = {
        class_id: image_counts[class_id] / image_total
        for class_id in range(len(BDD100K_NAMES))
    }
    tail_weights = {}
    for class_id, frequency in frequencies.items():
        if frequency <= 0.0:
            weight = 3.0
        else:
            weight = math.sqrt(frequency_threshold / frequency)
            weight = min(3.0, max(1.0, weight))
        tail_weights[class_id] = weight

    return dict(image_counts), frequencies, tail_weights


def intersection_over_union(
    first: tuple[float, float, float, float],
    second: tuple[float, float, float, float],
) -> float:
    intersection_width = max(0.0, min(first[2], second[2]) - max(first[0], second[0]))
    intersection_height = max(0.0, min(first[3], second[3]) - max(first[1], second[1]))
    intersection = intersection_width * intersection_height
    first_area = max(0.0, first[2] - first[0]) * max(0.0, first[3] - first[1])
    second_area = max(0.0, second[2] - second[0]) * max(0.0, second[3] - second[1])
    union = first_area + second_area - intersection
    return intersection / union if union > 0.0 else 0.0


def greedy_match(
    ground_truth: list[tuple[int, tuple[float, float, float, float]]],
    predictions: list[tuple[int, tuple[float, float, float, float]]],
    iou_threshold: float,
) -> tuple[list[float], list[int], list[int]]:
    candidates = []
    for gt_index, (gt_class, gt_box) in enumerate(ground_truth):
        for pred_index, (pred_class, pred_box) in enumerate(predictions):
            if gt_class != pred_class:
                continue
            iou = intersection_over_union(gt_box, pred_box)
            if iou >= iou_threshold:
                candidates.append((iou, gt_index, pred_index))

    candidates.sort(reverse=True)
    matched_gt = set()
    matched_predictions = set()
    matched_ious = []
    for iou, gt_index, pred_index in candidates:
        if gt_index in matched_gt or pred_index in matched_predictions:
            continue
        matched_gt.add(gt_index)
        matched_predictions.add(pred_index)
        matched_ious.append(iou)

    unmatched_gt = [index for index in range(len(ground_truth)) if index not in matched_gt]
    unmatched_predictions = [
        index for index in range(len(predictions)) if index not in matched_predictions
    ]
    return matched_ious, unmatched_gt, unmatched_predictions


def predictions_from_result(result) -> list[tuple[int, tuple[float, float, float, float]]]:
    if result.boxes is None or len(result.boxes) == 0:
        return []
    class_ids = result.boxes.cls.cpu().tolist()
    coordinates = result.boxes.xyxy.cpu().tolist()
    return [
        (int(class_id), tuple(float(value) for value in box))
        for class_id, box in zip(class_ids, coordinates)
    ]


def score_result(
    image_path: Path,
    ground_truth: list[tuple[int, tuple[float, float, float, float]]],
    predictions: list[tuple[int, tuple[float, float, float, float]]],
    tail_weights: dict[int, float],
    frequencies: dict[int, float],
    frequency_threshold: float,
    iou_threshold: float,
) -> dict:
    matched_ious, unmatched_gt, unmatched_predictions = greedy_match(
        ground_truth,
        predictions,
        iou_threshold,
    )
    missed_score = sum(3.0 * tail_weights[ground_truth[index][0]] for index in unmatched_gt)
    false_positive_score = float(len(unmatched_predictions))
    localization_score = sum(1.0 - iou for iou in matched_ious)
    score = (missed_score + false_positive_score + localization_score) / math.sqrt(
        max(1, len(ground_truth))
    )
    rare_classes = sorted(
        {
            BDD100K_NAMES[class_id]
            for class_id, _ in ground_truth
            if frequencies[class_id] < frequency_threshold
        }
    )
    average_iou = sum(matched_ious) / len(matched_ious) if matched_ious else 0.0
    return {
        "path": str(image_path),
        "score": score,
        "gt_count": len(ground_truth),
        "prediction_count": len(predictions),
        "false_negatives": len(unmatched_gt),
        "false_positives": len(unmatched_predictions),
        "average_iou": average_iou,
        "rare_classes": ";".join(rare_classes),
    }


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def write_outputs(
    output_dir: Path,
    records: list[dict],
    selected: list[dict],
    args: argparse.Namespace,
    weights_path: Path,
    dataset_dir: Path,
    image_counts: dict[int, int],
    frequencies: dict[int, float],
    tail_weights: dict[int, float],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    txt_path = output_dir / "hard_samples.txt"
    csv_path = output_dir / "hard_samples.csv"
    summary_path = output_dir / "hard_sample_summary.yaml"

    with txt_path.open("w", encoding="utf-8") as f:
        for record in selected:
            f.write(f"{record['path']}\n")

    fieldnames = (
        "path",
        "score",
        "gt_count",
        "prediction_count",
        "false_negatives",
        "false_positives",
        "average_iou",
        "rare_classes",
    )
    with csv_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for record in selected:
            row = dict(record)
            row["score"] = f"{record['score']:.8f}"
            row["average_iou"] = f"{record['average_iou']:.8f}"
            writer.writerow(row)

    positive_scores = [record["score"] for record in records if record["score"] > 0.0]
    class_summary = {
        f"{class_id}:{BDD100K_NAMES[class_id]}": {
            "train_image_count": int(image_counts.get(class_id, 0)),
            "image_frequency": round(frequencies[class_id], 8),
            "tail_weight": round(tail_weights[class_id], 6),
        }
        for class_id in range(len(BDD100K_NAMES))
    }
    summary = {
        "weights": str(weights_path),
        "dataset_dir": str(dataset_dir),
        "scan_split": "train",
        "imgsz": list(args.imgsz),
        "rect": False,
        "confidence": args.confidence,
        "batch": args.batch,
        "match_iou": args.match_iou,
        "top_ratio": args.top_ratio,
        "max_samples": args.max_samples,
        "tail_frequency_threshold": args.tail_frequency_threshold,
        "scanned_images": len(records),
        "positive_score_images": len(positive_scores),
        "selected_images": len(selected),
        "selection_score_threshold": round(selected[-1]["score"], 8) if selected else None,
        "score_distribution": {
            "minimum": min(positive_scores) if positive_scores else None,
            "p50": percentile(positive_scores, 0.5),
            "p90": percentile(positive_scores, 0.9),
            "p95": percentile(positive_scores, 0.95),
            "maximum": max(positive_scores) if positive_scores else None,
        },
        "classes": class_summary,
        "outputs": {
            "train_list": str(txt_path),
            "details_csv": str(csv_path),
        },
    }
    with summary_path.open("w", encoding="utf-8") as f:
        yaml.safe_dump(summary, f, allow_unicode=True, sort_keys=False)

    print(f"hard samples: {txt_path}")
    print(f"details: {csv_path}")
    print(f"summary: {summary_path}")
    print(f"selected: {len(selected)} / {len(records)}")


def main() -> None:
    args = parse_args()
    validate_args(args)

    weights_path = Path(args.weights).expanduser().resolve()
    if not weights_path.is_file():
        raise FileNotFoundError(f"权重不存在: {weights_path}")
    dataset_dir = Path(args.dataset_dir).expanduser().resolve()
    train_dir = (dataset_dir / "images" / "train").resolve()
    label_dir = (dataset_dir / "labels" / "train").resolve()
    if not label_dir.is_dir():
        raise FileNotFoundError(f"train 标签目录不存在: {label_dir}")
    output_dir = (
        Path(args.output_dir).expanduser().resolve()
        if args.output_dir
        else weights_path.parent.parent / "hard_samples"
    )

    images = image_files(train_dir)
    image_counts, frequencies, tail_weights = class_statistics(
        images,
        train_dir,
        label_dir,
        args.tail_frequency_threshold,
    )

    from ultralytics import YOLO

    model = YOLO(str(weights_path))
    results = model.predict(
        source=str(train_dir),
        imgsz=(int(args.imgsz[0]), int(args.imgsz[1])),
        rect=False,
        conf=args.confidence,
        batch=args.batch,
        device=args.device,
        stream=True,
        save=False,
        verbose=False,
    )

    train_image_set = set(images)
    records = []
    for result in results:
        image_path = Path(result.path).resolve()
        if image_path not in train_image_set:
            raise RuntimeError(f"模型返回了非 train 图片: {image_path}")
        image_height, image_width = result.orig_shape
        label_path = label_path_for_image(image_path, train_dir, label_dir)
        ground_truth = read_yolo_labels(label_path, image_width, image_height)
        predictions = predictions_from_result(result)
        records.append(
            score_result(
                image_path,
                ground_truth,
                predictions,
                tail_weights,
                frequencies,
                args.tail_frequency_threshold,
                args.match_iou,
            )
        )

    if len(records) != len(images):
        raise RuntimeError(f"推理结果数量不一致: expected={len(images)}, actual={len(records)}")

    positive_records = [record for record in records if record["score"] > 0.0]
    positive_records.sort(key=lambda record: (-record["score"], record["path"]))
    selected_count = min(
        args.max_samples,
        int(math.ceil(len(positive_records) * args.top_ratio)),
    )
    selected = positive_records[:selected_count]
    write_outputs(
        output_dir,
        records,
        selected,
        args,
        weights_path,
        dataset_dir,
        image_counts,
        frequencies,
        tail_weights,
    )


if __name__ == "__main__":
    main()
