from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

import torch

MODEL_DIR = Path(__file__).resolve().parents[2] / "model"
sys.path.insert(0, str(MODEL_DIR))

import distillation
import train as training


def assert_raises(exception_type, function, expected_text: str) -> None:
    try:
        function()
    except exception_type as exc:
        assert expected_text in str(exc), str(exc)
    else:
        raise AssertionError(f"expected {exception_type.__name__}")


def make_predictions(
    scores: torch.Tensor,
    boxes: torch.Tensor | None = None,
) -> dict[str, dict[str, torch.Tensor]]:
    if boxes is None:
        boxes = torch.zeros(scores.shape[0], 4, scores.shape[2], dtype=scores.dtype)
    return {
        "one2many": {"scores": scores, "boxes": boxes},
        "one2one": {"scores": scores.clone(), "boxes": boxes.clone()},
    }


def settings(**overrides) -> distillation.DistillationSettings:
    values = {
        "teacher_model": "unused.pt",
        "temperature": 2.0,
        "cls_weight": 0.5,
        "box_weight": 0.25,
        "confidence_threshold": 0.5,
        "background_weight": 0.05,
        "class_names": ("person", "car"),
    }
    values.update(overrides)
    return distillation.DistillationSettings(**values)


def test_identical_responses_have_zero_distillation_loss() -> None:
    scores = torch.tensor([[[1.0, -2.0], [0.5, 3.0]]], requires_grad=True)
    boxes = torch.tensor(
        [[[0.1, 0.2], [0.3, 0.4], [0.5, 0.6], [0.7, 0.8]]],
        requires_grad=True,
    )
    student = make_predictions(scores, boxes)
    teacher = make_predictions(scores.detach().clone(), boxes.detach().clone())

    loss, terms = distillation.response_distillation_loss(student, teacher, settings())

    assert abs(float(loss.detach())) < 1e-6
    assert abs(float(terms["classification"])) < 1e-6
    assert float(terms["box"]) == 0.0
    loss.backward()
    assert scores.grad is not None
    assert boxes.grad is not None


def test_changed_logits_create_positive_bernoulli_kl() -> None:
    student_scores = torch.zeros(1, 2, 3, requires_grad=True)
    teacher_scores = torch.full((1, 2, 3), 2.0)
    loss, terms = distillation.response_distillation_loss(
        make_predictions(student_scores),
        make_predictions(teacher_scores),
        settings(box_weight=0.0),
    )

    assert float(loss.detach()) > 0.0
    assert float(terms["classification"]) > 0.0
    loss.backward()
    assert student_scores.grad is not None
    assert torch.count_nonzero(student_scores.grad) > 0


def test_half_precision_responses_match_float_loss_and_gradients() -> None:
    # 8400 anchors * 10 classes exceeds the largest finite FP16 denominator.
    for anchors in (8400, 66000):
        student_scores = torch.zeros(1, 10, anchors, dtype=torch.float16, requires_grad=True)
        student_boxes = torch.ones(1, 4, anchors, dtype=torch.float16, requires_grad=True)
        teacher_scores = torch.full_like(student_scores, 2.0, requires_grad=True)
        teacher_boxes = torch.zeros_like(student_boxes, requires_grad=True)
        loss, terms = distillation.response_distillation_loss(
            make_predictions(student_scores, student_boxes),
            make_predictions(teacher_scores, teacher_boxes),
            settings(),
        )
        reference, _ = distillation.response_distillation_loss(
            make_predictions(student_scores.detach().float(), student_boxes.detach().float()),
            make_predictions(teacher_scores.detach().float(), teacher_boxes.detach().float()),
            settings(),
        )
        assert torch.isfinite(loss) and loss > 0
        assert torch.allclose(loss, reference, rtol=1e-5, atol=1e-6)
        assert terms["confident_anchors"].item() == 2 * anchors
        loss.backward()
        for value in (student_scores, student_boxes):
            assert value.grad is not None
            assert torch.isfinite(value.grad).all()
            assert torch.count_nonzero(value.grad) > 0
        assert teacher_scores.grad is None and teacher_boxes.grad is None


def test_box_loss_uses_only_confident_teacher_anchors() -> None:
    teacher_scores = torch.tensor([[[10.0, -10.0], [-10.0, -10.0]]])
    student_scores = teacher_scores.clone().requires_grad_(True)
    teacher_boxes = torch.zeros(1, 4, 2)
    background_only_difference = teacher_boxes.clone()
    background_only_difference[:, :, 1] = 3.0

    _, ignored_terms = distillation.response_distillation_loss(
        make_predictions(student_scores, background_only_difference),
        make_predictions(teacher_scores, teacher_boxes),
        settings(confidence_threshold=0.9),
    )
    assert float(ignored_terms["box"]) == 0.0

    confident_difference = teacher_boxes.clone()
    confident_difference[:, :, 0] = 3.0
    _, used_terms = distillation.response_distillation_loss(
        make_predictions(student_scores, confident_difference),
        make_predictions(teacher_scores, teacher_boxes),
        settings(confidence_threshold=0.9),
    )
    assert float(used_terms["box"]) > 0.0
    assert int(used_terms["confident_anchors"]) == 2


