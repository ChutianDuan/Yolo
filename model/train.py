import argparse
import math
import os
import random
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

CUDA_VISIBLE_DEVICES = "4,5"
os.environ["CUDA_VISIBLE_DEVICES"] = CUDA_VISIBLE_DEVICES

import torch
import yaml
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parent
SPLITS = ("train", "val", "test")
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}
IMAGE_WIDTH = 640
IMAGE_HEIGHT = 384  # Reduced, stride-aligned input for CPU-friendly inference.
TRAIN_IMGSZ = IMAGE_WIDTH  # Ultralytics train accepts one integer; rect keeps 16:9 batches.
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


def get_yolo():
    from ultralytics import YOLO
    return YOLO


# =========================
# 1. 全局配置
# =========================

@dataclass
class TrainConfig:
    # 项目路径。相对路径会基于 project_root 解析，避免机器路径写死。
    project_root: str = str(ROOT)
    dataset_dir: str = "/home/ubuntu/YOLO/model/data/bdd100k_yolo_det"
    data_yaml: str = "/home/ubuntu/YOLO/model/data/bdd100k_yolo_det/data.yaml"

    # 模型配置。优先使用本地检测权重，找不到时回退到检测结构 yaml。
    model_path: str = "yolo26s.pt"
    fallback_model_yaml: str = "yolo26s.yaml"

    # 类别
    names: Tuple[str, ...] = BDD100K_NAMES
    num_classes: int = len(BDD100K_NAMES)

    # 训练配置
    stage: str = "base"
    epochs: int = 100
    imgsz: int = TRAIN_IMGSZ
    input_width: int = IMAGE_WIDTH
    input_height: int = IMAGE_HEIGHT
    rect: bool = True
    batch: int = 64
    device: str = "0,1"  # Logical devices mapped to physical GPUs 4 and 5.
    workers: int = min(8, os.cpu_count() or 8)
    seed: int = 42

    # 优化器与学习率
    optimizer: str = "AdamW"
    lr0: float = 0.001
    lrf: float = 0.01
    momentum: float = 0.9
    weight_decay: float = 0.0005
    warmup_epochs: float = 3.0

    # 数据增强
    mosaic: float = 0.0
    close_mosaic: int = 0
    mixup: float = 0.0
    cutmix: float = 0.0
    copy_paste: float = 0.0
    fliplr: float = 0.5
    flipud: float = 0.0
    degrees: float = 0.0
    translate: float = 0.1
    scale: float = 0.25
    shear: float = 0.0
    perspective: float = 0.0
    hsv_h: float = 0.015
    hsv_s: float = 0.7
    hsv_v: float = 0.4
    erasing: float = 0.15

    # 基于类别图片频率的 repeat-factor sampling，不复制图片或标签。
    balance_long_tail: bool = True
    balance_repeat_power: float = 0.5
    balance_max_repeat: int = 4
    balance_frequency_threshold: float = 0.05
    balance_max_growth: float = 1.5

    # 困难样本二阶段微调。
    base_weights: str = ""
    hard_sample_list: str = ""
    hard_sample_repeat: int = 2

    # 损失权重
    box: float = 7.5
    cls: float = 0.5
    dfl: float = 1.5

    # 训练控制
    patience: int = 50
    amp: bool = True
    deterministic: bool = True
    cache: bool = False
    pretrained: bool = True
    plots: bool = True
    val: bool = True
    save_period: int = -1
    exist_ok: bool = True
    resume: bool = False

    # 输出
    project: str = "runs/detect"
    name: str = "bdd100k_yolo26s_det_640x384_stage1"

    # 训练前检查
    check_labels: bool = True
    check_images: bool = True
    visualize_labels: bool = False
    num_visualize_per_split: int = 5
    max_check_images: int = 200  # 0 表示检查全部图片
    remove_old_cache: bool = False

    # 训练后动作
    run_val_eval: bool = True
    run_test_eval: bool = False
    run_predict: bool = False
    predict_conf: float = 0.25

    # 调试：只做路径、数据和参数检查，不启动训练。
    dry_run: bool = False


CFG = TrainConfig()


# =========================
# 2. 工具函数
# =========================

def resolve_path(path_value: str, root: Path) -> str:
    path = Path(path_value).expanduser()
    if path.is_absolute():
        return str(path)
    return str(root / path)


def format_imgsz(cfg: TrainConfig) -> str:
    return (
        f"{cfg.input_width}x{cfg.input_height} deployment target "
        f"(train imgsz={cfg.imgsz}, rect={cfg.rect})"
    )


def normalize_cfg_paths(cfg: TrainConfig) -> TrainConfig:
    cfg.project_root = str(Path(cfg.project_root).expanduser().resolve())
    root = Path(cfg.project_root)

    cfg.dataset_dir = resolve_path(cfg.dataset_dir, root)
    cfg.data_yaml = resolve_path(cfg.data_yaml, root)
    cfg.project = resolve_path(cfg.project, root)

    model_path = Path(cfg.model_path).expanduser()
    if model_path.parent != Path(".") or model_path.is_absolute():
        cfg.model_path = resolve_path(cfg.model_path, root)

    fallback_model_yaml = Path(cfg.fallback_model_yaml).expanduser()
    if fallback_model_yaml.parent != Path(".") or fallback_model_yaml.is_absolute():
        cfg.fallback_model_yaml = resolve_path(cfg.fallback_model_yaml, root)

    if cfg.base_weights:
        cfg.base_weights = resolve_path(cfg.base_weights, root)
    if cfg.hard_sample_list:
        cfg.hard_sample_list = resolve_path(cfg.hard_sample_list, root)

    return cfg


