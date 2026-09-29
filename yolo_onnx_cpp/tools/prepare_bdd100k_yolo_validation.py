#!/usr/bin/env python3
"""Create a deterministic, bounded BDD100K YOLO validation subset manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

import cv2


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
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}


class ValidationPreparationError(RuntimeError):
    pass


@dataclass(frozen=True)
class SampleCandidate:
    sample_id: str
    image_path: Path
    label_path: Path
    labels: tuple[tuple[int, float, float, float, float], ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--max-images", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument(
        "--force-class",
        action="append",
        default=["train"],
        help="Keep every validation image containing this class; may be repeated.",
    )
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_yolo_labels(
    label_path: Path,
) -> tuple[tuple[int, float, float, float, float], ...]:
    labels: list[tuple[int, float, float, float, float]] = []
    try:
        lines = label_path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as exc:
        raise ValidationPreparationError(f"Cannot read label: {label_path}") from exc
    for line_number, raw_line in enumerate(lines, start=1):
        parts = raw_line.split()
        if not parts:
            continue
        if len(parts) != 5:
            raise ValidationPreparationError(
                f"Expected 5 label fields: {label_path}:{line_number}"
            )
        try:
            class_id = int(parts[0])
            center_x, center_y, width, height = (float(value) for value in parts[1:])
        except ValueError as exc:
            raise ValidationPreparationError(
                f"Invalid label value: {label_path}:{line_number}"
            ) from exc
        if not 0 <= class_id < len(BDD100K_NAMES):
            raise ValidationPreparationError(
                f"Class ID out of range: {label_path}:{line_number}"
            )
        values = (center_x, center_y, width, height)
        if not all(math.isfinite(value) for value in values):
            raise ValidationPreparationError(
                f"Non-finite label geometry: {label_path}:{line_number}"
            )
        if (
            not 0.0 <= center_x <= 1.0
            or not 0.0 <= center_y <= 1.0
            or not 0.0 < width <= 1.0
            or not 0.0 < height <= 1.0
        ):
            raise ValidationPreparationError(
                f"Normalized label geometry out of range: {label_path}:{line_number}"
            )
        labels.append((class_id, center_x, center_y, width, height))
    return tuple(labels)


def collect_candidates(dataset_dir: Path) -> list[SampleCandidate]:
    image_dir = dataset_dir / "images" / "val"
    label_dir = dataset_dir / "labels" / "val"
    if not image_dir.is_dir() or not label_dir.is_dir():
        raise ValidationPreparationError(
            f"Validation image/label directories are missing under {dataset_dir}"
        )

    image_by_stem: dict[str, Path] = {}
    for image_path in sorted(image_dir.iterdir()):
        if not image_path.is_file() or image_path.suffix.lower() not in IMAGE_SUFFIXES:
            continue
        if image_path.stem in image_by_stem:
            raise ValidationPreparationError(f"Duplicate validation image stem: {image_path.stem}")
        image_by_stem[image_path.stem] = image_path

    label_paths = sorted(label_dir.glob("*.txt"))
    if not image_by_stem or not label_paths:
        raise ValidationPreparationError("Validation split is empty")
    label_stems = {path.stem for path in label_paths}
    if set(image_by_stem) != label_stems:
        missing_images = sorted(label_stems - set(image_by_stem))
        missing_labels = sorted(set(image_by_stem) - label_stems)
        raise ValidationPreparationError(
            "Validation image/label stems differ: "
            f"missing_images={missing_images[:5]}, missing_labels={missing_labels[:5]}"
        )

    return [
        SampleCandidate(
            sample_id=label_path.stem,
            image_path=image_by_stem[label_path.stem],
            label_path=label_path,
            labels=parse_yolo_labels(label_path),
        )
        for label_path in label_paths
    ]


def parse_forced_classes(values: Iterable[str]) -> tuple[int, ...]:
    name_to_id = {name: class_id for class_id, name in enumerate(BDD100K_NAMES)}
    result: set[int] = set()
    for value in values:
        name = value.strip().lower()
        if name not in name_to_id:
            raise ValidationPreparationError(f"Unknown forced class: {value}")
        result.add(name_to_id[name])
    return tuple(sorted(result))


def stable_rank(sample_id: str, seed: int) -> bytes:
    return hashlib.sha256(f"{seed}:{sample_id}".encode("utf-8")).digest()


def select_candidates(
    candidates: list[SampleCandidate],
    max_images: int,
    seed: int,
    forced_class_ids: tuple[int, ...],
) -> tuple[list[SampleCandidate], set[str]]:
    if max_images <= 0:
        raise ValidationPreparationError("--max-images must be positive")
    target_count = min(max_images, len(candidates))
    forced_ids = set(forced_class_ids)
    forced = [
        candidate
        for candidate in candidates
        if forced_ids.intersection(label[0] for label in candidate.labels)
    ]
    if len(forced) > target_count:
        raise ValidationPreparationError(
            f"Forced-class images ({len(forced)}) exceed target ({target_count})"
        )
    forced_sample_ids = {candidate.sample_id for candidate in forced}
    remaining = [
        candidate for candidate in candidates if candidate.sample_id not in forced_sample_ids
    ]
    remaining.sort(key=lambda item: (stable_rank(item.sample_id, seed), item.sample_id))
    selected = forced + remaining[: target_count - len(forced)]
    selected.sort(key=lambda item: item.sample_id)
    return selected, forced_sample_ids


def absolute_box(
    label: tuple[int, float, float, float, float],
    image_width: int,
    image_height: int,
) -> tuple[float, float, float, float]:
    _, center_x, center_y, width, height = label
    x1 = max(0.0, (center_x - width * 0.5) * image_width)
    y1 = max(0.0, (center_y - height * 0.5) * image_height)
    x2 = min(float(image_width), (center_x + width * 0.5) * image_width)
    y2 = min(float(image_height), (center_y + height * 0.5) * image_height)
    if x2 <= x1 or y2 <= y1:
        raise ValidationPreparationError("Label becomes degenerate after clipping")
    return tuple(round(value, 6) for value in (x1, y1, x2, y2))


def class_counts(candidates: Iterable[SampleCandidate]) -> tuple[Counter[int], Counter[int]]:
    image_counts: Counter[int] = Counter()
    box_counts: Counter[int] = Counter()
    for candidate in candidates:
        classes = [label[0] for label in candidate.labels]
        box_counts.update(classes)
        image_counts.update(set(classes))
    return image_counts, box_counts


def named_counts(counts: Counter[int]) -> dict[str, int]:
    return {name: int(counts[class_id]) for class_id, name in enumerate(BDD100K_NAMES)}


def build_payload(
    dataset_dir: Path,
    candidates: list[SampleCandidate],
    selected: list[SampleCandidate],
    forced_sample_ids: set[str],
    forced_class_ids: tuple[int, ...],
    max_images: int,
    seed: int,
) -> dict:
    all_image_counts, all_box_counts = class_counts(candidates)
    selected_image_counts, selected_box_counts = class_counts(selected)
    samples = []
    for candidate in selected:
        image = cv2.imread(str(candidate.image_path), cv2.IMREAD_COLOR)
        if image is None:
            raise ValidationPreparationError(f"Cannot decode image: {candidate.image_path}")
        image_height, image_width = image.shape[:2]
        labels = [
            {
                "class_id": label[0],
                "class_name": BDD100K_NAMES[label[0]],
                "box_xyxy": absolute_box(label, image_width, image_height),
            }
            for label in candidate.labels
        ]
        samples.append(
            {
                "sample_id": candidate.sample_id,
                "image": str(candidate.image_path.relative_to(dataset_dir)),
                "label": str(candidate.label_path.relative_to(dataset_dir)),
                "image_sha256": sha256_file(candidate.image_path),
                "label_sha256": sha256_file(candidate.label_path),
                "image_width": image_width,
                "image_height": image_height,
                "forced": candidate.sample_id in forced_sample_ids,
                "labels": labels,
            }
        )

    convert_summary = dataset_dir / "convert_summary.yaml"
    return {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "dataset": {
            "dataset_dir": str(dataset_dir),
            "split": "val",
            "convert_summary_sha256": (
                sha256_file(convert_summary) if convert_summary.is_file() else None
            ),
            "classes": list(BDD100K_NAMES),
            "candidate_images": len(candidates),
            "candidate_class_images": named_counts(all_image_counts),
            "candidate_class_boxes": named_counts(all_box_counts),
        },
        "selection": {
            "method": "sha256_rank_with_forced_class_positives",
            "seed": seed,
            "requested_max_images": max_images,
            "selected_images": len(selected),
            "forced_classes": [BDD100K_NAMES[class_id] for class_id in forced_class_ids],
            "forced_images": len(forced_sample_ids),
            "selected_class_images": named_counts(selected_image_counts),
            "selected_class_boxes": named_counts(selected_box_counts),
        },
        "samples": samples,
    }


def markdown_summary(payload: dict) -> str:
    selection = payload["selection"]
    dataset = payload["dataset"]
    lines = [
        "# BDD100K YOLO validation subset",
        "",
        f"Generated: {payload['generated_at']}",
        "",
        f"- Split: {dataset['split']}",
        f"- Candidate images: {dataset['candidate_images']}",
        f"- Selected images: {selection['selected_images']}",
        f"- Selection method: {selection['method']}",
        f"- Seed: {selection['seed']}",
        f"- Forced classes: {', '.join(selection['forced_classes'])}",
        f"- Forced images: {selection['forced_images']}",
        "",
        "| Class | Candidate images | Candidate boxes | Selected images | Selected boxes |",
        "|---|---:|---:|---:|---:|",
    ]
    for name in BDD100K_NAMES:
        lines.append(
            f"| {name} | {dataset['candidate_class_images'][name]} | "
            f"{dataset['candidate_class_boxes'][name]} | "
            f"{selection['selected_class_images'][name]} | "
            f"{selection['selected_class_boxes'][name]} |"
        )
    lines.extend(
        [
            "",
            "The subset is independent from the training split. It is deterministic but not a "
            "pure random sample because every positive image for each forced class is retained.",
            "",
        ]
    )
    return "\n".join(lines)


def write_outputs(output_dir: Path, payload: dict) -> None:
    if output_dir.exists():
        raise ValidationPreparationError(f"Output directory already exists: {output_dir}")
    output_dir.mkdir(parents=True)
    samples_path = output_dir / "validation_samples.json"
    summary_path = output_dir / "selection_summary.md"
    samples_path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    summary_path.write_text(markdown_summary(payload), encoding="utf-8")
    manifest = {
        "schema_version": 1,
        "files": [
            {"path": samples_path.name, "sha256": sha256_file(samples_path)},
            {"path": summary_path.name, "sha256": sha256_file(summary_path)},
        ],
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    args = parse_args()
    dataset_dir = args.dataset_dir.expanduser().resolve()
    forced_class_ids = parse_forced_classes(args.force_class)
    candidates = collect_candidates(dataset_dir)
    selected, forced_sample_ids = select_candidates(
        candidates,
        args.max_images,
        args.seed,
        forced_class_ids,
    )
    payload = build_payload(
        dataset_dir,
        candidates,
        selected,
        forced_sample_ids,
        forced_class_ids,
        args.max_images,
        args.seed,
    )
    output_dir = args.output_dir.expanduser().resolve()
    write_outputs(output_dir, payload)
    print(f"selected={len(selected)} forced={len(forced_sample_ids)} output={output_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
