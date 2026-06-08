import argparse
import shutil
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

import yaml


ROOT = Path(__file__).resolve().parent
SPLITS = ("train", "val", "test")
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}
EPS = 1e-9


@dataclass
class Box:
    cls: int
    x1: float
    y1: float
    x2: float
    y2: float


@dataclass
class Segment:
    cls: int
    points: List[Tuple[float, float]]
    x1: float
    y1: float
    x2: float
    y2: float
    area: float


@dataclass
class SplitStats:
    src_images: int = 0
    patch_images: int = 0
    patch_images_with_labels: int = 0
    oversampled_patches: int = 0
    labels_before: int = 0
    labels_after: int = 0
    labels_dropped: int = 0


def get_pil():
    from PIL import Image, ImageEnhance, ImageFilter, ImageOps

    return Image, ImageEnhance, ImageFilter, ImageOps


def resolve_path(path_value: str) -> Path:
    path = Path(path_value).expanduser()
    if path.is_absolute():
        return path
    return ROOT / path


def image_files(image_dir: Path) -> List[Path]:
    if not image_dir.exists():
        return []
    return sorted(
        path
        for path in image_dir.iterdir()
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    )


def make_starts(length: int, tile: int, stride: int) -> List[int]:
    if tile <= 0 or stride <= 0:
        raise ValueError("tile 和 stride 必须为正数")
    if length <= tile:
        return [0]

    max_start = length - tile
    starts = list(range(0, max_start + 1, stride))
    if starts[-1] != max_start:
        starts.append(max_start)
    return sorted(set(starts))


def load_names(data_yaml: Path) -> Tuple[str, ...]:
    if not data_yaml.exists():
        return ("defect_1", "defect_2", "defect_3", "defect_4")

    with open(data_yaml, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}

    names = data.get("names")
    if isinstance(names, dict):
        return tuple(str(names[idx]) for idx in sorted(names))
    if isinstance(names, list):
        return tuple(str(name) for name in names)
    return ("defect_1", "defect_2", "defect_3", "defect_4")


def infer_dataset_task(src: Path, splits: Sequence[str]) -> str:
    for split in splits:
        label_dir = src / "labels" / split
        if not label_dir.exists():
            continue
        for label_path in sorted(label_dir.glob("*.txt")):
            with open(label_path, "r", encoding="utf-8") as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) == 5:
                        return "detect"
                    if len(parts) >= 7 and (len(parts) - 1) % 2 == 0:
                        return "segment"
    return "detect"


def clamp01(value: float) -> float:
    return min(max(value, 0.0), 1.0)


def polygon_area(points: Sequence[Tuple[float, float]]) -> float:
    if len(points) < 3:
        return 0.0
    area = 0.0
    prev_x, prev_y = points[-1]
    for x, y in points:
        area += prev_x * y - x * prev_y
        prev_x, prev_y = x, y
    return abs(area) / 2.0


def polygon_bounds(points: Sequence[Tuple[float, float]]) -> Tuple[float, float, float, float]:
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    return min(xs), min(ys), max(xs), max(ys)


def same_point(a: Tuple[float, float], b: Tuple[float, float], eps: float = 1e-6) -> bool:
    return abs(a[0] - b[0]) <= eps and abs(a[1] - b[1]) <= eps


def clean_polygon(points: Sequence[Tuple[float, float]], eps: float = 1e-6) -> List[Tuple[float, float]]:
    cleaned: List[Tuple[float, float]] = []
    for point in points:
        if not cleaned or not same_point(cleaned[-1], point, eps):
            cleaned.append(point)

    if len(cleaned) > 1 and same_point(cleaned[0], cleaned[-1], eps):
        cleaned.pop()

    if len(cleaned) <= 3:
        return cleaned

    pruned: List[Tuple[float, float]] = []
    count = len(cleaned)
    for idx, point in enumerate(cleaned):
        prev_point = cleaned[idx - 1]
        next_point = cleaned[(idx + 1) % count]
        v1x = point[0] - prev_point[0]
        v1y = point[1] - prev_point[1]
        v2x = next_point[0] - point[0]
        v2y = next_point[1] - point[1]
        cross = v1x * v2y - v1y * v2x
        dot = v1x * v2x + v1y * v2y
        if abs(cross) <= eps and dot >= -eps:
            continue
        pruned.append(point)
    return pruned


