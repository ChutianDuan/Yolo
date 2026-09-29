from __future__ import annotations

import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import mine_hard_samples as miner
import train as training


def assert_raises(exception_type, function, expected_text: str) -> None:
    try:
        function()
    except exception_type as exc:
        assert expected_text in str(exc), str(exc)
    else:
        raise AssertionError(f"expected {exception_type.__name__}")


def create_dataset(root: Path, class_ids: list[int]) -> list[Path]:
    image_dir = root / "images" / "train"
    label_dir = root / "labels" / "train"
    image_dir.mkdir(parents=True)
    label_dir.mkdir(parents=True)

    images = []
    for index, class_id in enumerate(class_ids):
        image_path = image_dir / f"frame_{index:02d}.jpg"
        image_path.write_bytes(b"test")
        (label_dir / f"frame_{index:02d}.txt").write_text(
            f"{class_id} 0.5 0.5 0.2 0.2\n",
            encoding="utf-8",
        )
        images.append(image_path.resolve())
    return images


def write_path_list(path: Path, images: list[Path]) -> None:
    path.write_text("".join(f"{image}\n" for image in images), encoding="utf-8")


def test_long_tail_sampling_is_deterministic_and_growth_limited() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir)
        images = create_dataset(root / "dataset", [0] * 9 + [5])
        cfg = training.TrainConfig(
            dataset_dir=str(root / "dataset"),
            balance_frequency_threshold=1.0,
            balance_repeat_power=1.0,
            balance_max_repeat=4,
            balance_max_growth=1.2,
            seed=42,
        )

        first_path, first_summary = training.build_long_tail_train_list(cfg, root / "first")
        second_path, second_summary = training.build_long_tail_train_list(cfg, root / "second")
        first = training.read_image_path_list(first_path)
        second = training.read_image_path_list(second_path)

        assert first == second
        assert len(first) == 12
        assert first.count(images[-1]) == 3
        assert first_summary["growth_cap_applied"] is True
        assert first_summary["classes"]["5:train"]["original_images"] == 1
        assert first_summary["classes"]["5:train"]["effective_images"] == 3
        assert second_summary["growth_ratio"] == 1.2


def test_hard_sample_manifest_requires_exact_train_path() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir)
        images = create_dataset(root / "dataset", [0])
        external = root / images[0].name
        external.write_bytes(b"external")
        manifest = root / "hard_samples.txt"
        write_path_list(manifest, [external.resolve()])
        cfg = training.TrainConfig(
            dataset_dir=str(root / "dataset"),
            hard_sample_list=str(manifest),
        )

        assert_raises(
            ValueError,
            lambda: training.validate_hard_sample_list(cfg),
            "不属于 images/train",
        )

        write_path_list(manifest, images)
        assert training.validate_hard_sample_list(cfg) == images


def test_miner_matches_full_training_path_without_resolving_symlinks() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir)
        dataset = root / "dataset"
        train_dir = dataset / "images" / "train"
        label_dir = dataset / "labels" / "train"
        train_dir.mkdir(parents=True)
        label_dir.mkdir(parents=True)
        target = root / "original.jpg"
        target.write_bytes(b"test")
        image = train_dir / "alias.jpg"
        image.symlink_to(target)
        (label_dir / "alias.txt").write_text("0 0.5 0.5 0.2 0.2\n")
        weights = root / "weights.pt"
        weights.write_bytes(b"test")
        argv = ["miner", "--weights", str(weights), "--dataset-dir", str(dataset),
                "--output-dir", str(root / "result")]
        result = SimpleNamespace(path=str(image), orig_shape=(384, 640), boxes=None)
        with mock.patch("sys.argv", argv), mock.patch("ultralytics.YOLO") as model:
            model.return_value.predict.return_value = [result]
            miner.main()
        paths = (root / "result" / "hard_samples.txt").read_text().splitlines()
        assert paths == [str(image)]
        cfg = training.TrainConfig(
            dataset_dir=str(dataset),
            hard_sample_list=str(root / "result" / "hard_samples.txt"),
        )
        assert training.validate_hard_sample_list(cfg) == [image]

        # An unrelated result with the same basename must not borrow a train label.
        result.path = str(root / "alias.jpg")
        with mock.patch("sys.argv", argv), mock.patch("ultralytics.YOLO") as model:
            model.return_value.predict.return_value = [result]
            assert_raises(RuntimeError, miner.main, "train")


