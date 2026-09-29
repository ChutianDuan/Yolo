from __future__ import annotations

import csv
import json
import os
from contextlib import contextmanager
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterator, Mapping, Sequence

import torch
import torch.nn.functional as F


DISTILLATION_ENV = "YOLO_BDD100K_DISTILLATION_CONFIG"
REPOSITORY_ROOT = Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class DistillationSettings:
    teacher_model: str
    temperature: float = 2.0
    cls_weight: float = 0.5
    box_weight: float = 0.25
    confidence_threshold: float = 0.05
    background_weight: float = 0.05
    class_names: tuple[str, ...] = ()

    def validate(self, require_teacher: bool = True) -> None:
        if require_teacher and not Path(self.teacher_model).is_file():
            raise FileNotFoundError(f"teacher 权重不存在: {self.teacher_model}")
        if self.temperature <= 0.0:
            raise ValueError("distill_temperature 必须大于 0")
        if self.cls_weight < 0.0 or self.box_weight < 0.0:
            raise ValueError("distill_cls_weight 和 distill_box_weight 不能小于 0")
        if self.cls_weight == 0.0 and self.box_weight == 0.0:
            raise ValueError("蒸馏分类和 box 权重不能同时为 0")
        if not 0.0 <= self.confidence_threshold <= 1.0:
            raise ValueError("distill_confidence_threshold 必须在 [0, 1] 范围内")
        if not 0.0 <= self.background_weight <= 1.0:
            raise ValueError("distill_background_weight 必须在 [0, 1] 范围内")

    def to_json(self) -> str:
        payload = asdict(self)
        payload["class_names"] = list(self.class_names)
        return json.dumps(payload, ensure_ascii=True, sort_keys=True)

    @classmethod
    def from_json(cls, value: str) -> "DistillationSettings":
        payload = json.loads(value)
        if not isinstance(payload, dict):
            raise ValueError("蒸馏配置必须是 JSON object")
        unknown = set(payload) - set(cls.__dataclass_fields__)
        if unknown:
            raise ValueError(f"蒸馏配置包含未知字段: {', '.join(sorted(unknown))}")
        payload["class_names"] = tuple(payload.get("class_names", ()))
        settings = cls(**payload)
        settings.validate(require_teacher=False)
        return settings

    @classmethod
    def from_environment(cls) -> "DistillationSettings":
        value = os.environ.get(DISTILLATION_ENV)
        if not value:
            raise RuntimeError(f"缺少蒸馏运行配置环境变量: {DISTILLATION_ENV}")
        return cls.from_json(value)


@contextmanager
def distillation_environment(settings: DistillationSettings) -> Iterator[None]:
    """Expose settings to Ultralytics DDP children and restore the caller environment."""
    settings.validate()
    previous = os.environ.get(DISTILLATION_ENV)
    previous_pythonpath = os.environ.get("PYTHONPATH")
    python_paths = previous_pythonpath.split(os.pathsep) if previous_pythonpath else []
    repository_root = str(REPOSITORY_ROOT)
    if repository_root not in python_paths:
        os.environ["PYTHONPATH"] = os.pathsep.join((repository_root, *python_paths))
    os.environ[DISTILLATION_ENV] = settings.to_json()
    try:
        yield
    finally:
        if previous is None:
            os.environ.pop(DISTILLATION_ENV, None)
        else:
            os.environ[DISTILLATION_ENV] = previous
        if previous_pythonpath is None:
            os.environ.pop("PYTHONPATH", None)
        else:
            os.environ["PYTHONPATH"] = previous_pythonpath


def _prediction_branches(predictions: Any) -> Mapping[str, Mapping[str, torch.Tensor]]:
    if isinstance(predictions, tuple):
        predictions = predictions[1]
    if not isinstance(predictions, Mapping):
        raise ValueError("检测头输出必须是 mapping 或 (decoded, mapping)")
    if "boxes" in predictions and "scores" in predictions:
        return {"detect": predictions}
    branches = {
        name: predictions[name]
        for name in ("one2many", "one2one")
        if name in predictions
    }
    if not branches:
        raise ValueError("检测头输出缺少 boxes/scores 或 one2many/one2one 分支")
    return branches