def validate_training_config(cfg: TrainConfig) -> None:
    if cfg.stage not in {"base", "hard"}:
        raise ValueError("stage 必须是 base 或 hard")
    if cfg.imgsz <= 0:
        raise ValueError("imgsz 必须为正整数")
    if cfg.input_width <= 0 or cfg.input_height <= 0:
        raise ValueError("input_width 和 input_height 必须为正整数")
    if cfg.input_width % 32 != 0 or cfg.input_height % 32 != 0:
        raise ValueError("部署输入尺寸必须按模型 stride=32 对齐")
    rectangular_incompatible = {
        "mosaic": cfg.mosaic,
        "mixup": cfg.mixup,
        "cutmix": cfg.cutmix,
        "copy_paste": cfg.copy_paste,
    }
    enabled_incompatible = [
        name for name, probability in rectangular_incompatible.items() if probability != 0.0
    ]
    if cfg.rect and enabled_incompatible:
        raise ValueError(
            "rect=True 时必须关闭会破坏矩形 batch 的增强: "
            + ", ".join(enabled_incompatible)
        )
    if not 0.0 < cfg.balance_frequency_threshold <= 1.0:
        raise ValueError("balance_frequency_threshold 必须在 (0, 1] 范围内")
    if cfg.balance_repeat_power <= 0.0:
        raise ValueError("balance_repeat_power 必须为正数")
    if cfg.balance_max_repeat < 1:
        raise ValueError("balance_max_repeat 不能小于 1")
    if cfg.balance_max_growth < 1.0:
        raise ValueError("balance_max_growth 不能小于 1")
    if cfg.hard_sample_repeat < 1:
        raise ValueError("hard_sample_repeat 不能小于 1")

    if cfg.stage == "hard":
        if cfg.resume:
            raise ValueError("hard 阶段不支持 --resume，请从第一阶段 best.pt 开始微调")
        if not cfg.balance_long_tail:
            raise ValueError("hard 阶段必须保留长尾平衡基础训练集")
        if not cfg.base_weights:
            raise ValueError("hard 阶段必须提供 --base-weights")
        if not Path(cfg.base_weights).is_file():
            raise FileNotFoundError(f"第一阶段权重不存在: {cfg.base_weights}")
        if not cfg.hard_sample_list:
            raise ValueError("hard 阶段必须提供 --hard-sample-list")
        if not Path(cfg.hard_sample_list).is_file():
            raise FileNotFoundError(f"困难样本清单不存在: {cfg.hard_sample_list}")


def available_splits(cfg: TrainConfig) -> Tuple[str, ...]:
    dataset_dir = Path(cfg.dataset_dir)
    splits = []
    for split in SPLITS:
        image_dir = dataset_dir / "images" / split
        label_dir = dataset_dir / "labels" / split
        if image_dir.exists() or label_dir.exists():
            splits.append(split)
    return tuple(splits)


def image_files(image_dir: Path) -> List[Path]:
    if not image_dir.exists():
        return []
    return sorted(
        path
        for path in image_dir.iterdir()
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    )


def label_files(label_dir: Path) -> List[Path]:
    if not label_dir.exists():
        return []
    return sorted(path for path in label_dir.iterdir() if path.is_file() and path.suffix == ".txt")


def split_available(cfg: TrainConfig, split: str, require_labels: bool) -> bool:
    dataset_dir = Path(cfg.dataset_dir)
    image_dir = dataset_dir / "images" / split
    label_dir = dataset_dir / "labels" / split
    return image_dir.exists() and (not require_labels or label_dir.exists())


def set_seed(seed: int, deterministic: bool) -> None:
    random.seed(seed)
    torch.manual_seed(seed)

    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)

    if deterministic:
        torch.backends.cudnn.deterministic = True
        torch.backends.cudnn.benchmark = False
    else:
        torch.backends.cudnn.benchmark = True


def resolve_runtime(cfg: TrainConfig) -> Tuple[str, int, bool]:
    requested_device = str(cfg.device).strip()
    if requested_device.lower() in ("", "auto"):
        device = "0" if torch.cuda.is_available() else "cpu"
    else:
        device = requested_device

    cuda_device_requested = any(
        part.strip().lstrip("-").isdigit()
        for part in device.split(",")
    )
    if cuda_device_requested and not torch.cuda.is_available():
        raise RuntimeError("配置要求使用 CUDA device，但当前环境 CUDA 不可用。请使用 --device cpu。")

    cpu_count = os.cpu_count() or 1
    workers = max(0, min(int(cfg.workers), cpu_count))

    amp = bool(cfg.amp) and torch.cuda.is_available() and device.lower() != "cpu"
    if cfg.amp and not amp:
        print("AMP 已关闭：当前运行设备不支持 CUDA AMP。")

    return device, workers, amp


def print_env(device: str, workers: int, amp: bool) -> None:
    print("\n========== Environment ==========")
    print("PyTorch:", torch.__version__)
    print("CUDA_VISIBLE_DEVICES:", os.environ.get("CUDA_VISIBLE_DEVICES"))
    print("CUDA available:", torch.cuda.is_available())
    print("Effective device:", device)
    print("Effective workers:", workers)
    print("Effective AMP:", amp)

    if torch.cuda.is_available():
        print("GPU count:", torch.cuda.device_count())
        for i in range(torch.cuda.device_count()):
            print(f"GPU {i}:", torch.cuda.get_device_name(i))


def print_config_summary(cfg: TrainConfig) -> None:
    print("\n========== Train Config ==========")
    print("stage:", cfg.stage)
    print("dataset_dir:", cfg.dataset_dir)
    print("data_yaml:", cfg.data_yaml)
    print("model_path:", cfg.model_path)
    print("fallback_model_yaml:", cfg.fallback_model_yaml)
    print("epochs:", cfg.epochs)
    print("imgsz:", format_imgsz(cfg))
    print("batch:", cfg.batch)
    print("optimizer:", cfg.optimizer)
    print("lr0:", cfg.lr0)
    print("augmentation:", {
        "mosaic": cfg.mosaic,
        "mixup": cfg.mixup,
        "copy_paste": cfg.copy_paste,
        "scale": cfg.scale,
        "erasing": cfg.erasing,
    })
    print("balance_long_tail:", cfg.balance_long_tail)
    print("balance_frequency_threshold:", cfg.balance_frequency_threshold)
    print("balance_max_repeat:", cfg.balance_max_repeat)
    print("balance_max_growth:", cfg.balance_max_growth)
    if cfg.stage == "hard":
        print("base_weights:", cfg.base_weights)
        print("hard_sample_list:", cfg.hard_sample_list)
        print("hard_sample_repeat:", cfg.hard_sample_repeat)
    print("resume:", cfg.resume)
    print("name:", cfg.name)


