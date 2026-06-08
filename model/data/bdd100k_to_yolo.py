import argparse
import json
import shutil
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

import yaml


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BDD_ROOT = ROOT / "datasets" / "bdd100k"
DEFAULT_OUTPUT_DIR = ROOT / "model" / "data" / "bdd100k_yolo_det"
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}
DEFAULT_IMAGE_SIZE = (1280, 720)
SPLITS = ("train", "val")
EPS = 1e-9
DEFAULT_CLASSES = (
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


@dataclass
class SplitStats:
    label_items: int = 0
    image_files_indexed: int = 0
    duplicate_image_names: int = 0
    images_written: int = 0
    images_missing: int = 0
    images_empty: int = 0
    labels_written: int = 0
    boxes_dropped: int = 0
    previews_written: int = 0


def get_pil_image():
    from PIL import Image

    return Image


def get_pil_draw():
    from PIL import ImageDraw

    return ImageDraw


def resolve_path(path_value: str | Path) -> Path:
    path = Path(path_value).expanduser()
    if path.is_absolute():
        return path
    return ROOT / path


def first_existing(paths: Iterable[Path], label: str) -> Path:
    for path in paths:
        if path.exists():
            return path
    candidates = "\n  ".join(str(path) for path in paths)
    raise FileNotFoundError(f"Cannot find {label}. Tried:\n  {candidates}")


def default_image_root(bdd_root: Path) -> Path:
    return first_existing(
        (
            bdd_root / "bdd100k" / "bdd100k" / "images" / "100k",
            bdd_root / "bdd100k" / "images" / "100k",
            bdd_root / "images" / "100k",
        ),
        "BDD100K image root",
    )


def default_label_root(bdd_root: Path) -> Path:
    return first_existing(
        (
            bdd_root / "bdd100k_labels_release" / "bdd100k" / "labels",
            bdd_root / "bdd100k" / "labels",
            bdd_root / "labels",
        ),
        "BDD100K label root",
    )


def label_json_path(label_root: Path, split: str) -> Path:
    candidates = (
        label_root / f"bdd100k_labels_images_{split}.json",
        label_root / "det_20" / f"det_{split}.json",
    )
    return first_existing(candidates, f"BDD100K {split} label json")


def image_files(image_dir: Path) -> List[Path]:
    if not image_dir.exists():
        return []
    return sorted(
        path
        for path in image_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    )


def prefer_image_path(current: Path, candidate: Path, split_dir: Path) -> Path:
    if current.parent == split_dir:
        return current
    if candidate.parent == split_dir:
        return candidate
    return current


def build_image_index(split_dir: Path) -> Tuple[Dict[str, Path], int]:
    index: Dict[str, Path] = {}
    duplicates = 0
    for image_path in image_files(split_dir):
        current = index.get(image_path.name)
        if current is None:
            index[image_path.name] = image_path
            continue
        duplicates += 1
        index[image_path.name] = prefer_image_path(current, image_path, split_dir)
    return index, duplicates


def read_image_size(image_path: Path, fallback: Tuple[int, int]) -> Tuple[int, int]:
    Image = get_pil_image()
    try:
        with Image.open(image_path) as image:
            return int(image.width), int(image.height)
    except OSError:
        return fallback


def clamp(value: float, low: float, high: float) -> float:
    return min(max(value, low), high)


def box_to_yolo_line(
    class_id: int,
    box: dict,
    image_width: int,
    image_height: int,
) -> Optional[str]:
    try:
        x1 = float(box["x1"])
        y1 = float(box["y1"])
        x2 = float(box["x2"])
        y2 = float(box["y2"])
    except (KeyError, TypeError, ValueError):
        return None

    x1 = clamp(x1, 0.0, float(image_width))
    y1 = clamp(y1, 0.0, float(image_height))
    x2 = clamp(x2, 0.0, float(image_width))
    y2 = clamp(y2, 0.0, float(image_height))

    box_w = x2 - x1
    box_h = y2 - y1
    if box_w <= EPS or box_h <= EPS:
        return None

    cx = (x1 + x2) * 0.5 / image_width
    cy = (y1 + y2) * 0.5 / image_height
    nw = box_w / image_width
    nh = box_h / image_height
    return f"{class_id} {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}"


def make_link_or_copy(src: Path, dst: Path, mode: str) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() or dst.is_symlink():
        return
    if mode == "symlink":
        dst.symlink_to(src.resolve())
    elif mode == "copy":
        shutil.copy2(src, dst)
    else:
        raise ValueError(f"Unsupported image mode: {mode}")


def write_label(label_path: Path, lines: Sequence[str]) -> None:
    label_path.parent.mkdir(parents=True, exist_ok=True)
    content = "\n".join(lines)
    if content:
        content += "\n"
    label_path.write_text(content, encoding="utf-8")


def yolo_line_to_box(line: str, image_width: int, image_height: int) -> Optional[Tuple[int, float, float, float, float]]:
    parts = line.split()
    if len(parts) != 5:
        return None
    try:
        class_id = int(parts[0])
        cx, cy, box_w, box_h = (float(value) for value in parts[1:])
    except ValueError:
        return None

    pixel_w = box_w * image_width
    pixel_h = box_h * image_height
    center_x = cx * image_width
    center_y = cy * image_height
    x1 = clamp(center_x - pixel_w * 0.5, 0.0, float(image_width))
    y1 = clamp(center_y - pixel_h * 0.5, 0.0, float(image_height))
    x2 = clamp(center_x + pixel_w * 0.5, 0.0, float(image_width))
    y2 = clamp(center_y + pixel_h * 0.5, 0.0, float(image_height))
    if x2 <= x1 or y2 <= y1:
        return None
    return class_id, x1, y1, x2, y2


def draw_preview(
    image_path: Path,
    preview_path: Path,
    lines: Sequence[str],
    class_names: Sequence[str],
    fallback_size: Tuple[int, int],
) -> bool:
    Image = get_pil_image()
    ImageDraw = get_pil_draw()
    try:
        with Image.open(image_path) as image:
            canvas = image.convert("RGB")
    except OSError:
        return False

    image_width, image_height = canvas.size
    draw = ImageDraw.Draw(canvas)
    for line in lines:
        parsed = yolo_line_to_box(line, image_width, image_height)
        if parsed is None:
            continue
        class_id, x1, y1, x2, y2 = parsed
        class_name = class_names[class_id] if 0 <= class_id < len(class_names) else str(class_id)
        color = (255, 64, 64)
        width = max(2, round(min(fallback_size) / 360))
        draw.rectangle((x1, y1, x2, y2), outline=color, width=width)
        text = f"{class_id}:{class_name}"
        text_x = x1
        text_y = max(0.0, y1 - 14)
        draw.rectangle((text_x, text_y, text_x + max(42, len(text) * 7), text_y + 14), fill=color)
        draw.text((text_x + 2, text_y + 1), text, fill=(255, 255, 255))

    preview_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(preview_path, quality=92)
    return True


def convert_split(
    split: str,
    image_root: Path,
    label_root: Path,
    output_dir: Path,
    class_to_id: Dict[str, int],
    class_names: Sequence[str],
    image_mode: str,
    fallback_size: Tuple[int, int],
    drop_empty: bool,
    max_images: int,
    preview_count: int,
) -> Tuple[SplitStats, Counter]:
    stats = SplitStats()
    class_counts: Counter = Counter()

    split_image_dir = image_root / split
    image_index, duplicates = build_image_index(split_image_dir)
    stats.image_files_indexed = len(image_index)
    stats.duplicate_image_names = duplicates

    json_path = label_json_path(label_root, split)
    with json_path.open("r", encoding="utf-8") as f:
        items = json.load(f)
    if not isinstance(items, list):
        raise ValueError(f"BDD100K labels must be a list: {json_path}")

    if max_images > 0:
        items = items[:max_images]
    stats.label_items = len(items)

    for item in items:
        image_name = item.get("name")
        if not isinstance(image_name, str):
            stats.images_missing += 1
            continue

        src_image = image_index.get(image_name)
        if src_image is None:
            stats.images_missing += 1
            continue

        image_width, image_height = read_image_size(src_image, fallback_size)
        lines: List[str] = []
        for label in item.get("labels", []) or []:
            category = label.get("category")
            if category not in class_to_id:
                continue
            line = box_to_yolo_line(class_to_id[category], label.get("box2d", {}), image_width, image_height)
            if line is None:
                stats.boxes_dropped += 1
                continue
            lines.append(line)
            class_counts[category] += 1

        if drop_empty and not lines:
            stats.images_empty += 1
            continue

        dst_image = output_dir / "images" / split / src_image.name
        dst_label = output_dir / "labels" / split / f"{src_image.stem}.txt"
        make_link_or_copy(src_image, dst_image, image_mode)
        write_label(dst_label, lines)

        stats.images_written += 1
        stats.labels_written += len(lines)
        if not lines:
            stats.images_empty += 1
        elif stats.previews_written < preview_count:
            preview_path = output_dir / "preview" / split / src_image.name
            if draw_preview(src_image, preview_path, lines, class_names, fallback_size):
                stats.previews_written += 1

    return stats, class_counts


def write_data_yaml(output_dir: Path, class_names: Sequence[str], splits: Sequence[str]) -> None:
    data = {
        "path": str(output_dir.resolve()),
        "train": "images/train",
        "val": "images/val",
        "names": {idx: name for idx, name in enumerate(class_names)},
    }
    if "test" in splits:
        data["test"] = "images/test"

    with (output_dir / "data.yaml").open("w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, allow_unicode=True, sort_keys=False)


def write_summary(
    output_dir: Path,
    args: argparse.Namespace,
    stats: Dict[str, SplitStats],
    counters: Dict[str, Counter],
) -> None:
    summary = {
        "source": {
            "bdd_root": str(args.bdd_root),
            "image_root": str(args.image_root),
            "label_root": str(args.label_root),
        },
        "output": str(output_dir.resolve()),
        "classes": list(args.classes),
        "image_mode": args.image_mode,
        "drop_empty": args.drop_empty,
        "splits": {
            split: {
                **asdict(split_stats),
                "class_counts": dict(counters[split]),
            }
            for split, split_stats in stats.items()
        },
    }
    with (output_dir / "convert_summary.yaml").open("w", encoding="utf-8") as f:
        yaml.safe_dump(summary, f, allow_unicode=True, sort_keys=False)


def write_class_distribution(output_dir: Path, class_names: Sequence[str], counters: Dict[str, Counter]) -> None:
    stats_dir = output_dir / "stats"
    stats_dir.mkdir(parents=True, exist_ok=True)
    splits = list(counters)

    totals = Counter()
    for counter in counters.values():
        totals.update(counter)

    csv_path = stats_dir / "class_distribution.csv"
    with csv_path.open("w", encoding="utf-8") as f:
        f.write("class_id,class_name,total")
        for split in splits:
            f.write(f",{split}")
        f.write("\n")
        for class_id, class_name in enumerate(class_names):
            split_values = [counters[split].get(class_name, 0) for split in splits]
            f.write(f"{class_id},{class_name},{sum(split_values)}")
            for value in split_values:
                f.write(f",{value}")
            f.write("\n")

    Image = get_pil_image()
    ImageDraw = get_pil_draw()
    bar_count = max(1, len(class_names))
    width = max(640, 140 + bar_count * 120)
    height = 420
    margin_left = 80
    margin_right = 40
    margin_top = 52
    margin_bottom = 92
    plot_w = width - margin_left - margin_right
    plot_h = height - margin_top - margin_bottom
    canvas = Image.new("RGB", (width, height), (255, 255, 255))
    draw = ImageDraw.Draw(canvas)

    draw.text((margin_left, 18), "BDD100K YOLO class distribution", fill=(32, 32, 32))
    draw.line((margin_left, margin_top, margin_left, margin_top + plot_h), fill=(80, 80, 80), width=2)
    draw.line((margin_left, margin_top + plot_h, margin_left + plot_w, margin_top + plot_h), fill=(80, 80, 80), width=2)

    values = [totals.get(class_name, 0) for class_name in class_names]
    max_value = max(values) if values else 0
    scale_max = max(1, max_value)
    step = plot_w / bar_count
    bar_w = max(24, int(step * 0.55))
    colors = ((45, 119, 191), (232, 126, 44), (46, 160, 67), (170, 90, 180), (214, 64, 69))

    for idx, class_name in enumerate(class_names):
        value = totals.get(class_name, 0)
        bar_h = 0 if value <= 0 else max(1, int(plot_h * value / scale_max))
        center_x = margin_left + step * idx + step * 0.5
        x1 = int(center_x - bar_w * 0.5)
        x2 = int(center_x + bar_w * 0.5)
        y1 = margin_top + plot_h - bar_h
        y2 = margin_top + plot_h
        color = colors[idx % len(colors)]
        draw.rectangle((x1, y1, x2, y2), fill=color, outline=(30, 30, 30))
        draw.text((x1, max(margin_top, y1 - 16)), str(value), fill=(32, 32, 32))
        label = f"{idx}:{class_name}"
        draw.text((x1, y2 + 10), label[:16], fill=(32, 32, 32))

    draw.text((14, margin_top), str(scale_max), fill=(64, 64, 64))
    draw.text((28, margin_top + plot_h - 10), "0", fill=(64, 64, 64))
    canvas.save(stats_dir / "class_distribution.png", quality=92)


def clear_output(output_dir: Path, overwrite: bool) -> None:
    if not output_dir.exists():
        return
    if not overwrite:
        raise FileExistsError(f"Output directory exists: {output_dir}. Use --overwrite to rebuild it.")
    shutil.rmtree(output_dir)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Convert BDD100K detection JSON labels to YOLO detect format.")
    parser.add_argument("--bdd-root", default=str(DEFAULT_BDD_ROOT), help="BDD100K download root")
    parser.add_argument("--image-root", default="", help="BDD100K image root. Default: auto-detect images/100k")
    parser.add_argument("--label-root", default="", help="BDD100K label root. Default: auto-detect labels")
    parser.add_argument("--out", default=str(DEFAULT_OUTPUT_DIR), help="Output YOLO dataset directory")
    parser.add_argument("--splits", nargs="+", default=list(SPLITS), choices=("train", "val", "test"))
    parser.add_argument("--classes", nargs="+", default=list(DEFAULT_CLASSES), help="BDD100K categories to keep")
    parser.add_argument("--image-mode", choices=("symlink", "copy"), default="symlink")
    parser.add_argument("--drop-empty", action="store_true", help="Skip images without selected classes")
    parser.add_argument("--fallback-size", nargs=2, type=int, default=DEFAULT_IMAGE_SIZE, metavar=("WIDTH", "HEIGHT"))
    parser.add_argument("--preview-count", type=int, default=8, help="Positive preview images to draw per split")
    parser.add_argument("--max-images-per-split", type=int, default=0, help="Debug limit; 0 means all images")
    parser.add_argument("--overwrite", action="store_true")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    args.bdd_root = resolve_path(args.bdd_root)
    args.image_root = resolve_path(args.image_root) if args.image_root else default_image_root(args.bdd_root)
    args.label_root = resolve_path(args.label_root) if args.label_root else default_label_root(args.bdd_root)
    output_dir = resolve_path(args.out)

    class_names = list(dict.fromkeys(args.classes))
    class_to_id = {name: idx for idx, name in enumerate(class_names)}
    fallback_size = int(args.fallback_size[0]), int(args.fallback_size[1])

    clear_output(output_dir, args.overwrite)
    output_dir.mkdir(parents=True, exist_ok=True)

    all_stats: Dict[str, SplitStats] = {}
    all_counters: Dict[str, Counter] = {}
    for split in args.splits:
        if split == "test":
            continue
        split_stats, class_counts = convert_split(
            split=split,
            image_root=args.image_root,
            label_root=args.label_root,
            output_dir=output_dir,
            class_to_id=class_to_id,
            class_names=class_names,
            image_mode=args.image_mode,
            fallback_size=fallback_size,
            drop_empty=args.drop_empty,
            max_images=args.max_images_per_split,
            preview_count=max(0, args.preview_count),
        )
        all_stats[split] = split_stats
        all_counters[split] = class_counts

    write_data_yaml(output_dir, class_names=class_names, splits=tuple(all_stats))
    write_summary(output_dir, args=args, stats=all_stats, counters=all_counters)
    write_class_distribution(output_dir, class_names=class_names, counters=all_counters)

    print("YOLO dataset:", output_dir)
    print("data_yaml:", output_dir / "data.yaml")
    print("class_distribution:", output_dir / "stats" / "class_distribution.png")
    for split, split_stats in all_stats.items():
        print(f"{split}: {split_stats.images_written} images, {split_stats.labels_written} labels")
        print(f"{split}: previews = {split_stats.previews_written}")
        if split_stats.images_missing:
            print(f"{split}: missing images = {split_stats.images_missing}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