def _bernoulli_kl(
    student_logits: torch.Tensor,
    teacher_logits: torch.Tensor,
    temperature: float,
) -> torch.Tensor:
    student_scaled = student_logits / temperature
    teacher_scaled = teacher_logits / temperature
    teacher_probability = teacher_scaled.sigmoid()
    return (
        teacher_probability
        * (F.logsigmoid(teacher_scaled) - F.logsigmoid(student_scaled))
        + (1.0 - teacher_probability)
        * (F.logsigmoid(-teacher_scaled) - F.logsigmoid(-student_scaled))
    ) * (temperature * temperature)


def response_distillation_loss(
    student_predictions: Any,
    teacher_predictions: Any,
    settings: DistillationSettings,
) -> tuple[torch.Tensor, dict[str, torch.Tensor]]:
    """Match teacher class logits everywhere and box responses on confident anchors."""
    settings.validate(require_teacher=False)
    student_branches = _prediction_branches(student_predictions)
    teacher_branches = _prediction_branches(teacher_predictions)
    if tuple(student_branches) != tuple(teacher_branches):
        raise ValueError(
            "teacher/student 检测分支不一致: "
            f"student={tuple(student_branches)}, teacher={tuple(teacher_branches)}"
        )

    cls_losses = []
    box_losses = []
    confident_counts = []
    for branch_name, student_branch in student_branches.items():
        teacher_branch = teacher_branches[branch_name]
        # AMP responses can overflow FP16 reductions even for one 640px image.
        # Cast before both the elementwise loss and its normalization.
        student_scores = student_branch["scores"].float()
        teacher_scores = teacher_branch["scores"].detach().float()
        student_boxes = student_branch["boxes"].float()
        teacher_boxes = teacher_branch["boxes"].detach().float()

        if student_scores.shape != teacher_scores.shape:
            raise ValueError(
                f"{branch_name} score shape 不一致: "
                f"student={tuple(student_scores.shape)}, teacher={tuple(teacher_scores.shape)}"
            )
        if student_boxes.shape != teacher_boxes.shape:
            raise ValueError(
                f"{branch_name} box shape 不一致: "
                f"student={tuple(student_boxes.shape)}, teacher={tuple(teacher_boxes.shape)}"
            )
        if student_scores.ndim != 3 or student_boxes.ndim != 3:
            raise ValueError(f"{branch_name} 检测响应必须是 [batch, channels, anchors]")

        teacher_confidence = teacher_scores.sigmoid().amax(dim=1)
        confident = teacher_confidence >= settings.confidence_threshold
        anchor_weight = torch.where(
            confident,
            torch.ones_like(teacher_confidence),
            torch.full_like(teacher_confidence, settings.background_weight),
        )

        element_kl = _bernoulli_kl(
            student_scores,
            teacher_scores,
            settings.temperature,
        )
        cls_denominator = (anchor_weight.sum() * student_scores.shape[1]).clamp_min(1.0)
        cls_losses.append((element_kl * anchor_weight.unsqueeze(1)).sum() / cls_denominator)

        per_anchor_box = F.smooth_l1_loss(
            student_boxes,
            teacher_boxes,
            reduction="none",
        ).mean(dim=1)
        confident_float = confident.to(per_anchor_box.dtype)
        box_losses.append(
            (per_anchor_box * confident_float).sum() / confident_float.sum().clamp_min(1.0)
        )
        confident_counts.append(confident_float.sum())

    cls_loss = torch.stack(cls_losses).mean()
    box_loss = torch.stack(box_losses).mean()
    total = settings.cls_weight * cls_loss + settings.box_weight * box_loss
    terms = {
        "classification": cls_loss.detach(),
        "box": box_loss.detach(),
        "total": total.detach(),
        "confident_anchors": torch.stack(confident_counts).sum().detach(),
    }
    return total, terms


