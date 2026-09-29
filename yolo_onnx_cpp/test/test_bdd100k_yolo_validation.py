import tempfile
from pathlib import Path
import sys

import cv2
import numpy as np


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import evaluate_bdd100k_yolo_validation as evaluator
import prepare_bdd100k_yolo_validation as preparer


def assert_raises(exception_type, function, expected_text: str) -> None:
    try:
        function()
    except exception_type as exc:
        assert expected_text in str(exc), str(exc)
    else:
        raise AssertionError(f"expected {exception_type.__name__}")


def create_dataset(root: Path, class_groups: list[list[int]]) -> Path:
    image_dir = root / "images" / "val"
    label_dir = root / "labels" / "val"
    image_dir.mkdir(parents=True)
    label_dir.mkdir(parents=True)
    for index, class_ids in enumerate(class_groups):
        sample_id = f"sample_{index:02d}"
        image = np.full((32, 64, 3), index, dtype=np.uint8)
        assert cv2.imwrite(str(image_dir / f"{sample_id}.jpg"), image)
        (label_dir / f"{sample_id}.txt").write_text(
            "".join(f"{class_id} 0.5 0.5 0.25 0.5\n" for class_id in class_ids),
            encoding="utf-8",
        )
    return root.resolve()


def prepare_subset(dataset_dir: Path, output_dir: Path, max_images: int = 4) -> dict:
    candidates = preparer.collect_candidates(dataset_dir)
    forced_class_ids = preparer.parse_forced_classes(["train"])
    selected, forced_sample_ids = preparer.select_candidates(
        candidates,
        max_images,
        42,
        forced_class_ids,
    )
    payload = preparer.build_payload(
        dataset_dir,
        candidates,
        selected,
        forced_sample_ids,
        forced_class_ids,
        max_images,
        42,
    )
    preparer.write_outputs(output_dir, payload)
    return payload


def test_selection_is_deterministic_and_keeps_all_forced_images() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        dataset_dir = create_dataset(
            Path(temporary_dir) / "dataset",
            [[0], [5], [2], [5, 2], [7]],
        )
        candidates = preparer.collect_candidates(dataset_dir)
        forced = preparer.parse_forced_classes(["train"])

        first, first_forced = preparer.select_candidates(candidates, 3, 42, forced)
        second, second_forced = preparer.select_candidates(
            list(reversed(candidates)), 3, 42, forced
        )

        assert [item.sample_id for item in first] == [item.sample_id for item in second]
        assert first_forced == second_forced == {"sample_01", "sample_03"}
        assert first_forced.issubset({item.sample_id for item in first})


def test_manifest_round_trip_preserves_boxes_and_ten_class_schema() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir)
        dataset_dir = create_dataset(root / "dataset", [[0, 8], [5], [2, 9], [7]])
        output_dir = root / "subset"
        payload = prepare_subset(dataset_dir, output_dir)

        images, labels, crowds, strides, loaded = evaluator.load_validation_ground_truth(
            output_dir / "manifest.json",
            dataset_dir,
            list(preparer.BDD100K_NAMES),
        )

        assert len(images) == 4
        assert loaded["dataset"]["classes"] == list(preparer.BDD100K_NAMES)
        assert sum(len(items[0]) for items in labels.values()) == 6
        assert all(items == {0: []} for items in crowds.values())
        assert set(strides.values()) == {1}
        assert payload["selection"]["selected_class_boxes"]["traffic light"] == 1
        assert payload["selection"]["selected_class_boxes"]["traffic sign"] == 1


def test_source_hash_tampering_is_rejected() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir)
        dataset_dir = create_dataset(root / "dataset", [[5], [2], [0]])
        output_dir = root / "subset"
        payload = prepare_subset(dataset_dir, output_dir, max_images=3)
        first_label = dataset_dir / payload["samples"][0]["label"]
        first_label.write_text(
            first_label.read_text(encoding="utf-8") + "0 0.5 0.5 0.1 0.1\n",
            encoding="utf-8",
        )

        assert_raises(
            evaluator.ValidationEvaluationError,
            lambda: evaluator.load_validation_ground_truth(
                output_dir / "manifest.json",
                dataset_dir,
                list(preparer.BDD100K_NAMES),
            ),
            "Label hash mismatch",
        )


def test_output_overwrite_and_relative_source_guards() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir)
        dataset_dir = create_dataset(root / "dataset", [[5], [2]])
        output_dir = root / "subset"
        payload = prepare_subset(dataset_dir, output_dir, max_images=2)

        assert_raises(
            preparer.ValidationPreparationError,
            lambda: preparer.write_outputs(output_dir, payload),
            "already exists",
        )
        assert_raises(
            evaluator.ValidationEvaluationError,
            lambda: evaluator.source_path(dataset_dir, "../outside.jpg", "image"),
            "must stay relative",
        )


def test_compact_validation_metrics_omit_per_sample_details() -> None:
    record = evaluator.evaluation.ObjectRecord
    labels = {"sample": {0: [record(0, "gt", "traffic sign", (0, 0, 10, 10))]}}
    predictions = {
        "sample": {
            0: [record(0, "prediction", "traffic sign", (0, 0, 10, 10), 0.9)]
        }
    }
    result = evaluator.evaluation.evaluate_predictions(
        predictions,
        labels,
        {"sample": {0: []}},
        [0.25],
        0.001,
        0.5,
        include_per_source=False,
    )

    assert "per_video" not in result["operating_points"]["0.001"]
    assert result["operating_points"]["0.001"]["pooled"]["map50"] == 1.0



if __name__ == "__main__":
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    for test in tests:
        test()
    print(f"{len(tests)} tests passed")
