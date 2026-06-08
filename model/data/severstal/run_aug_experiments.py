#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Run the E0-E6 YOLO-seg offline augmentation ablation matrix.

Default behavior:
- Reuse the existing E0 base weight.
- Reuse model/data/hard_train.csv.
- Build only missing E2-E6 datasets.
- Train only missing E1-E6 best.pt files.
- Compare selected weights on the original sliced val/test splits.
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence

ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = ROOT
DEFAULT_DATASET = ROOT / "data" / "severstal_yolo_seg_sliced_640x256"
DEFAULT_HARD_CSV = ROOT / "data" / "hard_train.csv"
DEFAULT_BASE_RUN = ROOT / "runs" / "segment" / "severstal_yolo11n_seg_pretrained"
DEFAULT_BASE_WEIGHT = DEFAULT_BASE_RUN / "weights" / "best.pt"
DEFAULT_PRETRAINED = ROOT / "yolo_base" / "yolo11n-seg.pt"
DEFAULT_AUG_ROOT = ROOT / "data" / "aug_experiments"
DEFAULT_PROJECT = ROOT / "runs" / "segment"
DEFAULT_COMPARE_DIR = ROOT / "runs" / "compare" / "aug_ablation_E0_rect_E6"
IMG_SIZE = (256, 640)  # TrainConfig expects (height, width).


@dataclass(frozen=True)
class ExperimentSpec:
    exp_id: str
    name: str
    build_policy: Optional[str]
    train_from_base: bool
    run_name: str
    epochs: int = 50
    lr0: float = 0.0002
    build_options: Dict[str, object] = field(default_factory=dict)

    @property
    def label(self) -> str:
        return f"{self.exp_id}_{self.name}"

    @property
    def dataset_name(self) -> str:
        return f"{self.exp_id}_{self.name}"


EXPERIMENTS: Dict[str, ExperimentSpec] = {
    "E0": ExperimentSpec(
        exp_id="E0",
        name="base",
        build_policy=None,
        train_from_base=False,
        run_name="severstal_yolo11n_seg_E0_base",
        epochs=100,
        lr0=0.001,
    ),
    "E0_RECT": ExperimentSpec(
        exp_id="E0",
        name="rect",
        build_policy=None,
        train_from_base=False,
        run_name="severstal_yolo11n_seg_E0_rect",
        epochs=100,
        lr0=0.001,
    ),
    "E1": ExperimentSpec(
        exp_id="E1",
        name="base_continue",
        build_policy=None,
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E1_base_continue",
    ),
    "E2": ExperimentSpec(
        exp_id="E2",
        name="hard_repeat",
        build_policy="hard",
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E2_hard_repeat",
        build_options={"max_total_repeat": 5},
    ),
    "E3": ExperimentSpec(
        exp_id="E3",
        name="minority_repeat",
        build_policy="minority",
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E3_minority_repeat",
        build_options={"max_total_repeat": 4},
    ),
    "E4": ExperimentSpec(
        exp_id="E4",
        name="copy_paste_light",
        build_policy="copy_paste",
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E4_copy_paste_light",
        build_options={"copy_paste_per_class": 100, "max_total_repeat": 1, "prefer_empty_background": True},
    ),
    "E5": ExperimentSpec(
        exp_id="E5",
        name="hard_negative",
        build_policy="hard_negative",
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E5_hard_negative",
        build_options={"negative_repeat": 3, "max_total_repeat": 3},
    ),
    "E6": ExperimentSpec(
        exp_id="E6",
        name="hard_aug_final",
        build_policy="combined",
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E6_hard_aug_final",
        build_options={"copy_paste_per_class": 100, "max_total_repeat": 4, "prefer_empty_background": True},
    ),
    "E7": ExperimentSpec(
        exp_id="E7",
        name="hard_negative_light",
        build_policy="hard_negative",
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E7_hard_negative_light",
        build_options={"negative_repeat": 2, "max_total_repeat": 2},
    ),
    "E8": ExperimentSpec(
        exp_id="E8",
        name="defect2_copy_paste_light",
        build_policy="copy_paste",
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E8_defect2_copy_paste_light",
        build_options={
            "copy_paste_per_class": 50,
            "copy_paste_classes": "1",
            "copy_paste_scale_min": 0.9,
            "copy_paste_scale_max": 1.1,
            "copy_paste_feather": 0.8,
            "max_total_repeat": 1,
            "prefer_empty_background": True,
        },
    ),
    "E9": ExperimentSpec(
        exp_id="E9",
        name="final_light",
        build_policy="hard_negative_copy_paste",
        train_from_base=True,
        run_name="severstal_yolo11n_seg_E9_final_light",
        build_options={
            "negative_repeat": 2,
            "max_total_repeat": 2,
            "copy_paste_per_class": 50,
            "copy_paste_classes": "1",
            "copy_paste_scale_min": 0.9,
            "copy_paste_scale_max": 1.1,
            "copy_paste_feather": 0.8,
            "prefer_empty_background": True,
        },
    ),
}


