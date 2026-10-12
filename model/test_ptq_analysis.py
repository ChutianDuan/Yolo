"""Small CPU regressions for PTQ exclusion and streamed diagnostics."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
sys.path = [str(ROOT)] + [p for p in sys.path if not p or Path(p).resolve() != ROOT / "model"]

import cv2
import numpy as np
import onnx
from onnx import helper, numpy_helper
from onnxruntime.quantization import CalibrationMethod, QuantFormat, QuantType, quantize_static

from model.ptq_analysis import (ErrorStats, Reader, cached_quantize, calibrate, diagnose,
                                main, matched_boxes, quality, session, speed_retention,
                                evaluation, ObjectRecord)
from model.ptq_int8 import PTQ_OPTIONS, quantize_ptq
from model.quantization import inspect_int8


def shared_graph(path: Path) -> None:
    rng = np.random.default_rng(42)
    nodes = [helper.make_node("Conv", ["images", "wa"], ["features"], name="a"),
             helper.make_node("Conv", ["features", "wb"], ["left"], name="b"),
             helper.make_node("Conv", ["features", "wc"], ["right"], name="c"),
             helper.make_node("Add", ["left", "right"], ["output"], name="add")]
    weights = [numpy_helper.from_array(rng.normal(size=(3,3,1,1)).astype(np.float32), name)
               for name in ("wa", "wb", "wc")]
    graph = helper.make_graph(nodes, "shared", [helper.make_tensor_value_info("images", 1, [1,3,32,32])],
                              [helper.make_tensor_value_info("output", 1, [1,3,32,32])], weights)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)], ir_version=10)
    onnx.save(model, str(path))


class PTQAnalysisTests(unittest.TestCase):
    def test_cached_quantizer_and_shared_branch_exclusion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target, public = root/"float.onnx", root/"cached.onnx", root/"public.onnx"
            shared_graph(source)
            image = root/"image.png"
            self.assertTrue(cv2.imwrite(str(image), np.random.default_rng(42).integers(0,256,(32,32,3),dtype=np.uint8)))
            ranges = calibrate(source, [image], (32,32), root)
            cached_quantize(source, target, ranges, [])
            quantize_static(source, public, Reader("images", [image], (32,32)),
                            quant_format=QuantFormat.QDQ, activation_type=QuantType.QUInt8,
                            weight_type=QuantType.QInt8, per_channel=True,
                            calibrate_method=CalibrationMethod.MinMax, op_types_to_quantize=["Conv"],
                            extra_options=PTQ_OPTIONS)
            inputs = np.random.default_rng(8).random((1,3,32,32), dtype=np.float32)
            np.testing.assert_array_equal(session(target).run(None, {"images":inputs})[0],
                                          session(public).run(None, {"images":inputs})[0])
            excluded = root/"excluded.onnx"
            cached_quantize(source, excluded, ranges, ["b"])
            detail = inspect_int8(excluded, (32,32), ["b"])
            self.assertEqual(detail["int8_conv_count"], 2)
            self.assertEqual(detail["fp32_conv_count"], 1)
            self.assertEqual(detail["excluded_with_shared_input_qdq"], ["b"])
            with self.assertRaisesRegex(ValueError, "packed INT8"):
                inspect_int8(excluded, (32,32))
            with self.assertRaisesRegex(ValueError, "Unknown Conv"):
                cached_quantize(source, root/"unknown.onnx", ranges, ["missing"])
            with self.assertRaises(FileExistsError):
                cached_quantize(source, target, ranges, [])
            rows = diagnose(source, target, ranges, [image], (32,32), root)
            self.assertEqual(len(rows), 3)
            self.assertTrue(all(r["output"]["nmse"] >= 0 for r in rows))
            input_info = session(target).get_inputs()
            with patch("model.quantization.ort.InferenceSession") as mocked:
                mocked.return_value.get_inputs.return_value = input_info
                mocked.return_value.run.return_value = [np.array([np.inf], np.float32)]
                with self.assertRaisesRegex(ValueError, "non-finite"):
                    inspect_int8(target, (32,32))

    def test_streamed_stats_and_detection_matching(self):
        stats = ErrorStats()
        stats.update(np.array([1., 2.]), np.array([1., 3.]), (0., 1.5))
        self.assertAlmostEqual(stats.result()["nmse"], 0.2)
        self.assertEqual(stats.result()["outside_fraction"], 0.5)
        with self.assertRaisesRegex(ValueError, "Non-finite"):
            stats.update(np.array([np.nan]), np.array([0.]))
        self.assertAlmostEqual(speed_retention(100, 50, 60), 0.8)
        self.assertIsNone(speed_retention(50, 60, 55))
        boxes = [[0,0.8,[0,0,10,10]], [1,0.9,[20,20,30,30]]]
        match = matched_boxes(boxes, list(reversed(boxes)))
        self.assertEqual(match["matched"], 2)
        self.assertEqual(match["mean_iou"], 1.)

    def test_public_ptq_exclusion_survives_preprocessing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target = root/"float.onnx", root/"excluded.onnx"
            shared_graph(source)
            image = root/"calibration.png"
            self.assertTrue(cv2.imwrite(str(image), np.full((32,32,3), 128, np.uint8)))
            quantize_ptq(source, target, [image], (32,32), ["b"])
            self.assertEqual(inspect_int8(target, (32,32), ["b"])["int8_conv_count"], 2)

    def test_pooled_quality_matches_existing_evaluator_with_crowds(self):
        person = ObjectRecord(0, "p", "person", (0,0,10,10))
        car = ObjectRecord(0, "c", "car", (0,0,10,10))
        crowd = ObjectRecord(0, "crowd", "person", (20,20,30,30))
        labels = {"a": {0: [person]}, "b": {0: [car]}}
        crowds = {"a": {0: [crowd]}, "b": {0: []}}
        predictions = {"a": {0: [ObjectRecord(0,"1","person",person.box,0.9),
                                 ObjectRecord(0,"2","person",crowd.box,0.5)]},
                       "b": {0: [ObjectRecord(0,"3","car",car.box,0.2),
                                 ObjectRecord(0,"4","person",crowd.box,0.4)]}}
        actual = quality(predictions, labels, crowds)
        for iou in np.arange(0.5,0.951,0.05):
            expected = evaluation.evaluate_predictions(predictions, labels, crowds,
                        [0.25] if iou == 0.5 else [], 0.001, float(iou), include_per_source=False)
            self.assertEqual(actual["iou_results"][f"{iou:.2f}"], expected)

    def test_output_directory_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(sys, "argv", ["ptq_analysis.py", "--output-dir", directory]):
                with self.assertRaises(FileExistsError):
                    main()
            (Path(directory)/"report.md").write_text("completed")
            with patch.object(sys, "argv", ["ptq_analysis.py", "--output-dir", directory, "--resume"]):
                with self.assertRaisesRegex(ValueError, "incomplete"):
                    main()


if __name__ == "__main__":
    unittest.main()