def distance_to_line(point: Tuple[float, float], start: Tuple[float, float], end: Tuple[float, float]) -> float:
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    denom = (dx * dx + dy * dy) ** 0.5
    if denom <= EPS:
        return ((point[0] - start[0]) ** 2 + (point[1] - start[1]) ** 2) ** 0.5
    return abs(dy * point[0] - dx * point[1] + end[0] * start[1] - end[1] * start[0]) / denom


def rdp(points: Sequence[Tuple[float, float]], tolerance: float) -> List[Tuple[float, float]]:
    if len(points) <= 2:
        return list(points)

    max_distance = -1.0
    max_index = 0
    start = points[0]
    end = points[-1]
    for idx in range(1, len(points) - 1):
        distance = distance_to_line(points[idx], start, end)
        if distance > max_distance:
            max_distance = distance
            max_index = idx

    if max_distance > tolerance:
        left = rdp(points[: max_index + 1], tolerance)
        right = rdp(points[max_index:], tolerance)
        return left[:-1] + right
    return [start, end]


def simplify_polygon(points: Sequence[Tuple[float, float]], tolerance: float) -> List[Tuple[float, float]]:
    cleaned = clean_polygon(points)
    if tolerance <= 0 or len(cleaned) <= 3:
        return cleaned

    simplified = rdp(cleaned + [cleaned[0]], tolerance)
    if len(simplified) > 1 and same_point(simplified[0], simplified[-1]):
        simplified.pop()
    return clean_polygon(simplified)


def make_segment(cls: int, points: Sequence[Tuple[float, float]]) -> Optional[Segment]:
    cleaned = clean_polygon(points)
    if len(cleaned) < 3:
        return None

    area = polygon_area(cleaned)
    if area <= EPS:
        return None

    x1, y1, x2, y2 = polygon_bounds(cleaned)
    if x2 <= x1 or y2 <= y1:
        return None

    return Segment(cls=cls, points=cleaned, x1=x1, y1=y1, x2=x2, y2=y2, area=area)


def box_to_segment(cls: int, x1: float, y1: float, x2: float, y2: float) -> Optional[Segment]:
    return make_segment(cls, [(x1, y1), (x2, y1), (x2, y2), (x1, y2)])


def parse_yolo_box(parts: Sequence[str], image_width: int, image_height: int) -> Optional[Box]:
    if len(parts) != 5:
        return None
    try:
        cls = int(parts[0])
        x, y, w, h = map(float, parts[1:])
    except ValueError:
        return None

    box_w = w * image_width
    box_h = h * image_height
    cx = x * image_width
    cy = y * image_height
    x1 = max(0.0, cx - box_w / 2)
    y1 = max(0.0, cy - box_h / 2)
    x2 = min(float(image_width), cx + box_w / 2)
    y2 = min(float(image_height), cy + box_h / 2)
    if x2 <= x1 or y2 <= y1:
        return None
    return Box(cls=cls, x1=x1, y1=y1, x2=x2, y2=y2)


def parse_yolo_segment(parts: Sequence[str], image_width: int, image_height: int) -> Optional[Segment]:
    if len(parts) == 5:
        box = parse_yolo_box(parts, image_width, image_height)
        if box is None:
            return None
        return box_to_segment(box.cls, box.x1, box.y1, box.x2, box.y2)

    if len(parts) < 7 or (len(parts) - 1) % 2 != 0:
        return None

    try:
        cls = int(parts[0])
        coords = [float(value) for value in parts[1:]]
    except ValueError:
        return None

    points = []
    for idx in range(0, len(coords), 2):
        points.append((coords[idx] * image_width, coords[idx + 1] * image_height))
    return make_segment(cls, points)