class ExperimentError(RuntimeError):
    pass


def parse_experiment_ids(values: Optional[Sequence[str]]) -> List[str]:
    if not values:
        return list(EXPERIMENTS)

    aliases = {"E0R": "E0_RECT", "E0-RECT": "E0_RECT"}
    ids = []
    for value in values:
        exp_id = aliases.get(value.upper(), value.upper())
        if exp_id not in EXPERIMENTS:
            known = ", ".join(EXPERIMENTS)
            raise ExperimentError(f"未知实验编号: {value}. 可选: {known}")
        if exp_id not in ids:
            ids.append(exp_id)
    return ids


def dataset_dir_for(spec: ExperimentSpec, aug_root: Path, source_dataset: Path) -> Path:
    if spec.build_policy is None:
        return source_dataset
    return aug_root / spec.dataset_name


def dataset_ready(path: Path) -> bool:
    return (
        (path / "data.yaml").exists()
        and (path / "images" / "train").exists()
        and (path / "labels" / "train").exists()
        and (path / "images" / "val").exists()
        and (path / "labels" / "val").exists()
    )


def weight_for_run(project: Path, run_name: str) -> Path:
    return project / run_name / "weights" / "best.pt"


def existing_or_new_base_weight(args: argparse.Namespace) -> Path:
    if args.rerun_base:
        return weight_for_run(args.project, EXPERIMENTS["E0"].run_name)
    return args.base_weight


def build_arg_namespace(
    spec: ExperimentSpec,
    args: argparse.Namespace,
    dataset_dir: Path,
) -> argparse.Namespace:
    options = {
        "dataset_dir": str(args.source_dataset),
        "hard_csv": str(args.hard_csv) if args.hard_csv else None,
        "out_dir": str(dataset_dir),
        "overwrite": args.force_build,
        "build_policy": spec.build_policy,
        "copy_mode": args.copy_mode,
        "seed": args.seed,
        "photometric": args.photometric,
        "medium_score": 2.0,
        "strong_score": 5.0,
        "medium_repeat": 3,
        "strong_repeat": 5,
        "negative_repeat": 3,
        "max_class_repeat": 4,
        "max_total_repeat": 6,
        "copy_paste_per_class": 0,
        "copy_paste_classes": "minority",
        "minority_ratio": 0.50,
        "copy_paste_scale_min": 0.7,
        "copy_paste_scale_max": 1.3,
        "copy_paste_feather": 1.2,
        "prefer_empty_background": False,
    }
    options.update(spec.build_options)
    return argparse.Namespace(**options)


def ensure_hard_csv(args: argparse.Namespace, base_weight: Path) -> None:
    if args.hard_csv.exists() and not args.force_mine:
        print(f"[mine] reuse hard csv: {args.hard_csv}")
        return

    if not args.force_mine:
        raise ExperimentError(
            f"hard csv 不存在: {args.hard_csv}\n"
            "请先生成 hard_train.csv，或加 --force-mine 让脚本用 base best.pt 自动挖 hard 样本。"
        )
    if not base_weight.exists():
        raise ExperimentError(f"无法挖 hard 样本：base weight 不存在: {base_weight}")

    from data.yolo_hard_aug_experiment import command_mine

    print(f"[mine] writing hard csv: {args.hard_csv}")
    mine_args = argparse.Namespace(
        model=str(base_weight),
        dataset_dir=str(args.source_dataset),
        split="train",
        out=str(args.hard_csv),
        imgsz=args.imgsz[0] if isinstance(args.imgsz, list) else args.imgsz,
        conf=args.mine_conf,
        low_conf_thr=0.35,
        iou_thr=0.50,
        low_iou_thr=0.20,
        small_area_ratio=0.001,
        device=args.device,
        max_det=300,
        limit=0,
        log_interval=100,
    )
    command_mine(mine_args)


