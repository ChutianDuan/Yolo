#!/usr/bin/env python3
"""Export verified BDD100K GT-only manifests for model-level evaluation."""

from __future__ import annotations

import argparse
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

import cv2

import evaluate_bdd100k_tracking as evaluation
from prepare_bdd100k_trackeval import load_official_labels, sha256_file


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument(
        "--video",
        action="append",
        required=True,
        metavar="NAME=PATH",
        help="Named BDD100K source video; repeat for each sequence.",
    )
    parser.add_argument("--frame-stride", type=int, default=6)
    parser.add_argument("--max-missing-tail-label-frames", type=int, default=2)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def parse_named_paths(values: Iterable[str]) -> list[tuple[str, Path]]:
    result: list[tuple[str, Path]] = []
    names: set[str] = set()
    for value in values:
        name, separator, raw_path = value.partition("=")
        name = name.strip()
        if not separator or not name or not raw_path.strip():
            raise evaluation.EvaluationError(
                f"Invalid --video value {value!r}; expected NAME=PATH"
            )
        if name in names:
            raise evaluation.EvaluationError(f"Duplicate video name: {name}")
        path = Path(raw_path).expanduser().resolve()
        if not path.is_file():
            raise evaluation.EvaluationError(f"Video does not exist: {path}")
        if path.stem != name:
            raise evaluation.EvaluationError(
                f"Video name/path stem mismatch: {name!r} != {path.stem!r}"
            )
        names.add(name)
        result.append((name, path))
    return result


def decode_video_metadata(path: Path) -> dict[str, Any]:
    capture = cv2.VideoCapture(str(path))
    if not capture.isOpened():
        raise evaluation.EvaluationError(f"Failed to open video: {path}")
    frame_count = 0
    width = 0
    height = 0
    fps = float(capture.get(cv2.CAP_PROP_FPS))
    try:
        while True:
            ok, frame = capture.read()
            if not ok:
                break
            if frame is None or frame.size == 0:
                raise evaluation.EvaluationError(
                    f"Decoded an empty frame at index {frame_count}: {path}"
                )
            if frame_count == 0:
                height, width = frame.shape[:2]
            elif frame.shape[1] != width or frame.shape[0] != height:
                raise evaluation.EvaluationError(
                    f"Video dimensions changed at frame {frame_count}: {path}"
                )
            frame_count += 1
    finally:
        capture.release()
    if frame_count <= 0 or width <= 0 or height <= 0:
        raise evaluation.EvaluationError(f"Video has no decodable frames: {path}")
    if not math.isfinite(fps) or fps <= 0.0:
        raise evaluation.EvaluationError(f"Video has invalid FPS {fps}: {path}")
    return {
        "path": str(path),
        "file_size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
        "decoded_frame_count": frame_count,
        "width": width,
        "height": height,
        "fps": fps,
    }


def select_evaluated_indices(
    label_indices: Iterable[int],
    decoded_frame_count: int,
    frame_stride: int,
    max_missing_tail_label_frames: int,
) -> tuple[list[int], list[int]]:
    if decoded_frame_count <= 0:
        raise evaluation.EvaluationError("decoded_frame_count must be positive")
    if frame_stride <= 0:
        raise evaluation.EvaluationError("frame_stride must be positive")
    if max_missing_tail_label_frames < 0:
        raise evaluation.EvaluationError(
            "max_missing_tail_label_frames must be non-negative"
        )
    indices = sorted(set(label_indices))
    if not indices or indices[0] < 0:
        raise evaluation.EvaluationError("Label indices must be non-empty and non-negative")
    selected = [index for index in indices if index * frame_stride < decoded_frame_count]
    skipped = [index for index in indices if index * frame_stride >= decoded_frame_count]
    if not selected:
        raise evaluation.EvaluationError("No label frame is covered by the source video")
    if skipped and skipped != indices[len(selected) :]:
        raise evaluation.EvaluationError("Missing label frames are not a trailing suffix")
    if len(skipped) > max_missing_tail_label_frames:
        raise evaluation.EvaluationError(
            f"Video misses {len(skipped)} trailing label frames; maximum is "
            f"{max_missing_tail_label_frames}"
        )
    return selected, skipped


def main() -> int:
    args = parse_args()
    videos = parse_named_paths(args.video)
    output_dir = args.output_dir.expanduser().resolve()
    if output_dir.exists():
        raise evaluation.EvaluationError(
            f"Output directory already exists: {output_dir}"
        )
    if args.frame_stride <= 0:
        raise evaluation.EvaluationError("--frame-stride must be positive")
    if args.max_missing_tail_label_frames < 0:
        raise evaluation.EvaluationError(
            "--max-missing-tail-label-frames must be non-negative"
        )

    video_metadata = {
        name: decode_video_metadata(path) for name, path in videos
    }
    labels, labels_metadata = load_official_labels(
        args.labels.resolve(), [name for name, _ in videos]
    )

    selected_frames: dict[str, list[dict[str, Any]]] = {}
    manifest_videos: dict[str, dict[str, Any]] = {}
    for name, _ in videos:
        frame_map = labels[name]
        selected, skipped = select_evaluated_indices(
            frame_map,
            video_metadata[name]["decoded_frame_count"],
            args.frame_stride,
            args.max_missing_tail_label_frames,
        )
        selected_frames[name] = [frame_map[index] for index in selected]
        manifest_videos[name] = {
            "frame_stride": args.frame_stride,
            "input_label_frames": len(frame_map),
            "evaluated_label_frames": len(selected),
            "skipped_tail_label_frames": skipped,
            "video": video_metadata[name],
        }

    output_dir.mkdir(parents=True)
    gt_dir = output_dir / "gt"
    gt_dir.mkdir()
    files: list[dict[str, str]] = []
    for name, _ in videos:
        path = gt_dir / f"{name}.json"
        path.write_text(
            json.dumps(selected_frames[name], ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        files.append(
            {
                "path": path.relative_to(output_dir).as_posix(),
                "sha256": sha256_file(path),
            }
        )

    manifest = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "source_labels": {
            **labels_metadata,
            "sha256": sha256_file(args.labels.resolve()),
        },
        "videos": manifest_videos,
        "files": files,
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"manifest={manifest_path}")
    print(
        "evaluated_frames="
        + str(sum(item["evaluated_label_frames"] for item in manifest_videos.values()))
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