def test_hard_stage_sampling_respects_growth_cap() -> None:
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir)
        images = create_dataset(root / "dataset", [0] * 10)
        balanced_list = root / "balanced.txt"
        hard_list = root / "hard.txt"
        write_path_list(balanced_list, images)
        write_path_list(hard_list, images[:4])
        cfg = training.TrainConfig(
            dataset_dir=str(root / "dataset"),
            hard_sample_list=str(hard_list),
            hard_sample_repeat=2,
            hard_max_growth=1.3,
            seed=7,
        )

        train_list, summary = training.build_hard_stage_train_list(
            cfg,
            balanced_list,
            root / "run",
        )
        weighted = training.read_image_path_list(train_list)

        assert summary["requested_unique_hard_samples"] == 4
        assert summary["used_unique_hard_samples"] == 1
        assert summary["dropped_hard_samples"] == 3
        assert summary["total_entries"] == 12
        assert summary["growth_cap_applied"] is True
        assert all(path.resolve() in set(images) for path in weighted)
        assert (root / "run" / "hard_stage_sampling_summary.yaml").is_file()


def test_small_tail_miss_increases_normalized_score() -> None:
    ground_truth = [
        (5, (0.0, 0.0, 10.0, 10.0)),
        (2, (0.0, 0.0, 100.0, 100.0)),
    ]
    predictions = [(2, (0.0, 0.0, 100.0, 100.0))]
    common = {
        "image_path": Path("frame.jpg"),
        "ground_truth": ground_truth,
        "predictions": predictions,
        "tail_weights": {5: 2.0, 2: 1.0},
        "frequencies": {5: 0.001, 2: 0.9},
        "frequency_threshold": 0.1,
        "iou_threshold": 0.5,
        "input_size": (384, 640),
        "image_size": (384, 640),
    }
    baseline = miner.score_result(**common, small_object_weight=1.0)
    weighted = miner.score_result(**common, small_object_weight=1.5)

    assert weighted["score"] > baseline["score"]
    assert weighted["false_negatives"] == 1
    assert weighted["_missed_classes"] == (5,)
    assert weighted["_small_object_count"] == 1
    assert weighted["_small_object_misses"] == 1


def test_class_balanced_selection_keeps_rare_missed_class() -> None:
    records = [
        {"path": f"common_{index}.jpg", "score": float(10 - index), "_missed_classes": (0,)}
        for index in range(5)
    ]
    records.append({"path": "rare.jpg", "score": 1.0, "_missed_classes": (5,)})

    selected, summary = miner.select_hard_records(
        records,
        top_ratio=2.0 / 3.0,
        max_samples=4,
        class_balance_ratio=0.5,
    )

    assert len(selected) == 4
    assert "rare.jpg" in {record["path"] for record in selected}
    assert summary["selected_by_missed_class"] == {0: 1, 5: 1}


def test_class_balanced_selection_preserves_global_score_budget() -> None:
    records = [
        {"path": f"class_{index}.jpg", "score": 1.0, "_missed_classes": (index,)}
        for index in range(5)
    ] + [
        {"path": f"global_{index}.jpg", "score": 10.0, "_missed_classes": ()}
        for index in range(5)
    ]
    for ratio in (0.0, 0.5, 1.0):
        selected, summary = miner.select_hard_records(
            records, top_ratio=1.0, max_samples=4, class_balance_ratio=ratio,
        )
        balanced_count = sum(summary["selected_by_missed_class"].values())
        assert len(selected) == 4
        assert balanced_count <= summary["class_balanced_budget"]
        assert sum(record["score"] == 10.0 for record in selected) == 4 - balanced_count


def test_config_defaults_are_portable_and_rect_guard_is_active() -> None:
    cfg = training.TrainConfig()
    assert not Path(cfg.dataset_dir).is_absolute()
    assert not Path(cfg.data_yaml).is_absolute()

    with tempfile.TemporaryDirectory() as temporary_dir:
        normalized = training.normalize_cfg_paths(
            training.TrainConfig(project_root=temporary_dir),
        )
        assert Path(normalized.dataset_dir) == Path(temporary_dir) / "data" / "bdd100k_yolo_det"
        assert Path(normalized.data_yaml) == Path(temporary_dir) / "data" / "bdd100k_yolo_det" / "data.yaml"

    assert_raises(
        ValueError,
        lambda: training.validate_training_config(training.TrainConfig(rect=True)),
        "rect=True",
    )


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