def test_response_shape_mismatch_is_rejected() -> None:
    student = make_predictions(torch.zeros(1, 2, 3))
    teacher = make_predictions(torch.zeros(1, 2, 4))
    assert_raises(
        ValueError,
        lambda: distillation.response_distillation_loss(student, teacher, settings()),
        "score shape 不一致",
    )


def test_criterion_keeps_native_loss_shape_and_accumulates_metrics() -> None:
    student_scores = torch.zeros(2, 2, 3, requires_grad=True)
    teacher_scores = torch.full((2, 2, 3), 2.0)
    student_predictions = make_predictions(student_scores)
    teacher_predictions = make_predictions(teacher_scores)

    class FakeBaseCriterion:
        def __call__(self, predictions, batch):
            anchor = predictions["one2many"]["scores"].sum() * 0.0
            values = torch.stack((anchor + 1.0, anchor + 2.0, anchor + 3.0))
            return values, values.detach()

    class FakeTeacher:
        def __init__(self):
            self.calls = 0

        def predict(self, image):
            self.calls += 1
            return teacher_predictions

    teacher = FakeTeacher()
    criterion = distillation.DistillationCriterion(
        FakeBaseCriterion(),
        teacher,
        settings(box_weight=0.0),
    )
    batch = {"img": torch.zeros(2, 3, 8, 8)}

    loss, items = criterion(student_predictions, batch)
    assert loss.shape == (3,)
    assert items.shape == (3,)
    assert torch.isclose(loss[1].detach() - 2.0, (items[1] - 2.0) * 2)
    assert float(items[1]) > 2.0
    totals, batches = criterion.consume_metric_totals()
    assert batches == 1
    assert float(totals["total"]) > 0.0
    assert teacher.calls == 1

    with torch.no_grad():
        validation_loss, validation_items = criterion(student_predictions, batch)
    assert validation_loss.shape == (3,)
    assert validation_items.shape == (3,)
    assert teacher.calls == 1


def test_distillation_environment_round_trip_and_restore() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        teacher = Path(temporary_dir) / "teacher.pt"
        teacher.write_bytes(b"local-test-weight")
        current = settings(teacher_model=str(teacher))
        previous = os.environ.get(distillation.DISTILLATION_ENV)
        previous_pythonpath = os.environ.get("PYTHONPATH")

        with distillation.distillation_environment(current):
            restored = distillation.DistillationSettings.from_environment()
            assert restored == current
            assert str(distillation.REPOSITORY_ROOT) in os.environ["PYTHONPATH"].split(os.pathsep)

        assert os.environ.get(distillation.DISTILLATION_ENV) == previous
        assert os.environ.get("PYTHONPATH") == previous_pythonpath


def test_distill_stage_requires_local_weights_and_unique_run() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir)
        teacher = root / "teacher.pt"
        student = root / "student.pt"
        teacher.write_bytes(b"teacher")
        student.write_bytes(b"student")
        cfg = training.TrainConfig(
            project_root=str(root),
            project=str(root / "runs"),
            stage="distill",
            teacher_model=str(teacher),
            model_path=str(student),
            name="new-distill-run",
        )
        training.validate_training_config(cfg)

        cfg.distill_temperature = 0.0
        assert_raises(
            ValueError,
            lambda: training.validate_training_config(cfg),
            "distill_temperature",
        )
        cfg.distill_temperature = 2.0
        (Path(cfg.project) / cfg.name).mkdir(parents=True)
        assert_raises(
            FileExistsError,
            lambda: training.validate_training_config(cfg),
            "使用新的 --name",
        )


def test_distill_cli_defaults_use_local_model_paths() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        cfg = training.build_config(
            [
                "--stage",
                "distill",
                "--project",
                temporary_dir,
                "--dry-run",
            ]
        )
        assert Path(cfg.model_path).resolve() == training.REPOSITORY_ROOT / "yolo26n.pt"
        expected_teacher = training.ROOT / "runs/detect/bdd100k_yolo26s_det_1280x736/weights/best.pt"
        assert Path(cfg.teacher_model).resolve() == expected_teacher
        assert cfg.name == "bdd100k_yolo26n_det_640x384_distill_v1"


def main() -> None:
    tests = sorted(
        (
            function
            for name, function in globals().items()
            if name.startswith("test_") and callable(function)
        ),
        key=lambda function: function.__name__,
    )
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
    print(f"{len(tests)} tests passed")


if __name__ == "__main__":
    main()