class DistillationCriterion:
    """Combine response KD with training loss and retain auditable epoch totals."""

    def __init__(self, base_criterion: Any, teacher_model: torch.nn.Module, settings: DistillationSettings):
        self.base_criterion = base_criterion
        self.teacher_model = teacher_model
        self.settings = settings
        self.last_terms: dict[str, torch.Tensor] = {}
        self.metric_sums: dict[str, torch.Tensor] = {}
        self.metric_batches = 0

    def __call__(self, predictions: Any, batch: dict[str, torch.Tensor]):
        base_loss, base_items = self.base_criterion(predictions, batch)
        if not torch.is_grad_enabled():
            return base_loss, base_items

        with torch.no_grad():
            teacher_predictions = self.teacher_model.predict(batch["img"])
        distill_loss, self.last_terms = response_distillation_loss(
            predictions,
            teacher_predictions,
            self.settings,
        )
        for name, value in self.last_terms.items():
            detached = value.detach().float()
            if name not in self.metric_sums:
                self.metric_sums[name] = detached.clone()
            else:
                self.metric_sums[name] += detached
        self.metric_batches += 1
        batch_size = int(batch["img"].shape[0])
        scaled_distill_loss = distill_loss * batch_size

        if base_loss.ndim == 0:
            return base_loss + scaled_distill_loss, base_items + distill_loss.detach()
        if base_loss.numel() < 2 or base_items.numel() < 2:
            raise ValueError("检测损失必须至少包含 box 和 cls 两项")
        combined_loss = base_loss.clone()
        combined_items = base_items.clone()
        combined_loss[1] = combined_loss[1] + scaled_distill_loss
        combined_items[1] = combined_items[1] + distill_loss.detach()
        return combined_loss, combined_items

    def consume_metric_totals(self) -> tuple[dict[str, torch.Tensor], int]:
        totals = self.metric_sums
        batches = self.metric_batches
        self.metric_sums = {}
        self.metric_batches = 0
        return totals, batches

    def update(self) -> None:
        update = getattr(self.base_criterion, "update", None)
        if update is not None:
            update()


def _ordered_names(names: Mapping[int, str] | Sequence[str]) -> tuple[str, ...]:
    if isinstance(names, Mapping):
        return tuple(str(names[index]) for index in sorted(names))
    return tuple(str(name) for name in names)


def validate_model_compatibility(
    student_model: torch.nn.Module,
    teacher_model: torch.nn.Module,
    expected_names: Sequence[str],
) -> None:
    student_head = student_model.model[-1]
    teacher_head = teacher_model.model[-1]
    expected_names = tuple(expected_names)
    student_names = _ordered_names(student_model.names)
    teacher_names = _ordered_names(teacher_model.names)

    if student_names != expected_names or teacher_names != expected_names:
        raise ValueError(
            "teacher/student 类别顺序必须与数据集完全一致: "
            f"expected={expected_names}, student={student_names}, teacher={teacher_names}"
        )
    for attribute in ("nc", "nl", "reg_max"):
        student_value = getattr(student_head, attribute, None)
        teacher_value = getattr(teacher_head, attribute, None)
        if student_value != teacher_value:
            raise ValueError(
                f"teacher/student {attribute} 不一致: "
                f"student={student_value}, teacher={teacher_value}"
            )
    if not getattr(student_model, "end2end", False) or not getattr(teacher_model, "end2end", False):
        raise ValueError("当前蒸馏实现要求 teacher/student 都是 end-to-end 检测模型")
    if not torch.equal(student_head.stride.cpu(), teacher_head.stride.cpu()):
        raise ValueError(
            "teacher/student stride 不一致: "
            f"student={tuple(student_head.stride.tolist())}, "
            f"teacher={tuple(teacher_head.stride.tolist())}"
        )


def prepare_teacher_for_raw_outputs(teacher_model: torch.nn.Module) -> torch.nn.Module:
    teacher_model.eval()
    for parameter in teacher_model.parameters():
        parameter.requires_grad_(False)
    # Only the Detect module itself needs training=True to return raw branches. Its
    # child BatchNorm modules stay in eval mode because train() is not called here.
    teacher_model.model[-1].training = True
    return teacher_model