def prepare_datasets(specs: Sequence[ExperimentSpec], args: argparse.Namespace, base_weight: Path) -> None:
    needs_hard = any(spec.build_policy in {"hard", "hard_negative", "combined"} for spec in specs)
    if needs_hard:
        ensure_hard_csv(args, base_weight)

    from data.yolo_hard_aug_experiment import build_repeated_train_set

    for spec in specs:
        if spec.build_policy is None:
            continue

        out_dataset = dataset_dir_for(spec, args.aug_root, args.source_dataset)
        if dataset_ready(out_dataset) and not args.force_build:
            print(f"[build] skip existing {spec.label}: {out_dataset}")
            continue

        print(f"[build] {spec.label} -> {out_dataset}")
        build_repeated_train_set(build_arg_namespace(spec, args, out_dataset))


def make_train_config(
    spec: ExperimentSpec,
    dataset_dir: Path,
    model_path: Path,
    args: argparse.Namespace,
) -> "TrainConfig":
    from train import TrainConfig

    cfg = TrainConfig()
    cfg.project_root = str(PROJECT_ROOT)
    cfg.dataset_dir = str(dataset_dir)
    cfg.data_yaml = str(dataset_dir / "data.yaml")
    cfg.model_path = str(model_path)
    cfg.project = str(args.project)
    cfg.name = spec.run_name
    cfg.epochs = spec.epochs
    cfg.imgsz = IMG_SIZE
    cfg.batch = args.batch
    cfg.device = args.device
    cfg.workers = args.workers
    cfg.seed = args.seed
    cfg.lr0 = spec.lr0
    cfg.scale = 0.5
    cfg.run_val_eval = False
    cfg.run_test_eval = False
    cfg.run_predict = False
    cfg.remove_old_cache = args.remove_cache
    cfg.max_check_images = args.max_check_images
    return cfg


def train_one(spec: ExperimentSpec, args: argparse.Namespace, base_weight: Path) -> Path:
    from train import normalize_cfg_paths, train

    if spec.train_from_base:
        model_path = base_weight
        dataset_dir = dataset_dir_for(spec, args.aug_root, args.source_dataset)
    else:
        model_path = args.pretrained_model
        dataset_dir = args.source_dataset

    if not dataset_ready(dataset_dir):
        raise ExperimentError(f"训练数据集未准备好: {dataset_dir}")
    if not model_path.exists():
        raise ExperimentError(f"训练起点权重不存在: {model_path}")

    best_pt = weight_for_run(args.project, spec.run_name)
    if best_pt.exists() and not args.force_train:
        print(f"[train] skip existing {spec.label}: {best_pt}")
        return best_pt

    print(f"[train] {spec.label}: dataset={dataset_dir}, model={model_path}")
    cfg = normalize_cfg_paths(make_train_config(spec, dataset_dir, model_path, args))
    train(cfg)
    if not best_pt.exists():
        raise ExperimentError(f"训练结束但未找到 best.pt: {best_pt}")
    return best_pt


def train_experiments(specs: Sequence[ExperimentSpec], args: argparse.Namespace, base_weight: Path) -> Path:
    active_base_weight = base_weight

    if args.rerun_base:
        active_base_weight = train_one(EXPERIMENTS["E0"], args, base_weight)
    elif not active_base_weight.exists():
        raise ExperimentError(f"E0 base weight 不存在: {active_base_weight}")
    else:
        print(f"[train] reuse E0 base: {active_base_weight}")

    for spec in specs:
        if spec is EXPERIMENTS["E0"]:
            continue
        train_one(spec, args, active_base_weight)

    return active_base_weight


