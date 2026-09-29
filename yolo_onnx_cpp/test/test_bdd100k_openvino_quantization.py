import importlib.util
import sys
import tempfile
from pathlib import Path

import numpy as np


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
SPEC = importlib.util.spec_from_file_location(
    "evaluate_bdd100k_openvino_quantization",
    TOOLS_DIR / "evaluate_bdd100k_openvino_quantization.py",
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE

GT_SPEC = importlib.util.spec_from_file_location(
    "prepare_bdd100k_ground_truth",
    TOOLS_DIR / "prepare_bdd100k_ground_truth.py",
)
assert GT_SPEC and GT_SPEC.loader
GT_MODULE = importlib.util.module_from_spec(GT_SPEC)
sys.modules[GT_SPEC.name] = GT_MODULE
GT_SPEC.loader.exec_module(GT_MODULE)
SPEC.loader.exec_module(MODULE)


def test_letterbox_preprocess_matches_cpp_geometry():
    image = np.empty((720, 1280, 3), dtype=np.uint8)
    image[:] = (10, 20, 30)

    tensor, transform = MODULE.letterbox_preprocess(image, 640, 384)

    assert tensor.shape == (1, 3, 384, 640)
    assert tensor.dtype == np.float32
    assert transform.scale_x == 0.5
    assert transform.scale_y == 0.5
    assert transform.pad_w == 0.0
    assert transform.pad_h == 12.0
    assert np.allclose(tensor[0, :, 12, 0], np.array([30, 20, 10]) / 255.0)


def test_end_to_end_decode_maps_boxes_and_runs_class_aware_nms():
    transform = MODULE.LetterboxTransform(1280, 720, 0.5, 0.5, 0.0, 12.0)
    output = np.zeros((1, 300, 6), dtype=np.float32)
    output[0, 0] = [50, 22, 150, 122, 0.9, 2]
    output[0, 1] = [52, 24, 148, 120, 0.8, 2]
    output[0, 2] = [50, 22, 150, 122, 0.7, 3]
    output[0, 3] = [50, 22, 150, 122, 0.0005, 2]

    detections = MODULE.decode_output(
        output,
        transform,
        ["person", "rider", "car", "truck"],
        score_threshold=0.001,
        nms_iou_threshold=0.45,
    )

    assert len(detections) == 2
    assert [item[0] for item in detections] == [2, 3]
    assert detections[0][1] == np.float32(0.9)
    assert detections[0][2] == (100.0, 20.0, 300.0, 220.0)


def test_raw_decode_supports_channel_first_output():
    transform = MODULE.LetterboxTransform(640, 384, 1.0, 1.0, 0.0, 0.0)
    # Four box channels plus two class-score channels, eight candidates.
    output = np.zeros((1, 6, 8), dtype=np.float32)
    output[0, :, 0] = [100, 100, 40, 20, 0.2, 0.9]
    output[0, :, 1] = [300, 200, 20, 40, 0.8, 0.1]

    detections = MODULE.decode_output(
        output,
        transform,
        ["person", "car"],
        score_threshold=0.25,
        nms_iou_threshold=0.45,
    )

    assert [(item[0], round(item[1], 1)) for item in detections] == [(1, 0.9), (0, 0.8)]
    assert detections[0][2] == (80.0, 90.0, 120.0, 110.0)
    assert detections[1][2] == (290.0, 180.0, 310.0, 220.0)


def test_crowd_suppression_is_class_specific():
    record = MODULE.ObjectRecord
    predictions = {
        0: [
            record(0, "car-overlap", "car", (0, 0, 10, 10), 0.9),
            record(0, "truck-overlap", "truck", (0, 0, 10, 10), 0.8),
        ]
    }
    crowds = {0: [record(0, "crowd", "car", (0, 0, 10, 10))]}

    filtered, ignored = MODULE.suppress_crowd_predictions(predictions, {0: []}, crowds, 0.5)

    assert ignored == 1
    assert [item.object_id for item in filtered[0]] == ["truck-overlap"]

    normal = {0: [record(0, "normal", "car", (0, 0, 10, 10))]}
    filtered, ignored = MODULE.suppress_crowd_predictions(
        predictions, normal, crowds, 0.5
    )
    assert ignored == 0
    assert [item.object_id for item in filtered[0]] == ["car-overlap", "truck-overlap"]
    distractors = {
        0: [record(0, "distractor", MODULE.IGNORE_REGION_CLASS, (0, 0, 10, 10))]
    }
    filtered, ignored = MODULE.suppress_crowd_predictions(
        predictions, {0: []}, distractors, 0.5
    )
    assert ignored == 2
    assert filtered[0] == []

    filtered, ignored = MODULE.suppress_crowd_predictions(
        predictions, normal, distractors, 0.5
    )
    assert ignored == 1
    assert [item.object_id for item in filtered[0]] == ["car-overlap"]


def test_threshold_validation_rejects_values_below_candidate_floor():
    try:
        MODULE.parse_thresholds("0.01,0.25", 0.05)
    except MODULE.QuantizationEvaluationError as exc:
        assert "minimum-score" in str(exc)
    else:
        raise AssertionError("expected threshold validation failure")


def test_ground_truth_tail_selection_is_bounded():
    selected, skipped = GT_MODULE.select_evaluated_indices(range(4), 13, 6, 1)
    assert selected == [0, 1, 2]
    assert skipped == [3]

    try:
        GT_MODULE.select_evaluated_indices(range(5), 13, 6, 1)
    except GT_MODULE.evaluation.EvaluationError as exc:
        assert "misses 2 trailing" in str(exc)
    else:
        raise AssertionError("expected excessive tail rejection")


def test_runtime_mapping_defaults_and_validates_overrides():
    runtimes = MODULE.parse_named_runtimes(["int8=onnxruntime"], {"fp32", "int8"})
    assert runtimes == {"fp32": "openvino", "int8": "onnxruntime"}

    try:
        MODULE.parse_named_runtimes(["unknown=onnxruntime"], {"fp32"})
    except MODULE.QuantizationEvaluationError as exc:
        assert "unknown model" in str(exc)
    else:
        raise AssertionError("expected runtime/model validation failure")


def test_best_class_operating_point_uses_grid_and_tie_breaks():
    def metrics(ground_truth, ap50, precision, recall, f1):
        return {
            "ground_truth": ground_truth,
            "ap50": ap50,
            "precision": precision,
            "recall": recall,
            "f1": f1,
        }

    evaluation = {
        "ap_floor": 0.001,
        "operating_points": {
            "0.001": {
                "threshold": 0.001,
                "per_class": {
                    "bus": metrics(8, 0.6, 0.2, 0.9, 0.33),
                    "train": metrics(0, 0.0, 0.0, 0.0, 0.0),
                },
            },
            "0.050": {
                "threshold": 0.05,
                "per_class": {
                    "bus": metrics(8, 0.0, 0.6, 0.5, 0.5),
                    "train": metrics(0, 0.0, 0.0, 0.0, 0.0),
                },
            },
            "0.100": {
                "threshold": 0.1,
                "per_class": {
                    "bus": metrics(8, 0.0, 0.7, 0.4, 0.5),
                    "train": metrics(0, 0.0, 0.0, 0.0, 0.0),
                },
            },
        },
    }

    assert MODULE.best_class_operating_points(evaluation) == [
        {
            "class_name": "bus",
            "ground_truth": 8,
            "ap50": 0.6,
            "threshold": 0.1,
            "precision": 0.7,
            "recall": 0.4,
            "f1": 0.5,
        }
    ]


def test_export_profile_and_overwrite_guard():
    export_path = Path(__file__).resolve().parents[2] / "model" / "onnx.py"
    spec = importlib.util.spec_from_file_location("yolo_onnx_export_profile", export_path)
    assert spec and spec.loader
    export_module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = export_module
    spec.loader.exec_module(export_module)

    assert export_module.DEFAULT_ONNX_OPSET == 13
    assert export_module.ACTIVATION_QUANT_TYPE == export_module.QuantType.QUInt8
    assert export_module.WEIGHT_QUANT_TYPE == export_module.QuantType.QInt8
    assert export_module.PER_CHANNEL_QUANTIZATION is True

    with tempfile.TemporaryDirectory() as directory:
        missing = Path(directory) / "missing.onnx"
        export_module.require_outputs_absent([missing])
        existing = Path(directory) / "existing.onnx"
        existing.write_bytes(b"test")
        try:
            export_module.require_outputs_absent([missing, existing])
        except FileExistsError as exc:
            assert str(existing) in str(exc)
        else:
            raise AssertionError("expected overwrite guard failure")


if __name__ == "__main__":
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    for test in tests:
        test()
    print(f"{len(tests)} tests passed")