def ensure_dirs(cfg: TrainConfig) -> None:
    dataset_dir = Path(cfg.dataset_dir)

    required_dirs = [
        dataset_dir / "images" / "train",
        dataset_dir / "images" / "val",
        dataset_dir / "labels" / "train",
        dataset_dir / "labels" / "val",
    ]

    if cfg.run_test_eval:
        required_dirs.extend([
            dataset_dir / "images" / "test",
            dataset_dir / "labels" / "test",
        ])
    elif cfg.run_predict:
        required_dirs.append(dataset_dir / "images" / "test")

    for directory in required_dirs:
        if not directory.exists():
            raise FileNotFoundError(f"目录不存在: {directory}")

    Path(cfg.project).mkdir(parents=True, exist_ok=True)
    (Path(cfg.project_root) / "outputs" / "label_check").mkdir(parents=True, exist_ok=True)


def read_label_class_ids(label_path: Path, num_classes: int) -> List[int]:
    classes: List[int] = []
    if not label_path.exists():
        return classes

    with open(label_path, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) != 5:
                continue
            try:
                cls = int(parts[0])
            except ValueError:
                continue
            if 0 <= cls < num_classes:
                classes.append(cls)
    return classes


def class_key(cfg: TrainConfig, class_id: int) -> str:
    name = cfg.names[class_id] if 0 <= class_id < len(cfg.names) else str(class_id)
    return f"{class_id}:{name}"


