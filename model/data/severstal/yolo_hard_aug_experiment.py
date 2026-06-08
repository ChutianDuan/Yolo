#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
YOLO-seg 二阶段实验脚本：Hard Example Mining + 均衡采样 + 缺陷 Copy-Paste + hard_aug 数据集构建。

适用目录结构：
  dataset/
    images/train/*.jpg|png|...
    labels/train/*.txt      # YOLO segmentation label: cls x1 y1 x2 y2 ...
    images/val/*
    labels/val/*
    images/test/*
    labels/test/*
    data.yaml

推荐使用：
  1) 挖 hard 样本：
     python yolo_hard_aug_experiment.py mine \
       --model runs/segment/severstal_yolo11n_seg_pretrained/weights/best.pt \
       --dataset-dir /home/ubuntu/YOLO/model/data/severstal_yolo_seg_sliced_640x256 \
       --split train \
       --out hard_train.csv \
       --device 4,5

  2) 构建二阶段增强数据集：
     python yolo_hard_aug_experiment.py build \
       --dataset-dir /home/ubuntu/YOLO/model/data/severstal_yolo_seg_sliced_640x256 \
       --hard-csv hard_train.csv \
       --out-dir /home/ubuntu/YOLO/model/data/severstal_yolo_seg_hard_aug \
       --copy-mode copy \
       --photometric \
       --copy-paste-per-class 300

  3) 用原训练脚本从 base best.pt 低学习率 fine-tune：
     python train.py \
       --model runs/segment/severstal_yolo11n_seg_pretrained/weights/best.pt \
       --dataset-dir /home/ubuntu/YOLO/model/data/severstal_yolo_seg_hard_aug \
       --name severstal_yolo11n_seg_hard_aug_v1 \
       --epochs 50 \
       --lr0 0.0002 \
       --remove-cache

说明：
- build 只增强 train，val/test 原样复制，避免验证集污染。
- 普通重复样本可以 hardlink/symlink/copy；几何不变的光照噪声增强会保存新图片，标签原样复制。
- copy-paste 会同步更新 YOLO segmentation polygon 标签。
- 该脚本尽量只依赖 pillow/numpy/pyyaml；mine 阶段需要 ultralytics。
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import random
import shutil
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import numpy as np
except ModuleNotFoundError:  # build/copy-paste can run without numpy; mine still requires it.
    np = None

import yaml
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}
SPLITS = ("train", "val", "test")


@dataclass
class Segment:
    cls: int
    points: List[Tuple[float, float]]  # normalized points in [0, 1]


@dataclass
class PredSegment:
    cls: int
    conf: float
    points_px: List[Tuple[float, float]]  # pixel points from ultralytics masks.xy


# -----------------------------
# 基础 IO
# -----------------------------

def image_files(image_dir: Path) -> List[Path]:
    if not image_dir.exists():
        return []
    return sorted(p for p in image_dir.iterdir() if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES)


def label_path_for_image(dataset_dir: Path, split: str, image_path: Path) -> Path:
    return dataset_dir / "labels" / split / f"{image_path.stem}.txt"


def read_yolo_segments(label_path: Path) -> List[Segment]:
    segments: List[Segment] = []
    if not label_path.exists():
        return segments

    with open(label_path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 7 or len(parts) % 2 == 0:
                continue
            try:
                cls = int(parts[0])
                coords = [float(x) for x in parts[1:]]
            except ValueError:
                continue
            points = list(zip(coords[0::2], coords[1::2]))
            if len(points) >= 3:
                segments.append(Segment(cls=cls, points=points))
    return segments


def write_yolo_segments(label_path: Path, segments: Sequence[Segment]) -> None:
    label_path.parent.mkdir(parents=True, exist_ok=True)
    with open(label_path, "w", encoding="utf-8") as f:
        for seg in segments:
            coords = []
            for x, y in seg.points:
                x = min(1.0, max(0.0, float(x)))
                y = min(1.0, max(0.0, float(y)))
                coords.extend([x, y])
            coord_str = " ".join(f"{v:.6f}" for v in coords)
            f.write(f"{seg.cls} {coord_str}\n")


def copy_file(src: Path, dst: Path, mode: str = "copy") -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() or dst.is_symlink():
        dst.unlink()
    if mode == "symlink":
        os.symlink(src.resolve(), dst)
    elif mode == "hardlink":
        os.link(src, dst)
    elif mode == "copy":
        shutil.copy2(src, dst)
    else:
        raise ValueError(f"Unknown copy mode: {mode}")


def copy_label_or_empty(src_label: Path, dst_label: Path, mode: str = "copy") -> None:
    dst_label.parent.mkdir(parents=True, exist_ok=True)
    if src_label.exists():
        copy_file(src_label, dst_label, mode=mode)
    else:
        dst_label.write_text("", encoding="utf-8")


def load_data_yaml(dataset_dir: Path) -> dict:
    data_yaml = dataset_dir / "data.yaml"
    if data_yaml.exists():
        with open(data_yaml, "r", encoding="utf-8") as f:
            return yaml.safe_load(f) or {}
    return {}


def normalize_names(raw_names) -> Dict[int, str]:
    if isinstance(raw_names, dict):
        return {int(k): str(v) for k, v in raw_names.items()}
    if isinstance(raw_names, list):
        return {i: str(v) for i, v in enumerate(raw_names)}
    return {}


def write_data_yaml_like(src_dataset: Path, out_dataset: Path) -> None:
    src = load_data_yaml(src_dataset)
    names = normalize_names(src.get("names", {}))
    nc = int(src.get("nc", len(names) if names else 0))
    if not names and nc > 0:
        names = {i: f"class_{i}" for i in range(nc)}

    data = {
        "path": str(out_dataset.resolve()),
        "train": "images/train",
        "val": "images/val",
        "nc": nc,
        "names": names,
        "task": "segment",
    }
    if (out_dataset / "images" / "test").exists():
        data["test"] = "images/test"

    with open(out_dataset / "data.yaml", "w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, allow_unicode=True, sort_keys=False)


# -----------------------------
# 几何与 mask 工具
# -----------------------------

def norm_to_pixel(points: Sequence[Tuple[float, float]], width: int, height: int) -> List[Tuple[float, float]]:
    return [(float(x) * width, float(y) * height) for x, y in points]


def pixel_to_norm(points: Sequence[Tuple[float, float]], width: int, height: int) -> List[Tuple[float, float]]:
    return [(float(x) / max(width, 1), float(y) / max(height, 1)) for x, y in points]


def polygon_area(points: Sequence[Tuple[float, float]]) -> float:
    if len(points) < 3:
        return 0.0
    area = 0.0
    prev_x, prev_y = points[-1]
    for x, y in points:
        area += prev_x * y - x * prev_y
        prev_x, prev_y = x, y
    return abs(area) * 0.5


def rasterize_polygon(points_px: Sequence[Tuple[float, float]], width: int, height: int) -> np.ndarray:
    if np is None:
        raise RuntimeError("numpy 未安装，无法执行 hard example mining。")
    mask = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(mask)
    pts = [(int(round(x)), int(round(y))) for x, y in points_px]
    if len(pts) >= 3:
        draw.polygon(pts, fill=1)
    return np.asarray(mask, dtype=bool)


def mask_iou(mask_a: np.ndarray, mask_b: np.ndarray) -> float:
    inter = np.logical_and(mask_a, mask_b).sum()
    union = np.logical_or(mask_a, mask_b).sum()
    if union <= 0:
        return 0.0
    return float(inter / union)


# -----------------------------
# mine：用 base 模型挖 hard 样本
# -----------------------------

def predict_segments(model, image_path: Path, imgsz: int, conf: float, device: str, max_det: int) -> List[PredSegment]:
    if np is None:
        raise RuntimeError("numpy 未安装，无法执行 hard example mining。")

    result = model.predict(
        source=str(image_path),
        imgsz=imgsz,
        conf=conf,
        device=device,
        max_det=max_det,
        verbose=False,
    )[0]

    if result.masks is None or result.boxes is None:
        return []

    classes = result.boxes.cls.detach().cpu().numpy().astype(int).tolist()
    confs = result.boxes.conf.detach().cpu().numpy().astype(float).tolist()
    polygons = result.masks.xy  # list[array[N,2]] in original image pixel coords

    preds: List[PredSegment] = []
    for cls, score, xy in zip(classes, confs, polygons):
        pts = [(float(x), float(y)) for x, y in np.asarray(xy).reshape(-1, 2)]
        if len(pts) >= 3:
            preds.append(PredSegment(cls=cls, conf=score, points_px=pts))
    return preds


def analyze_one_image(
    gt_segments: Sequence[Segment],
    pred_segments: Sequence[PredSegment],
    width: int,
    height: int,
    iou_thr: float,
    low_iou_thr: float,
    low_conf_thr: float,
    small_area_ratio: float,
) -> Dict[str, object]:
    gt_masks = [rasterize_polygon(norm_to_pixel(s.points, width, height), width, height) for s in gt_segments]
    pred_masks = [rasterize_polygon(p.points_px, width, height) for p in pred_segments]

    pairs: List[Tuple[float, int, int]] = []
    for gi, gm in enumerate(gt_masks):
        for pi, pm in enumerate(pred_masks):
            pairs.append((mask_iou(gm, pm), gi, pi))
    pairs.sort(reverse=True, key=lambda x: x[0])

    matched_gt = set()
    matched_pred = set()

    correct = 0
    wrong_class = 0
    low_conf_gt = 0
    matched_low_iou = 0

    for iou, gi, pi in pairs:
        if iou < iou_thr:
            continue
        if gi in matched_gt or pi in matched_pred:
            continue
        matched_gt.add(gi)
        matched_pred.add(pi)
        if gt_segments[gi].cls == pred_segments[pi].cls:
            correct += 1
            if pred_segments[pi].conf < low_conf_thr:
                low_conf_gt += 1
        else:
            wrong_class += 1

    fn = len(gt_segments) - len(matched_gt)
    fp = len(pred_segments) - len(matched_pred)

    # 对未匹配 GT，统计是否存在同类但 IoU 不够的预测，说明边界/定位差。
    for gi, gt in enumerate(gt_segments):
        if gi in matched_gt:
            continue
        best_same_cls = 0.0
        for pi, pred in enumerate(pred_segments):
            if pred.cls != gt.cls:
                continue
            if pi < len(pred_masks):
                best_same_cls = max(best_same_cls, mask_iou(gt_masks[gi], pred_masks[pi]))
        if low_iou_thr <= best_same_cls < iou_thr:
            matched_low_iou += 1

    small_defects = 0
    for gt in gt_segments:
        area_norm = polygon_area(gt.points)  # normalized area ratio roughly in [0, 1]
        if area_norm < small_area_ratio:
            small_defects += 1

    score = (
        3.0 * fn
        + 2.0 * fp
        + 2.0 * wrong_class
        + 1.5 * matched_low_iou
        + 1.0 * small_defects
        + 1.0 * low_conf_gt
    )

    tags = []
    if fn > 0:
        tags.append("FN")
    if fp > 0:
        tags.append("FP")
    if wrong_class > 0:
        tags.append("WRONG_CLASS")
    if matched_low_iou > 0:
        tags.append("LOW_IOU")
    if small_defects > 0:
        tags.append("SMALL")
    if low_conf_gt > 0:
        tags.append("LOW_CONF")
    if not gt_segments and fp > 0:
        tags.append("HARD_NEGATIVE")

    return {
        "gt_count": len(gt_segments),
        "pred_count": len(pred_segments),
        "correct": correct,
        "fn": fn,
        "fp": fp,
        "wrong_class": wrong_class,
        "low_iou": matched_low_iou,
        "small_defects": small_defects,
        "low_conf_gt": low_conf_gt,
        "hard_score": score,
        "hard_tags": ";".join(tags),
        "gt_classes": ";".join(str(s.cls) for s in gt_segments),
        "pred_classes": ";".join(str(p.cls) for p in pred_segments),
    }


def command_mine(args: argparse.Namespace) -> None:
    if np is None:
        raise RuntimeError("numpy 未安装，无法执行 hard example mining。")

    from ultralytics import YOLO

    dataset_dir = Path(args.dataset_dir)
    image_dir = dataset_dir / "images" / args.split
    images = image_files(image_dir)
    if args.limit > 0:
        images = images[: args.limit]

    if not images:
        raise FileNotFoundError(f"No images found: {image_dir}")

    model = YOLO(args.model)
    rows = []
    class_counter = Counter()

    print(f"[mine] split={args.split}, images={len(images)}")
    for idx, image_path in enumerate(images, 1):
        label_path = label_path_for_image(dataset_dir, args.split, image_path)
        gt_segments = read_yolo_segments(label_path)
        class_counter.update(s.cls for s in gt_segments)

        with Image.open(image_path) as img:
            width, height = img.size

        pred_segments = predict_segments(
            model=model,
            image_path=image_path,
            imgsz=args.imgsz,
            conf=args.conf,
            device=args.device,
            max_det=args.max_det,
        )
        stat = analyze_one_image(
            gt_segments=gt_segments,
            pred_segments=pred_segments,
            width=width,
            height=height,
            iou_thr=args.iou_thr,
            low_iou_thr=args.low_iou_thr,
            low_conf_thr=args.low_conf_thr,
            small_area_ratio=args.small_area_ratio,
        )
        stat.update({
            "split": args.split,
            "image": str(image_path),
            "label": str(label_path),
            "stem": image_path.stem,
            "width": width,
            "height": height,
        })
        rows.append(stat)

        if idx % args.log_interval == 0 or idx == len(images):
            hard = sum(1 for r in rows if float(r["hard_score"]) > 0)
            print(f"  processed {idx}/{len(images)}, hard={hard}")

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "split", "image", "label", "stem", "width", "height",
        "gt_count", "pred_count", "correct", "fn", "fp", "wrong_class",
        "low_iou", "small_defects", "low_conf_gt", "hard_score",
        "hard_tags", "gt_classes", "pred_classes",
    ]
    with open(out_path, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    hard_rows = [r for r in rows if float(r["hard_score"]) > 0]
    print("\n[mine finished]")
    print(f"csv: {out_path}")
    print(f"total images: {len(rows)}")
    print(f"hard images: {len(hard_rows)}")
    print(f"class distribution: {dict(sorted(class_counter.items()))}")
    print("top hard samples:")
    for r in sorted(rows, key=lambda x: float(x["hard_score"]), reverse=True)[:10]:
        print(f"  score={r['hard_score']:.1f}, tags={r['hard_tags']}, image={r['image']}")


# -----------------------------
# build：构造 hard_aug 数据集
# -----------------------------

def read_hard_csv(path: Optional[str]) -> Dict[str, dict]:
    if not path:
        return {}
    csv_path = Path(path)
    if not csv_path.exists():
        raise FileNotFoundError(csv_path)
    result: Dict[str, dict] = {}
    with open(csv_path, "r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            result[row["stem"]] = row
    return result


def train_class_distribution(dataset_dir: Path) -> Counter:
    counter = Counter()
    for img in image_files(dataset_dir / "images" / "train"):
        segs = read_yolo_segments(label_path_for_image(dataset_dir, "train", img))
        counter.update(s.cls for s in segs)
    return counter


def image_classes(dataset_dir: Path, split: str, image_path: Path) -> List[int]:
    return [s.cls for s in read_yolo_segments(label_path_for_image(dataset_dir, split, image_path))]


def compute_class_repeat(class_counter: Counter, max_class_repeat: int) -> Dict[int, int]:
    if not class_counter:
        return {}
    max_count = max(class_counter.values())
    repeats = {}
    for cls, count in class_counter.items():
        if count <= 0:
            repeats[cls] = max_class_repeat
        else:
            # sqrt 比例比线性比例更保守，避免少数类被复制到过拟合。
            rep = int(math.ceil(math.sqrt(max_count / count)))
            repeats[cls] = max(1, min(max_class_repeat, rep))
    return repeats


def hard_repeat_from_row(row: Optional[dict], args: argparse.Namespace) -> int:
    if not row:
        return 1
    score = float(row.get("hard_score", 0.0) or 0.0)
    gt_count = int(float(row.get("gt_count", 0) or 0))
    fp = int(float(row.get("fp", 0) or 0))

    if score >= args.strong_score:
        return args.strong_repeat
    if score >= args.medium_score:
        return args.medium_repeat
    if gt_count == 0 and fp > 0:
        return args.negative_repeat
    return 1


def hard_negative_repeat_from_row(row: Optional[dict], args: argparse.Namespace) -> int:
    if not row:
        return 1

    gt_count = int(float(row.get("gt_count", 0) or 0))
    fp = int(float(row.get("fp", 0) or 0))
    tags = set(str(row.get("hard_tags", "")).split(";"))

    if gt_count == 0 and (fp > 0 or "HARD_NEGATIVE" in tags):
        return max(1, args.negative_repeat)
    return 1


def random_photometric_aug(img: Image.Image, rng: random.Random) -> Image.Image:
    """几何不变增强：不会改变 label polygon。"""
    img = img.convert("RGB")

    # 亮度/对比度小幅扰动，适合工业缺陷低对比场景。
    if rng.random() < 0.85:
        img = ImageEnhance.Brightness(img).enhance(rng.uniform(0.85, 1.15))
    if rng.random() < 0.85:
        img = ImageEnhance.Contrast(img).enhance(rng.uniform(0.85, 1.20))
    if rng.random() < 0.35:
        img = ImageEnhance.Sharpness(img).enhance(rng.uniform(0.8, 1.4))

    # 轻微 blur。
    if rng.random() < 0.20:
        img = img.filter(ImageFilter.GaussianBlur(radius=rng.uniform(0.2, 0.8)))

    # 轻微高斯噪声。
    if np is not None and rng.random() < 0.35:
        arr = np.asarray(img).astype(np.float32)
        sigma = rng.uniform(2.0, 8.0)
        noise = rng.normalvariate(0, sigma)
        # 每次生成整图随机噪声，不只用一个标量。
        arr += np.random.normal(0, sigma, size=arr.shape).astype(np.float32)
        arr = np.clip(arr, 0, 255).astype(np.uint8)
        img = Image.fromarray(arr)

    return img


def save_image_with_original_suffix(img: Image.Image, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    suffix = out_path.suffix.lower()
    if suffix in {".jpg", ".jpeg"}:
        img.save(out_path, quality=95)
    else:
        img.save(out_path)


def copy_split_unchanged(src_dataset: Path, out_dataset: Path, split: str, mode: str) -> None:
    src_img_dir = src_dataset / "images" / split
    if not src_img_dir.exists():
        return
    for img in image_files(src_img_dir):
        dst_img = out_dataset / "images" / split / img.name
        dst_label = out_dataset / "labels" / split / f"{img.stem}.txt"
        copy_file(img, dst_img, mode=mode)
        copy_label_or_empty(label_path_for_image(src_dataset, split, img), dst_label, mode=mode)


def build_repeated_train_set(args: argparse.Namespace) -> None:
    rng = random.Random(args.seed)
    if np is not None:
        np.random.seed(args.seed)
    elif args.photometric:
        raise RuntimeError("--photometric 需要安装 numpy。")

    src_dataset = Path(args.dataset_dir)
    out_dataset = Path(args.out_dir)
    hard_map = read_hard_csv(args.hard_csv)
    policy = args.build_policy

    if out_dataset.exists() and args.overwrite:
        shutil.rmtree(out_dataset)
    out_dataset.mkdir(parents=True, exist_ok=True)

    # val/test 原样复制；只增强 train。
    for split in ("val", "test"):
        copy_split_unchanged(src_dataset, out_dataset, split, mode=args.copy_mode)

    train_images = image_files(src_dataset / "images" / "train")
    if not train_images:
        raise FileNotFoundError(src_dataset / "images" / "train")

    class_counter = train_class_distribution(src_dataset)
    use_hard = policy in {"hard", "combined"} and bool(hard_map)
    use_minority = policy in {"minority", "combined"}
    use_hard_negative = policy in {"hard_negative", "hard_negative_copy_paste"}
    use_copy_paste = policy in {"copy_paste", "combined", "hard_negative_copy_paste"} and args.copy_paste_per_class > 0

    if policy in {"hard", "hard_negative", "hard_negative_copy_paste"} and not hard_map:
        raise ValueError(f"--build-policy {policy} 需要提供存在的 --hard-csv")
    if policy == "combined" and not hard_map:
        print("[build] warning: --build-policy combined 未提供 hard csv，将只使用 minority/copy-paste 部分。")

    class_repeat = compute_class_repeat(class_counter, args.max_class_repeat) if use_minority else {}

    print("[build] class distribution:", dict(sorted(class_counter.items())))
    print("[build] policy:", policy)
    print("[build] hard repeats:", use_hard)
    print("[build] minority repeats:", use_minority)
    print("[build] hard negative repeats:", use_hard_negative)
    print("[build] copy-paste:", use_copy_paste)
    if class_repeat:
        print("[build] class repeat:", dict(sorted(class_repeat.items())))

    total_written = 0
    repeat_hist = Counter()

    for img_path in train_images:
        label_path = label_path_for_image(src_dataset, "train", img_path)
        classes = image_classes(src_dataset, "train", img_path)
        row = hard_map.get(img_path.stem)
        cls_rep = max([class_repeat.get(c, 1) for c in classes], default=1) if use_minority else 1
        h_rep = hard_repeat_from_row(row, args) if use_hard else 1
        hn_rep = hard_negative_repeat_from_row(row, args) if use_hard_negative else 1
        repeats = max(1, cls_rep, h_rep, hn_rep)
        repeats = min(repeats, args.max_total_repeat)
        repeat_hist[repeats] += 1

        for k in range(repeats):
            if k == 0:
                new_stem = img_path.stem
            else:
                new_stem = f"{img_path.stem}_aug{k:02d}"

            dst_img = out_dataset / "images" / "train" / f"{new_stem}{img_path.suffix.lower()}"
            dst_label = out_dataset / "labels" / "train" / f"{new_stem}.txt"

            if k == 0 or not args.photometric:
                copy_file(img_path, dst_img, mode=args.copy_mode)
            else:
                with Image.open(img_path) as img:
                    aug_img = random_photometric_aug(img, rng)
                save_image_with_original_suffix(aug_img, dst_img)

            copy_label_or_empty(label_path, dst_label, mode=args.copy_mode if k == 0 else "copy")
            total_written += 1

    print("[build] repeated train images written:", total_written)
    print("[build] repeat histogram:", dict(sorted(repeat_hist.items())))

    if use_copy_paste:
        generate_copy_paste_samples(args, src_dataset, out_dataset, class_counter, rng)
    elif policy in {"copy_paste", "combined", "hard_negative_copy_paste"}:
        print("[copy-paste] disabled: --copy-paste-per-class <= 0")

    write_data_yaml_like(src_dataset, out_dataset)
    print("\n[build finished]")
    print("out dataset:", out_dataset)
    print("data yaml:", out_dataset / "data.yaml")


# -----------------------------
# copy-paste：同步更新 segmentation polygon
# -----------------------------

def segment_bbox_px(seg: Segment, width: int, height: int, pad: int = 4) -> Tuple[int, int, int, int]:
    pts = norm_to_pixel(seg.points, width, height)
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0 = max(0, int(math.floor(min(xs))) - pad)
    y0 = max(0, int(math.floor(min(ys))) - pad)
    x1 = min(width, int(math.ceil(max(xs))) + pad)
    y1 = min(height, int(math.ceil(max(ys))) + pad)
    return x0, y0, x1, y1


def make_local_mask(seg: Segment, width: int, height: int, crop_box: Tuple[int, int, int, int]) -> Image.Image:
    x0, y0, x1, y1 = crop_box
    mask = Image.new("L", (x1 - x0, y1 - y0), 0)
    pts_px = norm_to_pixel(seg.points, width, height)
    pts_local = [(x - x0, y - y0) for x, y in pts_px]
    ImageDraw.Draw(mask).polygon([(int(round(x)), int(round(y))) for x, y in pts_local], fill=255)
    return mask


def find_defect_sources(dataset_dir: Path, selected_classes: Optional[set] = None) -> Dict[int, List[Tuple[Path, Segment]]]:
    sources: Dict[int, List[Tuple[Path, Segment]]] = defaultdict(list)
    for img in image_files(dataset_dir / "images" / "train"):
        segs = read_yolo_segments(label_path_for_image(dataset_dir, "train", img))
        for seg in segs:
            if selected_classes is None or seg.cls in selected_classes:
                sources[seg.cls].append((img, seg))
    return sources


def find_background_images(dataset_dir: Path, prefer_empty: bool = True) -> List[Path]:
    imgs = image_files(dataset_dir / "images" / "train")
    if not prefer_empty:
        return imgs
    empty = []
    for img in imgs:
        segs = read_yolo_segments(label_path_for_image(dataset_dir, "train", img))
        if len(segs) == 0:
            empty.append(img)
    return empty if empty else imgs


def choose_minority_classes(class_counter: Counter, ratio: float) -> set:
    if not class_counter:
        return set()
    max_count = max(class_counter.values())
    threshold = max_count * ratio
    return {cls for cls, count in class_counter.items() if count <= threshold}


def paste_one_defect(
    bg_img: Image.Image,
    src_img: Image.Image,
    src_seg: Segment,
    rng: random.Random,
    scale_range: Tuple[float, float],
    feather_radius: float,
) -> Tuple[Image.Image, Optional[Segment]]:
    bg = bg_img.convert("RGB")
    src = src_img.convert("RGB")
    bg_w, bg_h = bg.size
    src_w, src_h = src.size

    crop_box = segment_bbox_px(src_seg, src_w, src_h, pad=6)
    x0, y0, x1, y1 = crop_box
    crop_w, crop_h = x1 - x0, y1 - y0
    if crop_w < 3 or crop_h < 3:
        return bg, None

    patch = src.crop(crop_box)
    mask = make_local_mask(src_seg, src_w, src_h, crop_box)

    scale = rng.uniform(scale_range[0], scale_range[1])
    new_w = max(3, int(round(crop_w * scale)))
    new_h = max(3, int(round(crop_h * scale)))
    if new_w >= bg_w or new_h >= bg_h:
        shrink = min((bg_w - 2) / max(new_w, 1), (bg_h - 2) / max(new_h, 1), 1.0)
        new_w = max(3, int(new_w * shrink))
        new_h = max(3, int(new_h * shrink))

    patch = patch.resize((new_w, new_h), Image.BILINEAR)
    mask = mask.resize((new_w, new_h), Image.BILINEAR)
    if feather_radius > 0:
        mask = mask.filter(ImageFilter.GaussianBlur(radius=feather_radius))

    px = rng.randint(0, max(0, bg_w - new_w))
    py = rng.randint(0, max(0, bg_h - new_h))

    bg.paste(patch, (px, py), mask)

    # 变换 polygon：source pixel -> crop local -> scale -> background pixel -> normalized。
    src_pts_px = norm_to_pixel(src_seg.points, src_w, src_h)
    new_pts_px = []
    for x, y in src_pts_px:
        lx = (x - x0) * (new_w / max(crop_w, 1))
        ly = (y - y0) * (new_h / max(crop_h, 1))
        nx = px + lx
        ny = py + ly
        if 0 <= nx < bg_w and 0 <= ny < bg_h:
            new_pts_px.append((nx, ny))

    if len(new_pts_px) < 3:
        return bg, None

    new_seg = Segment(cls=src_seg.cls, points=pixel_to_norm(new_pts_px, bg_w, bg_h))
    return bg, new_seg


def generate_copy_paste_samples(
    args: argparse.Namespace,
    src_dataset: Path,
    out_dataset: Path,
    class_counter: Counter,
    rng: random.Random,
) -> None:
    if args.copy_paste_classes.strip().lower() == "minority":
        selected_classes = choose_minority_classes(class_counter, args.minority_ratio)
    elif args.copy_paste_classes.strip().lower() == "all":
        selected_classes = set(class_counter.keys())
    else:
        selected_classes = {int(x) for x in args.copy_paste_classes.split(",") if x.strip() != ""}

    if not selected_classes:
        print("[copy-paste] no selected classes, skipped")
        return

    sources = find_defect_sources(src_dataset, selected_classes)
    backgrounds = find_background_images(src_dataset, prefer_empty=args.prefer_empty_background)
    if not backgrounds:
        print("[copy-paste] no background images, skipped")
        return

    print("[copy-paste] selected classes:", sorted(selected_classes))
    print("[copy-paste] backgrounds:", len(backgrounds))

    total = 0
    for cls in sorted(selected_classes):
        cls_sources = sources.get(cls, [])
        if not cls_sources:
            print(f"[copy-paste] class {cls}: no source segments, skipped")
            continue

        for i in range(args.copy_paste_per_class):
            bg_path = rng.choice(backgrounds)
            src_path, src_seg = rng.choice(cls_sources)

            with Image.open(bg_path) as bg_img, Image.open(src_path) as src_img:
                new_img, new_seg = paste_one_defect(
                    bg_img=bg_img,
                    src_img=src_img,
                    src_seg=src_seg,
                    rng=rng,
                    scale_range=(args.copy_paste_scale_min, args.copy_paste_scale_max),
                    feather_radius=args.copy_paste_feather,
                )

            if new_seg is None:
                continue

            # 背景如果本来有标签，则保留；空背景则只写新缺陷。
            bg_label = label_path_for_image(src_dataset, "train", bg_path)
            new_segments = read_yolo_segments(bg_label)
            new_segments.append(new_seg)

            new_stem = f"cp_cls{cls}_{i:05d}"
            out_img = out_dataset / "images" / "train" / f"{new_stem}{bg_path.suffix.lower()}"
            out_label = out_dataset / "labels" / "train" / f"{new_stem}.txt"
            save_image_with_original_suffix(new_img, out_img)
            write_yolo_segments(out_label, new_segments)
            total += 1

    print("[copy-paste] synthetic samples written:", total)


# -----------------------------
# report：快速查看 hard csv 分布
# -----------------------------

def command_report(args: argparse.Namespace) -> None:
    rows = list(read_hard_csv(args.hard_csv).values())
    if not rows:
        print("empty hard csv")
        return
    score_counter = Counter()
    tag_counter = Counter()
    fn = fp = wrong = low_iou = 0
    for r in rows:
        score = float(r.get("hard_score", 0) or 0)
        if score >= args.strong_score:
            score_counter["strong"] += 1
        elif score >= args.medium_score:
            score_counter["medium"] += 1
        elif score > 0:
            score_counter["weak"] += 1
        else:
            score_counter["easy"] += 1
        for tag in str(r.get("hard_tags", "")).split(";"):
            if tag:
                tag_counter[tag] += 1
        fn += int(float(r.get("fn", 0) or 0))
        fp += int(float(r.get("fp", 0) or 0))
        wrong += int(float(r.get("wrong_class", 0) or 0))
        low_iou += int(float(r.get("low_iou", 0) or 0))

    print("[report]")
    print("images:", len(rows))
    print("score levels:", dict(score_counter))
    print("tags:", dict(tag_counter.most_common()))
    print("FN:", fn, "FP:", fp, "wrong_class:", wrong, "low_iou:", low_iou)
    print("top hard samples:")
    for r in sorted(rows, key=lambda x: float(x.get("hard_score", 0) or 0), reverse=True)[: args.topk]:
        print(f"  {float(r['hard_score']):.1f}\t{r.get('hard_tags','')}\t{r.get('image','')}")


# -----------------------------
# CLI
# -----------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="YOLO-seg hard example mining and hard augmentation dataset builder.")
    sub = parser.add_subparsers(dest="command", required=True)

    mine = sub.add_parser("mine", help="Use a trained YOLO-seg model to mine hard examples.")
    mine.add_argument("--model", required=True, type=str)
    mine.add_argument("--dataset-dir", required=True, type=str)
    mine.add_argument("--split", default="train", choices=SPLITS)
    mine.add_argument("--out", required=True, type=str)
    mine.add_argument("--imgsz", default=640, type=int)
    mine.add_argument("--conf", default=0.25, type=float)
    mine.add_argument("--low-conf-thr", default=0.35, type=float)
    mine.add_argument("--iou-thr", default=0.50, type=float)
    mine.add_argument("--low-iou-thr", default=0.20, type=float)
    mine.add_argument("--small-area-ratio", default=0.001, type=float, help="Normalized polygon area below this is treated as small defect.")
    mine.add_argument("--device", default="0", type=str)
    mine.add_argument("--max-det", default=300, type=int)
    mine.add_argument("--limit", default=0, type=int, help="Debug only. 0 means no limit.")
    mine.add_argument("--log-interval", default=100, type=int)
    mine.set_defaults(func=command_mine)

    build = sub.add_parser("build", help="Build hard_aug dataset by oversampling hard/minority samples and optional copy-paste.")
    build.add_argument("--dataset-dir", required=True, type=str)
    build.add_argument("--hard-csv", default=None, type=str)
    build.add_argument("--out-dir", required=True, type=str)
    build.add_argument("--overwrite", action="store_true")
    build.add_argument("--build-policy", default="combined", choices=["none", "hard", "minority", "copy_paste", "hard_negative", "hard_negative_copy_paste", "combined"], help="Offline augmentation policy for train split.")
    build.add_argument("--copy-mode", default="copy", choices=["copy", "hardlink", "symlink"])
    build.add_argument("--seed", default=42, type=int)
    build.add_argument("--photometric", action="store_true", help="Apply brightness/contrast/noise/blur for duplicated samples.")

    # repeat policy
    build.add_argument("--medium-score", default=2.0, type=float)
    build.add_argument("--strong-score", default=5.0, type=float)
    build.add_argument("--medium-repeat", default=3, type=int)
    build.add_argument("--strong-repeat", default=5, type=int)
    build.add_argument("--negative-repeat", default=3, type=int)
    build.add_argument("--max-class-repeat", default=4, type=int)
    build.add_argument("--max-total-repeat", default=6, type=int)

    # copy-paste policy
    build.add_argument("--copy-paste-per-class", default=0, type=int, help="Synthetic copy-paste samples per selected class. 0 disables.")
    build.add_argument("--copy-paste-classes", default="minority", type=str, help="minority, all, or comma-separated class ids like 1,3")
    build.add_argument("--minority-ratio", default=0.50, type=float, help="Class count <= max_count * ratio will be selected as minority.")
    build.add_argument("--copy-paste-scale-min", default=0.7, type=float)
    build.add_argument("--copy-paste-scale-max", default=1.3, type=float)
    build.add_argument("--copy-paste-feather", default=1.2, type=float)
    build.add_argument("--prefer-empty-background", action="store_true", help="Prefer empty-label images as copy-paste background.")
    build.set_defaults(func=build_repeated_train_set)

    report = sub.add_parser("report", help="Report hard csv statistics.")
    report.add_argument("--hard-csv", required=True, type=str)
    report.add_argument("--medium-score", default=2.0, type=float)
    report.add_argument("--strong-score", default=5.0, type=float)
    report.add_argument("--topk", default=20, type=int)
    report.set_defaults(func=command_report)

    return parser


def main(argv: Optional[Sequence[str]] = None) -> None:
    parser = build_parser()
    args = parser.parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