def compare_experiments(specs: Sequence[ExperimentSpec], args: argparse.Namespace, base_weight: Path) -> None:
    import compare_yolo_weights

    weights: List[str] = []
    names: List[str] = []

    if args.rerun_base:
        e0_weight = weight_for_run(args.project, EXPERIMENTS["E0"].run_name)
    else:
        e0_weight = base_weight

    compare_specs: List[ExperimentSpec] = []
    has_baseline = any(spec is EXPERIMENTS["E0"] or spec is EXPERIMENTS["E0_RECT"] for spec in specs)
    if not has_baseline:
        compare_specs.append(EXPERIMENTS["E0"])
    compare_specs.extend(specs)

    missing = []
    for spec in compare_specs:
        if spec is EXPERIMENTS["E0"]:
            weight = e0_weight
        else:
            weight = weight_for_run(args.project, spec.run_name)
        if not weight.exists():
            missing.append(f"{spec.label}: {weight}")
            continue
        weights.append(str(weight))
        names.append(spec.label)

    if missing:
        raise ExperimentError("以下权重缺失，无法汇总对比:\n" + "\n".join(missing))
    if len(weights) < 1:
        raise ExperimentError("没有可对比的权重。")

    argv = [
        "--weights",
        *weights,
        "--names",
        *names,
        "--data",
        str(args.source_dataset / "data.yaml"),
        "--splits",
        *args.splits,
        "--imgsz",
        *[str(value) for value in args.imgsz],
        "--batch",
        str(args.batch),
        "--device",
        args.device,
        "--workers",
        str(args.workers),
        "--out-dir",
        str(args.compare_dir),
    ]
    if args.compare_plots:
        argv.append("--plots")
    if args.verbose:
        argv.append("--verbose")
    if args.continue_on_error:
        argv.append("--continue-on-error")

    print(f"[compare] {', '.join(names)}")
    compare_yolo_weights.main(argv)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run E0-E6 YOLO-seg augmentation ablation experiments.")
    parser.add_argument("--experiments", nargs="+", default=None, help="实验编号，例如 E0_rect E2 E4 E6。默认 E0、E0_rect、E1-E9。")

    stage = parser.add_mutually_exclusive_group()
    stage.add_argument("--prepare-only", action="store_true", help="只构建 E2-E6 数据集，不训练、不对比。")
    stage.add_argument("--train-only", action="store_true", help="只训练，不构建、不对比。")
    stage.add_argument("--compare-only", action="store_true", help="只汇总已有权重。")

    parser.add_argument("--source-dataset", type=Path, default=DEFAULT_DATASET)
    parser.add_argument("--hard-csv", type=Path, default=DEFAULT_HARD_CSV)
    parser.add_argument("--base-weight", type=Path, default=DEFAULT_BASE_WEIGHT)
    parser.add_argument("--pretrained-model", type=Path, default=DEFAULT_PRETRAINED)
    parser.add_argument("--aug-root", type=Path, default=DEFAULT_AUG_ROOT)
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument("--compare-dir", type=Path, default=DEFAULT_COMPARE_DIR)

    parser.add_argument("--force-mine", action="store_true", help="重新用 base best.pt 挖 hard_train.csv。")
    parser.add_argument("--rerun-base", action="store_true", help="重新训练 E0，并用新的 E0 best.pt 作为 E1-E6 起点。")
    parser.add_argument("--force-build", action="store_true", help="重建已存在的增强数据集。")
    parser.add_argument("--force-train", action="store_true", help="即使 best.pt 已存在也重新训练。")

    parser.add_argument("--copy-mode", default="hardlink", choices=["copy", "hardlink", "symlink"])
    parser.add_argument("--photometric", action="store_true", help="对重复样本额外做轻量光照/噪声增强。默认关闭。")
    parser.add_argument("--remove-cache", action="store_true", help="训练前删除 labels/*.cache。")

    parser.add_argument("--epochs-note", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--imgsz", type=int, nargs="+", default=[640, 256], help="评估尺寸；一个值表示方形，两个值按 宽 高，例如 640 256")
    parser.add_argument("--batch", type=int, default=96)
    parser.add_argument("--device", type=str, default="4,5")
    parser.add_argument("--workers", type=int, default=min(8, os.cpu_count() or 8))
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--max-check-images", type=int, default=200)
    parser.add_argument("--mine-conf", type=float, default=0.25)

    parser.add_argument("--splits", nargs="+", default=["val", "test"], choices=["train", "val", "test"])
    parser.add_argument("--compare-plots", action="store_true")
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def normalize_args(args: argparse.Namespace) -> argparse.Namespace:
    for key in ["source_dataset", "hard_csv", "base_weight", "pretrained_model", "aug_root", "project", "compare_dir"]:
        setattr(args, key, Path(getattr(args, key)).expanduser().resolve())
    return args


def main(argv: Optional[Sequence[str]] = None) -> None:
    args = normalize_args(build_parser().parse_args(argv))
    selected_ids = parse_experiment_ids(args.experiments)
    selected_specs = [EXPERIMENTS[exp_id] for exp_id in selected_ids]

    if not args.source_dataset.exists():
        raise ExperimentError(f"source dataset 不存在: {args.source_dataset}")

    base_weight = existing_or_new_base_weight(args)

    if args.compare_only:
        compare_experiments(selected_specs, args, base_weight)
        return

    if not args.train_only:
        build_specs = [spec for spec in selected_specs if spec.build_policy is not None]
        prepare_datasets(build_specs, args, base_weight if base_weight.exists() else args.base_weight)

    if args.prepare_only:
        print("[done] prepare-only finished")
        return

    active_base_weight = train_experiments(selected_specs, args, base_weight)
    if args.train_only:
        print("[done] train-only finished")
        return
    compare_experiments(selected_specs, args, active_base_weight)


if __name__ == "__main__":
    try:
        main()
    except ExperimentError as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        raise SystemExit(2)