def build_long_tail_train_list(cfg: TrainConfig, run_dir: Path) -> Tuple[Path, dict]:
    dataset_dir = Path(cfg.dataset_dir)
    image_dir = dataset_dir / "images" / "train"
    label_dir = dataset_dir / "labels" / "train"
    images = image_files(image_dir)

    if not images:
        raise FileNotFoundError(f"train 图片为空: {image_dir}")

    image_classes: Dict[Path, List[int]] = {}
    class_box_counts = Counter()
    class_image_counts = Counter()
    missing_label_count = 0

    for image_path in images:
        label_path = label_dir / f"{image_path.stem}.txt"
        if not label_path.exists():
            missing_label_count += 1
        classes = read_label_class_ids(label_path, cfg.num_classes)
        image_classes[image_path] = classes
        class_box_counts.update(classes)
        class_image_counts.update(set(classes))

    if not any(class_box_counts.values()):
        raise RuntimeError("train labels 中没有有效 box，无法做长尾平衡采样。")

    class_frequencies = {
        class_id: class_image_counts[class_id] / len(images)
        for class_id in range(cfg.num_classes)
    }
    class_repeat_factors = {}
    for class_id in range(cfg.num_classes):
        frequency = class_frequencies[class_id]
        if frequency <= 0.0 or frequency >= cfg.balance_frequency_threshold:
            repeat_factor = 1.0
        else:
            raw_factor = (
                cfg.balance_frequency_threshold / frequency
            ) ** cfg.balance_repeat_power
            repeat_factor = min(float(cfg.balance_max_repeat), max(1.0, raw_factor))
        class_repeat_factors[class_id] = repeat_factor

    rng = random.Random(cfg.seed)
    extra_candidates = []
    for image_path in images:
        classes = image_classes[image_path]
        unique_classes = set(classes)
        repeat_factor = max(
            (class_repeat_factors[cls] for cls in unique_classes),
            default=1.0,
        )

        whole_repeat = min(cfg.balance_max_repeat, int(math.floor(repeat_factor)))
        repeat = max(1, whole_repeat)
        fractional = repeat_factor - whole_repeat
        if repeat < cfg.balance_max_repeat and rng.random() < fractional:
            repeat += 1

        for _ in range(repeat - 1):
            extra_candidates.append((repeat_factor, rng.random(), image_path))

    max_balanced_images = max(
        len(images),
        int(math.floor(len(images) * cfg.balance_max_growth)),
    )
    max_extra_images = max_balanced_images - len(images)
    extra_candidates.sort(key=lambda item: (-item[0], item[1], str(item[2])))
    selected_extras = extra_candidates[:max_extra_images]

    balanced_image_paths = list(images)
    balanced_image_paths.extend(item[2] for item in selected_extras)
    random.Random(cfg.seed + 1).shuffle(balanced_image_paths)

    image_repeats = Counter(balanced_image_paths)
    image_repeat_distribution = Counter(image_repeats.values())
    effective_class_box_counts = Counter()
    effective_class_image_counts = Counter()
    for image_path, repeat in image_repeats.items():
        classes = image_classes[image_path]
        effective_class_box_counts.update(
            {cls: count * repeat for cls, count in Counter(classes).items()}
        )
        effective_class_image_counts.update({cls: repeat for cls in set(classes)})

    run_dir.mkdir(parents=True, exist_ok=True)
    train_list_path = run_dir / "train_long_tail_balanced.txt"
    with open(train_list_path, "w", encoding="utf-8") as f:
        for image_path in balanced_image_paths:
            f.write(f"{image_path}\n")

    class_summary = {}
    rare_class_warnings = []
    for class_id in range(cfg.num_classes):
        positive_images = int(class_image_counts[class_id])
        warning = positive_images < 500
        key = class_key(cfg, class_id)
        if warning:
            rare_class_warnings.append(key)
        class_summary[key] = {
            "original_images": positive_images,
            "effective_images": int(effective_class_image_counts[class_id]),
            "original_boxes": int(class_box_counts[class_id]),
            "effective_boxes": int(effective_class_box_counts[class_id]),
            "image_frequency": round(class_frequencies[class_id], 8),
            "repeat_factor": round(class_repeat_factors[class_id], 6),
            "overfitting_warning": warning,
        }

    summary = {
        "enabled": True,
        "method": "repeat_factor_sampling_by_class_image_frequency",
        "note": "所有原始 train 图片至少保留一次；验证集和测试集不参与重复采样。",
        "train_list": str(train_list_path),
        "original_images": len(images),
        "balanced_images": len(balanced_image_paths),
        "growth_ratio": round(len(balanced_image_paths) / len(images), 6),
        "missing_label_count": missing_label_count,
        "frequency_threshold": cfg.balance_frequency_threshold,
        "repeat_power": cfg.balance_repeat_power,
        "max_repeat": cfg.balance_max_repeat,
        "max_growth": cfg.balance_max_growth,
        "growth_cap_applied": len(extra_candidates) > max_extra_images,
        "classes": class_summary,
        "rare_class_overfitting_warnings": rare_class_warnings,
        "image_repeat_distribution": {int(k): int(v) for k, v in sorted(image_repeat_distribution.items())},
    }

    summary_path = run_dir / "tail_balance_summary.yaml"
    with open(summary_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(summary, f, allow_unicode=True, sort_keys=False)

    print("\n========== Long-tail Balanced Sampling ==========")
    print(f"train list: {train_list_path}")
    print(f"summary: {summary_path}")
    print(f"images: {len(images)} -> {len(balanced_image_paths)} ({summary['growth_ratio']}x)")
    if rare_class_warnings:
        print("过拟合警告：以下类别少于 500 张正样本:", ", ".join(rare_class_warnings))

    return train_list_path, summary


def read_image_path_list(list_path: Path) -> List[Path]:
    paths = []
    with open(list_path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if line and not line.startswith("#"):
                paths.append(Path(line).expanduser())
    return paths


def validate_hard_sample_list(cfg: TrainConfig) -> List[Path]:
    dataset_dir = Path(cfg.dataset_dir).resolve()
    train_dir = (dataset_dir / "images" / "train").resolve()
    label_dir = (dataset_dir / "labels" / "train").resolve()
    manifest_path = Path(cfg.hard_sample_list).resolve()
    accepted = []
    seen = set()
    errors = []

    for listed_path in read_image_path_list(manifest_path):
        candidate = listed_path if listed_path.is_absolute() else manifest_path.parent / listed_path
        candidate = candidate.resolve()

        if candidate in seen:
            errors.append(f"重复项: {candidate}")
            continue
        seen.add(candidate)

        try:
            relative_path = candidate.relative_to(train_dir)
        except ValueError:
            errors.append(f"不属于 images/train: {candidate}")
            continue

        if candidate.suffix.lower() not in IMAGE_SUFFIXES or not candidate.is_file():
            errors.append(f"图片不存在或扩展名不支持: {candidate}")
            continue

        label_path = label_dir / relative_path.with_suffix(".txt")
        if not label_path.is_file():
            errors.append(f"缺少对应 train 标签: {label_path}")
            continue

        accepted.append(candidate)

    if errors:
        details = "\n".join(f"  - {message}" for message in errors[:20])
        remaining = len(errors) - 20
        if remaining > 0:
            details += f"\n  - 另有 {remaining} 个错误"
        raise ValueError(f"困难样本清单校验失败:\n{details}")
    if not accepted:
        raise ValueError(f"困难样本清单为空: {manifest_path}")

    return accepted


def build_hard_stage_train_list(
    cfg: TrainConfig,
    balanced_list_path: Path,
    run_dir: Path,
) -> Tuple[Path, dict]:
    balanced_paths = read_image_path_list(balanced_list_path)
    hard_samples = validate_hard_sample_list(cfg)
    weighted_paths = list(balanced_paths)
    for image_path in hard_samples:
        weighted_paths.extend([image_path] * cfg.hard_sample_repeat)
    random.Random(cfg.seed + 2).shuffle(weighted_paths)

    train_list_path = run_dir / "train_hard_weighted.txt"
    with open(train_list_path, "w", encoding="utf-8") as f:
        for image_path in weighted_paths:
            f.write(f"{image_path}\n")

    summary = {
        "stage": "hard",
        "base_train_list": str(balanced_list_path),
        "hard_sample_list": cfg.hard_sample_list,
        "train_list": str(train_list_path),
        "balanced_base_entries": len(balanced_paths),
        "unique_hard_samples": len(hard_samples),
        "extra_repeats_per_hard_sample": cfg.hard_sample_repeat,
        "hard_sample_extra_entries": len(hard_samples) * cfg.hard_sample_repeat,
        "total_entries": len(weighted_paths),
        "growth_over_balanced_base": round(len(weighted_paths) / len(balanced_paths), 6),
    }
    summary_path = run_dir / "hard_stage_sampling_summary.yaml"
    with open(summary_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(summary, f, allow_unicode=True, sort_keys=False)

    print("\n========== Hard-sample Weighted Sampling ==========")
    print(f"train list: {train_list_path}")
    print(f"summary: {summary_path}")
    print(f"entries: {len(balanced_paths)} -> {len(weighted_paths)}")

    return train_list_path, summary


def write_data_yaml(cfg: TrainConfig) -> None:
    """
    写 YOLO detection data.yaml。
    长尾平衡开启时，data.yaml 写到独立配置目录，避免 DDP 重建训练目录时丢失。
    """
    dataset_dir = Path(cfg.dataset_dir)
    train_value = "images/train"
    data_yaml = Path(cfg.data_yaml)

    if cfg.balance_long_tail:
        data_config_dir = Path(cfg.project) / "_data_configs" / cfg.name
        train_list_path, _ = build_long_tail_train_list(cfg, data_config_dir)
        if cfg.stage == "hard":
            train_list_path, _ = build_hard_stage_train_list(
                cfg,
                train_list_path,
                data_config_dir,
            )
        train_value = str(train_list_path)
        data_yaml_name = (
            "data_hard_weighted.yaml"
            if cfg.stage == "hard"
            else "data_tail_balanced.yaml"
        )
        data_yaml = data_config_dir / data_yaml_name
        cfg.data_yaml = str(data_yaml)

    data = {
        "path": cfg.dataset_dir,
        "train": train_value,
        "val": "images/val",
        "nc": cfg.num_classes,
        "names": {i: name for i, name in enumerate(cfg.names)},
    }

    if (dataset_dir / "images" / "test").exists():
        data["test"] = "images/test"

    data_yaml.parent.mkdir(parents=True, exist_ok=True)

    with open(data_yaml, "w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, allow_unicode=True, sort_keys=False)

    print("\n========== data.yaml ==========")
    print(f"已写入: {data_yaml}")
    print(yaml.safe_dump(data, allow_unicode=True, sort_keys=False))


def remove_yolo_cache(cfg: TrainConfig) -> None:
    """
    如果改过 labels，建议删除旧 cache。
    """
    if not cfg.remove_old_cache:
        return

    dataset_dir = Path(cfg.dataset_dir)
    for split in available_splits(cfg):
        cache_path = dataset_dir / "labels" / f"{split}.cache"
        if cache_path.exists():
            cache_path.unlink()
            print(f"已删除 cache: {cache_path}")


def count_files(cfg: TrainConfig) -> None:
    dataset_dir = Path(cfg.dataset_dir)

    print("\n========== Dataset File Count ==========")

    for split in available_splits(cfg):
        image_dir = dataset_dir / "images" / split
        label_dir = dataset_dir / "labels" / split

        images = image_files(image_dir)
        labels = label_files(label_dir)

        print(f"{split}: images={len(images)}, labels={len(labels)}")

        image_stems = {p.stem for p in images}
        label_stems = {p.stem for p in labels}

        missing_labels = sorted(image_stems - label_stems)
        extra_labels = sorted(label_stems - image_stems)

        if missing_labels:
            print(f"  缺失 label 数量: {len(missing_labels)}")
            print(f"  缺失 label 示例: {missing_labels[:5]}")

        if extra_labels:
            print(f"  多余 label 数量: {len(extra_labels)}")
            print(f"  多余 label 示例: {extra_labels[:5]}")


def polygon_area(points: Sequence[Tuple[float, float]]) -> float:
    if len(points) < 3:
        return 0.0

    area = 0.0
    prev_x, prev_y = points[-1]
    for x, y in points:
        area += prev_x * y - x * prev_y
        prev_x, prev_y = x, y
    return abs(area) / 2.0


def check_label_file(label_path: Path, num_classes: int) -> Tuple[List[str], Counter, int]:
    errors = []
    counter = Counter()
    box_count = 0

    with open(label_path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]

    for line_idx, line in enumerate(lines, start=1):
        parts = line.split()

        if len(parts) != 5:
            errors.append(f"{label_path}:{line_idx} 字段数量错误: {line}")
            continue

        try:
            cls = int(parts[0])
            x, y, w, h = (float(value) for value in parts[1:])
        except ValueError:
            errors.append(f"{label_path}:{line_idx} 数值解析失败: {line}")
            continue

        box_count += 1

        if not (0 <= cls < num_classes):
            errors.append(f"{label_path}:{line_idx} 类别越界: cls={cls}")
        else:
            counter[cls] += 1

        if any(coord < 0 or coord > 1 for coord in (x, y, w, h)):
            errors.append(f"{label_path}:{line_idx} 坐标越界: {line}")

        if w <= 0 or h <= 0:
            errors.append(f"{label_path}:{line_idx} box 宽高非法: {line}")

    return errors, counter, box_count


def check_yolo_labels(cfg: TrainConfig) -> None:
    if not cfg.check_labels:
        return

    dataset_dir = Path(cfg.dataset_dir)

    print("\n========== YOLO Detection Label Check ==========")

    total_errors = []
    total_counter = Counter()

    for split in available_splits(cfg):
        label_dir = dataset_dir / "labels" / split
        labels = label_files(label_dir)

        split_counter = Counter()
        split_box_count = 0
        empty_count = 0

        for label_path in labels:
            errors, counter, box_count = check_label_file(label_path, cfg.num_classes)
            total_errors.extend(errors)
            split_counter.update(counter)
            total_counter.update(counter)
            split_box_count += box_count

            if box_count == 0:
                empty_count += 1

        print(f"\n[{split}]")
        print("label files:", len(labels))
        print("empty labels:", empty_count)
        print("box count:", split_box_count)
        print("class distribution:", dict(sorted(split_counter.items())))

    print("\n[all]")
    print("class distribution:", dict(sorted(total_counter.items())))
    print("error count:", len(total_errors))

    if total_errors:
        print("\n前 30 个标签错误:")
        for err in total_errors[:30]:
            print(err)
        raise RuntimeError("标签检查失败，请先修复 labels。")

    print("标签检查通过。")


def check_images_readable(cfg: TrainConfig) -> None:
    if not cfg.check_images:
        return

    dataset_dir = Path(cfg.dataset_dir)

    print("\n========== Image Read Check ==========")

    rng = random.Random(cfg.seed)

    for split in available_splits(cfg):
        image_dir = dataset_dir / "images" / split
        images = image_files(image_dir)

        if cfg.max_check_images > 0 and len(images) > cfg.max_check_images:
            images_to_check = rng.sample(images, k=cfg.max_check_images)
        else:
            images_to_check = images

        bad = []
        shapes = Counter()

        for image_path in images_to_check:
            try:
                with Image.open(image_path) as img:
                    shapes[(img.height, img.width, img.mode)] += 1
            except OSError:
                bad.append(str(image_path))

        print(f"\n[{split}]")
        print("images:", len(images))
        print("checked:", len(images_to_check), "(0 means full check)" if cfg.max_check_images == 0 else "(sampled)")
        print("bad images:", len(bad))
        print("shapes:", dict(shapes.most_common(5)))

        if bad:
            print("坏图示例:", bad[:10])
            raise RuntimeError("存在无法读取的图片，请先修复。")

    print("图片读取检查通过。")


def read_yolo_boxes(label_path: Path) -> List[Tuple[int, float, float, float, float]]:
    boxes: List[Tuple[int, float, float, float, float]] = []

    if not label_path.exists():
        return boxes

    with open(label_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            parts = line.split()
            if len(parts) != 5:
                continue

            try:
                cls = int(parts[0])
                x, y, w, h = (float(value) for value in parts[1:])
            except ValueError:
                continue

            if w > 0 and h > 0:
                boxes.append((cls, x, y, w, h))

    return boxes


def draw_yolo_boxes(
    image: Image.Image,
    boxes: List[Tuple[int, float, float, float, float]],
    names: Tuple[str, ...],
) -> Image.Image:
    w_img, h_img = image.size

    colors = (
        (255, 64, 64),
        (255, 144, 32),
        (64, 128, 255),
        (64, 190, 90),
        (170, 90, 220),
        (32, 170, 170),
        (220, 80, 150),
        (150, 150, 40),
        (40, 180, 230),
        (230, 170, 40),
    )

    canvas = image.convert("RGB")
    draw = ImageDraw.Draw(canvas)

    for cls, x, y, box_w, box_h in boxes:
        color = colors[cls % len(colors)]
        name = names[cls] if 0 <= cls < len(names) else str(cls)
        cx = x * w_img
        cy = y * h_img
        bw = box_w * w_img
        bh = box_h * h_img
        x1 = max(0, min(w_img - 1, int(round(cx - bw / 2))))
        y1 = max(0, min(h_img - 1, int(round(cy - bh / 2))))
        x2 = max(0, min(w_img - 1, int(round(cx + bw / 2))))
        y2 = max(0, min(h_img - 1, int(round(cy + bh / 2))))
        if x2 <= x1 or y2 <= y1:
            continue

        draw.rectangle((x1, y1, x2, y2), outline=color, width=2)
        text = f"{cls}:{name}"
        text_w = max(42, len(text) * 7)
        text_y = max(0, y1 - 14)
        draw.rectangle((x1, text_y, x1 + text_w, text_y + 14), fill=color)
        draw.text((x1 + 2, text_y + 1), text, fill=(255, 255, 255))

    return canvas


def visualize_random_labels(cfg: TrainConfig) -> None:
    if not cfg.visualize_labels:
        return

    dataset_dir = Path(cfg.dataset_dir)
    out_root = Path(cfg.project_root) / "outputs" / "label_check"
    out_root.mkdir(parents=True, exist_ok=True)

    print("\n========== Label Visualization ==========")

    rng = random.Random(cfg.seed)

    for split in available_splits(cfg):
        image_dir = dataset_dir / "images" / split
        label_dir = dataset_dir / "labels" / split

        positive_images = []
        for image_path in image_files(image_dir):
            label_path = label_dir / f"{image_path.stem}.txt"
            boxes = read_yolo_boxes(label_path)
            if boxes:
                positive_images.append(image_path)

        sample_images = rng.sample(
            positive_images,
            k=min(cfg.num_visualize_per_split, len(positive_images)),
        )

        out_dir = out_root / split
        out_dir.mkdir(parents=True, exist_ok=True)

        print(f"{split}: positive_images={len(positive_images)}, sample={len(sample_images)}")

        for image_path in sample_images:
            label_path = label_dir / f"{image_path.stem}.txt"

            try:
                image = Image.open(image_path).convert("RGB")
            except OSError:
                continue

            boxes = read_yolo_boxes(label_path)
            vis = draw_yolo_boxes(image, boxes, cfg.names)

            out_path = out_dir / image_path.name
            vis.save(out_path, quality=95)

    print(f"可视化结果保存到: {out_root}")


def choose_model_path(cfg: TrainConfig) -> str:
    """
    正式训练优先使用预训练 .pt。
    如果没有权重，则回退到 yolo26s.yaml，从头训练检测模型。
    """
    model_path = Path(cfg.model_path)

    if model_path.exists():
        print(f"\n使用预训练权重: {model_path}")
        return str(model_path)

    if model_path.parent == Path(".") and model_path.suffix == ".pt":
        print(f"\n使用 Ultralytics 模型名: {cfg.model_path}")
        print("如果本地没有该权重，Ultralytics 可能会尝试下载。")
        return cfg.model_path

    if model_path.parent == Path(".") and model_path.suffix in {".yaml", ".yml"}:
        print(f"\n使用模型结构文件从头训练: {cfg.model_path}")
        return cfg.model_path

    print(f"\n未找到预训练权重: {model_path}")
    print(f"回退使用结构文件: {cfg.fallback_model_yaml}")
    print("注意：从 yaml 训练是从头训练，收敛更慢，正式实验不建议这样做。")
    return cfg.fallback_model_yaml


def config_as_dict(cfg: TrainConfig) -> dict:
    data = asdict(cfg)
    data["names"] = list(cfg.names)
    data["inference_imgsz"] = [cfg.input_height, cfg.input_width]
    return data


def save_run_config(cfg: TrainConfig, device: str, workers: int, amp: bool) -> None:
    save_dir = Path(cfg.project) / cfg.name
    save_dir.mkdir(parents=True, exist_ok=True)

    data = config_as_dict(cfg)
    data["effective_device"] = device
    data["effective_workers"] = workers
    data["effective_amp"] = amp

    cfg_path = save_dir / "train_config.yaml"
    with open(cfg_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, allow_unicode=True, sort_keys=False)

    print(f"\n训练配置保存到: {cfg_path}")


def is_weight_file(model_path: str) -> bool:
    return Path(model_path).suffix.lower() in {".pt", ".pth"}


def choose_resume_checkpoint(cfg: TrainConfig) -> str:
    last_pt = Path(cfg.project) / cfg.name / "weights" / "last.pt"
    if not last_pt.exists():
        raise FileNotFoundError(f"找不到可恢复训练的 last.pt: {last_pt}")
    print(f"\n恢复训练权重: {last_pt}")
    return str(last_pt)


# =========================
# 3. 训练与验证
# =========================

def train(cfg: TrainConfig):
    cfg = normalize_cfg_paths(cfg)
    validate_training_config(cfg)
    set_seed(cfg.seed, cfg.deterministic)
    device, workers, amp = resolve_runtime(cfg)

    print_env(device, workers, amp)
    print_config_summary(cfg)

    ensure_dirs(cfg)
    write_data_yaml(cfg)
    remove_yolo_cache(cfg)
    count_files(cfg)
    check_images_readable(cfg)
    check_yolo_labels(cfg)
    visualize_random_labels(cfg)
    save_run_config(cfg, device, workers, amp)

    if cfg.dry_run:
        print("\n========== Dry Run Finished ==========")
        print("已完成环境、路径、数据和配置检查，未启动训练。")
        return None

    if cfg.resume:
        model_path = choose_resume_checkpoint(cfg)
    elif cfg.stage == "hard":
        model_path = cfg.base_weights
        print(f"\n使用第一阶段最佳权重微调: {model_path}")
    else:
        model_path = choose_model_path(cfg)
    YOLO = get_yolo()
    model = YOLO(model_path)

    print("\n========== Start Training ==========")

    train_kwargs = {
        "data": cfg.data_yaml,
        "epochs": cfg.epochs,
        "imgsz": cfg.imgsz,
        "rect": cfg.rect,
        "batch": cfg.batch,
        "device": device,
        "workers": workers,
        "project": cfg.project,
        "name": cfg.name,
        "exist_ok": cfg.exist_ok,
        "optimizer": cfg.optimizer,
        "lr0": cfg.lr0,
        "lrf": cfg.lrf,
        "momentum": cfg.momentum,
        "weight_decay": cfg.weight_decay,
        "warmup_epochs": cfg.warmup_epochs,
        "box": cfg.box,
        "cls": cfg.cls,
        "dfl": cfg.dfl,
        "mosaic": cfg.mosaic,
        "close_mosaic": cfg.close_mosaic,
        "mixup": cfg.mixup,
        "cutmix": cfg.cutmix,
        "copy_paste": cfg.copy_paste,
        "fliplr": cfg.fliplr,
        "flipud": cfg.flipud,
        "degrees": cfg.degrees,
        "translate": cfg.translate,
        "scale": cfg.scale,
        "shear": cfg.shear,
        "perspective": cfg.perspective,
        "hsv_h": cfg.hsv_h,
        "hsv_s": cfg.hsv_s,
        "hsv_v": cfg.hsv_v,
        "erasing": cfg.erasing,
        "patience": cfg.patience,
        "amp": amp,
        "deterministic": cfg.deterministic,
        "cache": cfg.cache,
        "pretrained": cfg.pretrained and is_weight_file(model_path),
        "plots": cfg.plots,
        "val": cfg.val,
        "save_period": cfg.save_period,
        "seed": cfg.seed,
        "resume": cfg.resume,
    }

    results = model.train(**train_kwargs)

    print("\n========== Training Finished ==========")
    print("结果目录:", Path(cfg.project) / cfg.name)

    return results


def validate_best(cfg: TrainConfig, split: str = "val"):
    cfg = normalize_cfg_paths(cfg)
    device, workers, _ = resolve_runtime(cfg)
    best_pt = Path(cfg.project) / cfg.name / "weights" / "best.pt"

    if not best_pt.exists():
        raise FileNotFoundError(f"找不到 best.pt: {best_pt}")

    if not split_available(cfg, split, require_labels=True):
        print(f"\n跳过 {split} 验证：缺少 images/{split} 或 labels/{split}。")
        return None

    print(f"\n========== Validate Best Model on {split} ==========")
    print("model:", best_pt)

    YOLO = get_yolo()
    model = YOLO(str(best_pt))

    metrics = model.val(
        data=cfg.data_yaml,
        split=split,
        imgsz=cfg.imgsz,
        rect=cfg.rect,
        batch=cfg.batch,
        device=device,
        workers=workers,
        project=cfg.project,
        name=f"{cfg.name}_{split}_eval",
        exist_ok=True,
        plots=True,
    )

    return metrics


def predict_samples(cfg: TrainConfig, split: str = "test", conf: float = 0.25):
    cfg = normalize_cfg_paths(cfg)
    device, _, _ = resolve_runtime(cfg)
    best_pt = Path(cfg.project) / cfg.name / "weights" / "best.pt"

    if not best_pt.exists():
        raise FileNotFoundError(f"找不到 best.pt: {best_pt}")

    if not split_available(cfg, split, require_labels=False):
        print(f"\n跳过 {split} 预测：缺少 images/{split}。")
        return None

    image_dir = Path(cfg.dataset_dir) / "images" / split

    print(f"\n========== Predict Samples on {split} ==========")
    print("model:", best_pt)
    print("source:", image_dir)

    YOLO = get_yolo()
    model = YOLO(str(best_pt))

    return model.predict(
        source=str(image_dir),
        imgsz=(cfg.input_height, cfg.input_width),
        rect=False,
        conf=conf,
        device=device,
        project=str(Path(cfg.project_root) / "runs" / "detect_predict"),
        name=f"{cfg.name}_{split}_pred",
        save=True,
        save_txt=True,
        save_conf=True,
        exist_ok=True,
        max_det=300,
    )


# =========================
# 4. 命令行入口
# =========================

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Train YOLO detection on the BDD100K YOLO dataset.")

    parser.add_argument(
        "--stage",
        choices=("base", "hard"),
        default="base",
        help="base 主训练或 hard 困难样本微调",
    )
    parser.add_argument("--dataset-dir", type=str, help="数据集目录，默认 bdd100k_yolo_det")
    parser.add_argument("--data-yaml", type=str, help="data.yaml 路径")
    parser.add_argument("--model", type=str, help="本地权重路径或 Ultralytics 模型名")
    parser.add_argument("--name", type=str, help="训练输出目录名")
    parser.add_argument("--project", type=str, help="训练输出根目录")

    parser.add_argument("--epochs", type=int, help="训练 epoch 数")
    parser.add_argument("--imgsz", type=int, help="训练最长边尺寸；矩形 batch 由 rect=True 控制")
    parser.add_argument("--input-width", type=int, help="部署目标输入宽度，默认 640")
    parser.add_argument("--input-height", type=int, help="部署目标输入高度，默认 384")
    parser.add_argument("--batch", type=int, help="batch size")
    parser.add_argument("--device", type=str, help="auto、cpu、0 或 0,1；默认 0,1 对应物理 GPU 3,4")
    parser.add_argument("--workers", type=int, help="DataLoader workers")
    parser.add_argument("--seed", type=int, help="随机种子")

    parser.add_argument("--optimizer", type=str, help="优化器，例如 AdamW、SGD、auto")
    parser.add_argument("--lr0", type=float, help="初始学习率")
    parser.add_argument("--lrf", type=float, help="最终学习率比例")
    parser.add_argument("--patience", type=int, help="early stopping patience")
    amp_group = parser.add_mutually_exclusive_group()
    amp_group.add_argument("--amp", dest="amp", action="store_true", default=None, help="开启 AMP")
    amp_group.add_argument("--no-amp", dest="amp", action="store_false", help="关闭 AMP")

    cache_group = parser.add_mutually_exclusive_group()
    cache_group.add_argument("--cache", dest="cache", action="store_true", default=None, help="开启数据缓存")
    cache_group.add_argument("--no-cache", dest="cache", action="store_false", help="关闭数据缓存")

    parser.add_argument("--mosaic", type=float, help="mosaic 增强概率")
    parser.add_argument("--mixup", type=float, help="mixup 增强概率")
    parser.add_argument("--copy-paste", dest="copy_paste", type=float, help="copy-paste 增强概率")
    parser.add_argument("--scale", type=float, help="随机缩放增强幅度")
    parser.add_argument("--erasing", type=float, help="random erasing 概率")
    parser.add_argument("--close-mosaic", type=int, help="最后多少个 epoch 关闭 mosaic")

    balance_group = parser.add_mutually_exclusive_group()
    balance_group.add_argument("--balance-long-tail", dest="balance_long_tail", action="store_true", default=None, help="开启长尾类轻量重复采样")
    balance_group.add_argument("--no-balance-long-tail", dest="balance_long_tail", action="store_false", help="关闭长尾类重复采样")
    parser.add_argument("--balance-repeat-power", type=float, help="长尾重复强度，0.5 表示平方根比例")
    parser.add_argument("--balance-max-repeat", type=int, help="单张图片最大重复次数")
    parser.add_argument("--balance-frequency-threshold", type=float, help="类别图片频率平衡阈值，默认 0.05")
    parser.add_argument("--balance-max-growth", type=float, help="平衡清单相对原始图片数的最大增长倍数")

    parser.add_argument("--base-weights", type=str, help="hard 阶段必需：第一阶段 best.pt")
    parser.add_argument("--hard-sample-list", type=str, help="hard 阶段必需：困难样本图片清单")
    parser.add_argument("--hard-sample-repeat", type=int, help="每个困难样本在基础清单上额外加入的次数")

    parser.add_argument("--max-check-images", type=int, help="每个 split 最多抽样检查多少张图，0 表示全部")
    parser.add_argument("--skip-checks", action="store_true", help="跳过图片、标签和可视化检查")
    parser.add_argument("--no-image-check", action="store_true", help="跳过图片读取检查")
    parser.add_argument("--no-label-check", action="store_true", help="跳过 YOLO 标签检查")
    visualize_group = parser.add_mutually_exclusive_group()
    visualize_group.add_argument("--visualize", dest="visualize_labels", action="store_true", default=None, help="生成标签可视化图")
    visualize_group.add_argument("--no-visualize", dest="visualize_labels", action="store_false", help="跳过标签可视化")
    parser.add_argument("--remove-cache", action="store_true", help="训练前删除 labels/*.cache")

    parser.add_argument("--no-val-eval", action="store_true", help="训练后不额外验证 val")
    parser.add_argument("--no-test-eval", action="store_true", help="训练后不验证 test")
    parser.add_argument("--no-predict", action="store_true", help="训练后不保存 test 预测")
    parser.add_argument("--predict-conf", type=float, help="预测置信度阈值")
    parser.add_argument("--dry-run", action="store_true", help="只做检查，不启动训练")
    parser.add_argument("--resume", action="store_true", help="从当前 run 的 weights/last.pt 恢复训练")

    return parser


def build_config(argv: Optional[Sequence[str]] = None) -> TrainConfig:
    parser = build_parser()
    args = parser.parse_args(argv)
    cfg = TrainConfig()
    cfg.stage = args.stage

    if cfg.stage == "hard":
        cfg.name = "bdd100k_yolo26s_det_640x384_stage2"
        cfg.epochs = 30
        cfg.optimizer = "AdamW"
        cfg.lr0 = 1e-4
        cfg.lrf = 0.1
        cfg.warmup_epochs = 1.0
        cfg.patience = 10
        cfg.scale = 0.15
        cfg.erasing = 0.05

    if args.dataset_dir is not None and args.data_yaml is None:
        cfg.data_yaml = str(Path(args.dataset_dir) / "data.yaml")

    overrides = {
        "dataset_dir": args.dataset_dir,
        "data_yaml": args.data_yaml,
        "model_path": args.model,
        "name": args.name,
        "project": args.project,
        "epochs": args.epochs,
        "imgsz": args.imgsz,
        "input_width": args.input_width,
        "input_height": args.input_height,
        "batch": args.batch,
        "device": args.device,
        "workers": args.workers,
        "seed": args.seed,
        "optimizer": args.optimizer,
        "lr0": args.lr0,
        "lrf": args.lrf,
        "patience": args.patience,
        "mosaic": args.mosaic,
        "mixup": args.mixup,
        "copy_paste": args.copy_paste,
        "scale": args.scale,
        "erasing": args.erasing,
        "close_mosaic": args.close_mosaic,
        "balance_repeat_power": args.balance_repeat_power,
        "balance_max_repeat": args.balance_max_repeat,
        "balance_frequency_threshold": args.balance_frequency_threshold,
        "balance_max_growth": args.balance_max_growth,
        "base_weights": args.base_weights,
        "hard_sample_list": args.hard_sample_list,
        "hard_sample_repeat": args.hard_sample_repeat,
        "max_check_images": args.max_check_images,
        "predict_conf": args.predict_conf,
    }

    for key, value in overrides.items():
        if value is not None:
            setattr(cfg, key, value)

    if args.amp is not None:
        cfg.amp = args.amp
    if args.cache is not None:
        cfg.cache = args.cache
    if args.balance_long_tail is not None:
        cfg.balance_long_tail = args.balance_long_tail
    if args.skip_checks:
        cfg.check_images = False
        cfg.check_labels = False
        cfg.visualize_labels = False
    if args.no_image_check:
        cfg.check_images = False
    if args.no_label_check:
        cfg.check_labels = False
    if args.visualize_labels is not None:
        cfg.visualize_labels = args.visualize_labels
    if args.remove_cache:
        cfg.remove_old_cache = True

    if args.no_val_eval:
        cfg.run_val_eval = False
    if args.no_test_eval:
        cfg.run_test_eval = False
    if args.no_predict:
        cfg.run_predict = False

    cfg.dry_run = args.dry_run
    cfg.resume = args.resume

    return normalize_cfg_paths(cfg)


def main(argv: Optional[Sequence[str]] = None) -> None:
    cfg = build_config(argv)

    train(cfg)

    if cfg.dry_run:
        return

    if cfg.run_val_eval:
        validate_best(cfg, split="val")

    if cfg.run_test_eval:
        validate_best(cfg, split="test")

    if cfg.run_predict:
        predict_samples(cfg, split="test", conf=cfg.predict_conf)


if __name__ == "__main__":
    main()