def read_boxes(label_path: Path, image_width: int, image_height: int) -> List[Box]:
    boxes: List[Box] = []
    if not label_path.exists():
        return boxes

    with open(label_path, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split()
            if not parts:
                continue
            box = parse_yolo_box(parts, image_width, image_height)
            if box is None and len(parts) >= 7 and (len(parts) - 1) % 2 == 0:
                segment = parse_yolo_segment(parts, image_width, image_height)
                if segment is not None:
                    box = Box(segment.cls, segment.x1, segment.y1, segment.x2, segment.y2)
            if box is not None:
                boxes.append(box)
    return boxes


def read_segments(label_path: Path, image_width: int, image_height: int) -> List[Segment]:
    segments: List[Segment] = []
    if not label_path.exists():
        return segments

    with open(label_path, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split()
            if not parts:
                continue
            segment = parse_yolo_segment(parts, image_width, image_height)
            if segment is not None:
                segments.append(segment)
    return segments


def boxes_for_patch(
    boxes: Sequence[Box],
    crop_x: int,
    crop_y: int,
    tile_width: int,
    tile_height: int,
    min_visibility: float,
    min_box_size: float,
) -> Tuple[List[str], int]:
    labels = []
    dropped = 0
    crop_x2 = crop_x + tile_width
    crop_y2 = crop_y + tile_height

    for box in boxes:
        ix1 = max(box.x1, float(crop_x))
        iy1 = max(box.y1, float(crop_y))
        ix2 = min(box.x2, float(crop_x2))
        iy2 = min(box.y2, float(crop_y2))
        iw = ix2 - ix1
        ih = iy2 - iy1
        if iw <= 0 or ih <= 0:
            continue

        box_area = (box.x2 - box.x1) * (box.y2 - box.y1)
        visibility = (iw * ih) / max(box_area, EPS)
        if visibility < min_visibility or iw < min_box_size or ih < min_box_size:
            dropped += 1
            continue

        nx = ((ix1 + ix2) / 2 - crop_x) / tile_width
        ny = ((iy1 + iy2) / 2 - crop_y) / tile_height
        nw = iw / tile_width
        nh = ih / tile_height

        labels.append(f"{box.cls} {clamp01(nx):.6f} {clamp01(ny):.6f} {clamp01(nw):.6f} {clamp01(nh):.6f}")

    return labels, dropped


def clip_polygon(
    points: Sequence[Tuple[float, float]],
    xmin: float,
    ymin: float,
    xmax: float,
    ymax: float,
) -> List[Tuple[float, float]]:
    def inside(point: Tuple[float, float], edge: str) -> bool:
        x, y = point
        if edge == "left":
            return x >= xmin - EPS
        if edge == "right":
            return x <= xmax + EPS
        if edge == "top":
            return y >= ymin - EPS
        if edge == "bottom":
            return y <= ymax + EPS
        raise ValueError(f"未知裁剪边界: {edge}")

    def intersect(start: Tuple[float, float], end: Tuple[float, float], edge: str) -> Tuple[float, float]:
        x1, y1 = start
        x2, y2 = end
        if edge in {"left", "right"}:
            x = xmin if edge == "left" else xmax
            if abs(x2 - x1) <= EPS:
                return x, y1
            ratio = (x - x1) / (x2 - x1)
            return x, y1 + ratio * (y2 - y1)

        y = ymin if edge == "top" else ymax
        if abs(y2 - y1) <= EPS:
            return x1, y
        ratio = (y - y1) / (y2 - y1)
        return x1 + ratio * (x2 - x1), y

    clipped = list(points)
    for edge in ("left", "right", "top", "bottom"):
        if not clipped:
            return []
        output: List[Tuple[float, float]] = []
        prev_point = clipped[-1]
        prev_inside = inside(prev_point, edge)
        for point in clipped:
            point_inside = inside(point, edge)
            if point_inside:
                if not prev_inside:
                    output.append(intersect(prev_point, point, edge))
                output.append(point)
            elif prev_inside:
                output.append(intersect(prev_point, point, edge))
            prev_point = point
            prev_inside = point_inside
        clipped = clean_polygon(output)
    return clean_polygon(clipped)


def segments_for_patch(
    segments: Sequence[Segment],
    crop_x: int,
    crop_y: int,
    tile_width: int,
    tile_height: int,
    min_visibility: float,
    min_box_size: float,
    min_segment_area: float,
    min_segment_points: int,
    simplify_tolerance: float,
) -> Tuple[List[str], int]:
    labels = []
    dropped = 0
    crop_x2 = crop_x + tile_width
    crop_y2 = crop_y + tile_height

    for segment in segments:
        if segment.x2 <= crop_x or segment.x1 >= crop_x2 or segment.y2 <= crop_y or segment.y1 >= crop_y2:
            continue

        clipped = clip_polygon(
            segment.points,
            xmin=float(crop_x),
            ymin=float(crop_y),
            xmax=float(crop_x2),
            ymax=float(crop_y2),
        )
        clipped = simplify_polygon(clipped, tolerance=simplify_tolerance)
        if len(clipped) < min_segment_points:
            dropped += 1
            continue

        clipped_area = polygon_area(clipped)
        if clipped_area < min_segment_area:
            dropped += 1
            continue

        visibility = clipped_area / max(segment.area, EPS)
        x1, y1, x2, y2 = polygon_bounds(clipped)
        if visibility < min_visibility or (x2 - x1) < min_box_size or (y2 - y1) < min_box_size:
            dropped += 1
            continue

        coords = []
        for x, y in clipped:
            coords.append(clamp01((x - crop_x) / tile_width))
            coords.append(clamp01((y - crop_y) / tile_height))

        coord_text = " ".join(f"{value:.6f}" for value in coords)
        labels.append(f"{segment.cls} {coord_text}")

    return labels, dropped


def read_image(image_path: Path):
    Image, _, _, _ = get_pil()
    try:
        image = Image.open(image_path)
        image.load()
    except OSError as exc:
        raise RuntimeError(f"无法读取图片: {image_path}") from exc

    if image.mode not in {"RGB", "L"}:
        image = image.convert("RGB")
    return image


def crop_patch_image(image, crop_x: int, crop_y: int, tile_width: int, tile_height: int):
    return image.crop((crop_x, crop_y, crop_x + tile_width, crop_y + tile_height))


def write_patch_image(image, out_path: Path, image_ext: str, jpg_quality: int) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if image_ext.lower() in {".jpg", ".jpeg"}:
        save_image = image.convert("RGB") if image.mode != "RGB" else image
        save_image.save(out_path, quality=jpg_quality)
    else:
        image.save(out_path)


def write_label(label_path: Path, labels: Sequence[str]) -> None:
    label_path.parent.mkdir(parents=True, exist_ok=True)
    content = "\n".join(labels)
    if content:
        content += "\n"
    label_path.write_text(content, encoding="utf-8")


def label_classes(labels: Sequence[str]) -> List[int]:
    classes = []
    for label in labels:
        parts = label.split()
        if parts:
            classes.append(int(parts[0]))
    return classes


def contains_any_class(labels: Sequence[str], classes: Sequence[int]) -> bool:
    if not labels or not classes:
        return False
    wanted = set(classes)
    return any(cls in wanted for cls in label_classes(labels))


def flip_labels_horizontal(labels: Sequence[str]) -> List[str]:
    flipped = []
    for label in labels:
        parts = label.split()
        if len(parts) == 5:
            cls, x, y, w, h = parts
            new_x = 1.0 - float(x)
            flipped.append(f"{cls} {new_x:.6f} {float(y):.6f} {float(w):.6f} {float(h):.6f}")
            continue

        if len(parts) < 7 or (len(parts) - 1) % 2 != 0:
            continue

        cls = parts[0]
        try:
            coords = [float(value) for value in parts[1:]]
        except ValueError:
            continue

        flipped_coords = []
        for idx in range(0, len(coords), 2):
            flipped_coords.append(1.0 - coords[idx])
            flipped_coords.append(coords[idx + 1])
        coord_text = " ".join(f"{value:.6f}" for value in flipped_coords)
        flipped.append(f"{cls} {coord_text}")
    return flipped


def augment_patch(image, labels: Sequence[str], variant: str, index: int):
    _, ImageEnhance, ImageFilter, ImageOps = get_pil()
    if variant == "copy":
        return image.copy(), list(labels)
    if variant == "hflip":
        return ImageOps.mirror(image), flip_labels_horizontal(labels)
    if variant == "brightness":
        factor = 1.08 if index % 2 else 0.92
        return ImageEnhance.Brightness(image).enhance(factor), list(labels)
    if variant == "contrast":
        factor = 1.12 if index % 2 else 0.9
        return ImageEnhance.Contrast(image).enhance(factor), list(labels)
    if variant == "blur":
        return image.filter(ImageFilter.GaussianBlur(radius=0.8)), list(labels)
    raise ValueError(f"未知增强方式: {variant}")


def update_patch_stats(stats: SplitStats, counter: Counter, labels: Sequence[str], oversampled: bool = False) -> None:
    stats.patch_images += 1
    stats.labels_after += len(labels)
    if labels:
        stats.patch_images_with_labels += 1
        for cls in label_classes(labels):
            counter[cls] += 1
    if oversampled:
        stats.oversampled_patches += 1


def clear_output(dst: Path, overwrite: bool) -> None:
    if not dst.exists():
        return
    if not overwrite:
        raise FileExistsError(f"输出目录已存在: {dst}。如需重建请加 --overwrite")
    shutil.rmtree(dst)


def slice_split(args: argparse.Namespace, src: Path, dst: Path, split: str) -> Tuple[SplitStats, Counter]:
    stats = SplitStats()
    cls_counter = Counter()

    src_image_dir = src / "images" / split
    src_label_dir = src / "labels" / split
    dst_image_dir = dst / "images" / split
    dst_label_dir = dst / "labels" / split

    images = image_files(src_image_dir)
    if args.max_images_per_split > 0:
        images = images[: args.max_images_per_split]

    stats.src_images = len(images)
    dst_image_dir.mkdir(parents=True, exist_ok=True)
    dst_label_dir.mkdir(parents=True, exist_ok=True)

    for image_idx, image_path in enumerate(images, start=1):
        image = read_image(image_path)
        image_width, image_height = image.size

        label_path = src_label_dir / f"{image_path.stem}.txt"
        if args.task == "segment":
            annotations = read_segments(label_path, image_width, image_height)
            stats.labels_before += len(annotations)
        else:
            annotations = read_boxes(label_path, image_width, image_height)
            stats.labels_before += len(annotations)

        starts_x = make_starts(image_width, args.tile_width, args.stride_x)
        starts_y = make_starts(image_height, args.tile_height, args.stride_y)

        for crop_y in starts_y:
            for crop_x in starts_x:
                crop = crop_patch_image(
                    image,
                    crop_x=crop_x,
                    crop_y=crop_y,
                    tile_width=args.tile_width,
                    tile_height=args.tile_height,
                )

                if args.task == "segment":
                    labels, dropped = segments_for_patch(
                        annotations,
                        crop_x=crop_x,
                        crop_y=crop_y,
                        tile_width=args.tile_width,
                        tile_height=args.tile_height,
                        min_visibility=args.min_visibility,
                        min_box_size=args.min_box_size,
                        min_segment_area=args.min_segment_area,
                        min_segment_points=args.min_segment_points,
                        simplify_tolerance=args.simplify_tolerance,
                    )
                else:
                    labels, dropped = boxes_for_patch(
                        annotations,
                        crop_x=crop_x,
                        crop_y=crop_y,
                        tile_width=args.tile_width,
                        tile_height=args.tile_height,
                        min_visibility=args.min_visibility,
                        min_box_size=args.min_box_size,
                    )
                stats.labels_dropped += dropped

                if args.drop_empty and not labels:
                    continue

                patch_stem = f"{image_path.stem}_x{crop_x:04d}_y{crop_y:04d}"
                image_ext = args.image_ext or image_path.suffix.lower()
                out_image_path = dst_image_dir / f"{patch_stem}{image_ext}"
                out_label_path = dst_label_dir / f"{patch_stem}.txt"

                write_patch_image(crop, out_image_path, image_ext=image_ext, jpg_quality=args.jpg_quality)
                write_label(out_label_path, labels)
                update_patch_stats(stats, cls_counter, labels, oversampled=False)

                if (
                    split in args.oversample_splits
                    and args.oversample_factor > 1
                    and contains_any_class(labels, args.oversample_classes)
                ):
                    for extra_idx in range(1, args.oversample_factor):
                        variant = args.oversample_augments[(extra_idx - 1) % len(args.oversample_augments)]
                        aug_image, aug_labels = augment_patch(crop, labels, variant=variant, index=extra_idx)
                        aug_stem = f"{patch_stem}_os{extra_idx}_{variant}"
                        aug_image_path = dst_image_dir / f"{aug_stem}{image_ext}"
                        aug_label_path = dst_label_dir / f"{aug_stem}.txt"
                        write_patch_image(aug_image, aug_image_path, image_ext=image_ext, jpg_quality=args.jpg_quality)
                        write_label(aug_label_path, aug_labels)
                        update_patch_stats(stats, cls_counter, aug_labels, oversampled=True)

        if args.print_every > 0 and image_idx % args.print_every == 0:
            print(f"[{split}] {image_idx}/{len(images)} images processed")

    return stats, cls_counter


def write_data_yaml(dst: Path, names: Sequence[str], splits: Iterable[str], task: str) -> None:
    data = {
        "path": str(dst),
        "train": "images/train",
        "val": "images/val",
        "nc": len(names),
        "names": {idx: name for idx, name in enumerate(names)},
    }
    if task == "segment":
        data["task"] = "segment"
    if "test" in set(splits):
        data["test"] = "images/test"

    with open(dst / "data.yaml", "w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, allow_unicode=True, sort_keys=False)


def write_slice_config(dst: Path, args: argparse.Namespace, stats: Dict[str, SplitStats], counters: Dict[str, Counter]) -> None:
    data = {
        "source": str(resolve_path(args.src)),
        "output": str(resolve_path(args.dst)),
        "task": args.task,
        "tile_width": args.tile_width,
        "tile_height": args.tile_height,
        "stride_x": args.stride_x,
        "stride_y": args.stride_y,
        "min_visibility": args.min_visibility,
        "min_box_size": args.min_box_size,
        "min_segment_area": args.min_segment_area,
        "min_segment_points": args.min_segment_points,
        "simplify_tolerance": args.simplify_tolerance,
        "drop_empty": args.drop_empty,
        "oversample_classes": args.oversample_classes,
        "oversample_factor": args.oversample_factor,
        "oversample_splits": args.oversample_splits,
        "oversample_augments": args.oversample_augments,
        "splits": args.splits,
        "stats": {split: asdict(split_stats) for split, split_stats in stats.items()},
        "class_distribution": {
            split: dict(sorted(counter.items())) for split, counter in counters.items()
        },
    }
    with open(dst / "slice_config.yaml", "w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, allow_unicode=True, sort_keys=False)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Slice a YOLO detect/segment dataset into small-defect-friendly patches.")
    parser.add_argument("--src", default="severstal_yolo", help="源 YOLO 数据集目录")
    parser.add_argument("--dst", default="severstal_yolo_sliced_640x256", help="输出 YOLO 数据集目录")
    parser.add_argument("--splits", nargs="+", default=list(SPLITS), choices=SPLITS, help="要切片的 split")
    parser.add_argument("--task", choices=["auto", "detect", "segment"], default="auto", help="标签任务类型，auto 会从标签格式推断")

    parser.add_argument("--tile-width", type=int, default=640, help="patch 宽度")
    parser.add_argument("--tile-height", type=int, default=256, help="patch 高度")
    parser.add_argument("--stride-x", type=int, default=512, help="横向滑窗步长")
    parser.add_argument("--stride-y", type=int, default=256, help="纵向滑窗步长")
    parser.add_argument("--min-visibility", type=float, default=0.2, help="保留截断标注的最小可见面积比例")
    parser.add_argument("--min-box-size", type=float, default=2.0, help="保留标注外接框的最小宽高像素")
    parser.add_argument("--min-segment-area", type=float, default=4.0, help="segment 裁剪后保留的最小多边形面积像素")
    parser.add_argument("--min-segment-points", type=int, default=3, help="segment 裁剪后保留的最少多边形点数")
    parser.add_argument("--simplify-tolerance", type=float, default=1.0, help="segment 多边形点数简化容差像素，0 表示不简化")
    parser.add_argument("--drop-empty", action="store_true", help="丢弃没有标注的 patch。默认保留负样本 patch")
    parser.add_argument("--oversample-classes", nargs="*", type=int, default=[1], help="需要过采样增强的少样本类别 id，默认 defect_2=1")
    parser.add_argument("--oversample-factor", type=int, default=3, help="包含少样本类别的 train patch 总保留倍数，1 表示关闭")
    parser.add_argument("--oversample-splits", nargs="+", default=["train"], choices=SPLITS, help="在哪些 split 启用过采样，默认只增强 train")
    parser.add_argument("--oversample-augments", nargs="+", default=["hflip", "brightness"], choices=["copy", "hflip", "brightness", "contrast", "blur"], help="过采样副本使用的增强方式")

    parser.add_argument("--image-ext", default=".jpg", help="输出图片扩展名。传空字符串则保留源扩展名")
    parser.add_argument("--jpg-quality", type=int, default=95, help="JPG 输出质量")
    parser.add_argument("--overwrite", action="store_true", help="如果输出目录存在则先删除")
    parser.add_argument("--max-images-per-split", type=int, default=0, help="调试用：每个 split 最多处理多少张，0 表示全部")
    parser.add_argument("--print-every", type=int, default=1000, help="每处理多少张源图打印一次进度，0 表示不打印")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    src = resolve_path(args.src).resolve()
    dst = resolve_path(args.dst).resolve()

    if args.image_ext == "":
        args.image_ext = None
    elif not args.image_ext.startswith("."):
        args.image_ext = "." + args.image_ext

    if not src.exists():
        raise FileNotFoundError(f"源数据集不存在: {src}")

    if args.task == "auto":
        args.task = infer_dataset_task(src, args.splits)

    clear_output(dst, overwrite=args.overwrite)
    dst.mkdir(parents=True, exist_ok=True)

    names = load_names(src / "data.yaml")
    all_stats: Dict[str, SplitStats] = {}
    all_counters: Dict[str, Counter] = {}

    print("\n========== Slice YOLO Dataset ==========")
    print("src:", src)
    print("dst:", dst)
    print("task:", args.task)
    print("tile:", f"{args.tile_width}x{args.tile_height}")
    print("stride:", f"{args.stride_x}x{args.stride_y}")
    print("min_visibility:", args.min_visibility)
    print("min_box_size:", args.min_box_size)
    if args.task == "segment":
        print("min_segment_area:", args.min_segment_area)
        print("min_segment_points:", args.min_segment_points)
        print("simplify_tolerance:", args.simplify_tolerance)
    print("drop_empty:", args.drop_empty)
    print("oversample_classes:", args.oversample_classes)
    print("oversample_factor:", args.oversample_factor)
    print("oversample_splits:", args.oversample_splits)
    print("oversample_augments:", args.oversample_augments)

    for split in args.splits:
        if not (src / "images" / split).exists():
            print(f"跳过 {split}: images/{split} 不存在")
            continue
        stats, counter = slice_split(args, src=src, dst=dst, split=split)
        all_stats[split] = stats
        all_counters[split] = counter
        print(f"\n[{split}]")
        print("src_images:", stats.src_images)
        print("patch_images:", stats.patch_images)
        print("patch_images_with_labels:", stats.patch_images_with_labels)
        print("oversampled_patches:", stats.oversampled_patches)
        print("labels_before:", stats.labels_before)
        print("labels_after:", stats.labels_after)
        print("labels_dropped:", stats.labels_dropped)
        print("class_distribution:", dict(sorted(counter.items())))

    write_data_yaml(dst, names=names, splits=all_stats.keys(), task=args.task)
    write_slice_config(dst, args=args, stats=all_stats, counters=all_counters)

    print("\n========== Finished ==========")
    print("data_yaml:", dst / "data.yaml")
    print("slice_config:", dst / "slice_config.yaml")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