def attach_distillation_criterion(
    student_model: torch.nn.Module,
    teacher_model: torch.nn.Module,
    settings: DistillationSettings,
) -> DistillationCriterion:
    settings.validate()
    validate_model_compatibility(student_model, teacher_model, settings.class_names)
    prepare_teacher_for_raw_outputs(teacher_model)
    criterion = DistillationCriterion(student_model.init_criterion(), teacher_model, settings)
    student_model.criterion = criterion
    return criterion


def _load_teacher(path: str, device: torch.device) -> torch.nn.Module:
    from ultralytics import YOLO

    teacher_path = Path(path)
    if not teacher_path.is_file():
        raise FileNotFoundError(f"teacher 权重不存在: {teacher_path}")
    teacher = YOLO(str(teacher_path))
    if teacher.task != "detect":
        raise ValueError(f"teacher 必须是 detect 模型，实际为: {teacher.task}")
    return teacher.model.to(device)


def _trainer_base():
    from ultralytics.models.yolo.detect.train import DetectionTrainer

    return DetectionTrainer


class DistillationDetectionTrainer(_trainer_base()):
    """Ultralytics detection trainer with a frozen local teacher on every DDP rank."""

    def __init__(self, *args: Any, **kwargs: Any):
        self.distillation_settings = DistillationSettings.from_environment()
        self.teacher_model: torch.nn.Module | None = None
        super().__init__(*args, **kwargs)

    def _record_distillation_epoch(self, trainer: Any) -> None:
        from ultralytics.utils import RANK
        from ultralytics.utils.torch_utils import unwrap_model

        if trainer is not self:
            raise RuntimeError("蒸馏 epoch 回调收到未知 trainer")
        criterion = getattr(unwrap_model(self.model), "criterion", None)
        if not isinstance(criterion, DistillationCriterion):
            raise RuntimeError("student 未挂载 DistillationCriterion")
        totals, batches = criterion.consume_metric_totals()
        if batches < 1:
            return

        keys = ("classification", "box", "total", "confident_anchors")
        payload = torch.stack([totals[key] for key in keys])
        batch_count = payload.new_tensor(float(batches))
        payload = torch.cat((payload, batch_count.reshape(1)))
        if torch.distributed.is_available() and torch.distributed.is_initialized():
            torch.distributed.all_reduce(payload)

        if RANK not in {-1, 0}:
            return
        global_batches = max(int(payload[-1].item()), 1)
        averages = payload[:-1] / global_batches
        metrics_path = self.save_dir / "distillation_metrics.csv"
        fieldnames = (
            "epoch",
            "rank_batches",
            "classification_loss",
            "box_loss",
            "total_loss",
            "confident_anchors_per_batch",
        )
        write_header = not metrics_path.exists()
        with metrics_path.open("a", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            if write_header:
                writer.writeheader()
            writer.writerow(
                {
                    "epoch": self.epoch + 1,
                    "rank_batches": global_batches,
                    "classification_loss": f"{float(averages[0]):.8g}",
                    "box_loss": f"{float(averages[1]):.8g}",
                    "total_loss": f"{float(averages[2]):.8g}",
                    "confident_anchors_per_batch": f"{float(averages[3]):.8g}",
                }
            )

    def _setup_train(self) -> None:
        from ultralytics.utils import LOGGER
        from ultralytics.utils.torch_utils import unwrap_model

        super()._setup_train()
        student_model = unwrap_model(self.model)
        teacher_model = _load_teacher(
            self.distillation_settings.teacher_model,
            self.device,
        )
        attach_distillation_criterion(
            student_model,
            teacher_model,
            self.distillation_settings,
        )
        self.add_callback("on_train_epoch_end", self._record_distillation_epoch)
        self.teacher_model = teacher_model
        LOGGER.info(
            "Distillation teacher ready: %s (T=%.3g, cls=%.3g, box=%.3g, conf=%.3g, bg=%.3g)",
            self.distillation_settings.teacher_model,
            self.distillation_settings.temperature,
            self.distillation_settings.cls_weight,
            self.distillation_settings.box_weight,
            self.distillation_settings.confidence_threshold,
            self.distillation_settings.background_weight,
        )
