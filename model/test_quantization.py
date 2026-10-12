"""Small CPU regressions; no model downloads or real training dataset required."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path = [str(SCRIPT_DIR.parent)] + [p for p in sys.path if not p or Path(p).resolve() != SCRIPT_DIR]

import cv2
import numpy as np
import onnx
import onnxruntime as ort
import torch
import yaml
from torch import nn

from model.ptq_int8 import quantize_ptq
from model.quantization import (
    QATConv2d, inspect_int8, load_training_data, pack_qdq_weights,
    prepare_qat_model, set_observers,
)


class QuantizationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        torch.set_num_threads(2)

    def test_fake_quant_backpropagates_and_optimizer_updates_weights(self) -> None:
        layer = QATConv2d.from_float(nn.Conv2d(3, 4, 3, padding=1))
        before = layer.weight.detach().clone()
        optimizer = torch.optim.SGD(layer.parameters(), lr=0.01)
        layer(torch.randn(2, 3, 32, 32)).square().mean().backward()
        self.assertGreater(layer.weight.grad.abs().sum().item(), 0)
        optimizer.step()
        self.assertFalse(torch.equal(before, layer.weight))

    def test_eval_and_frozen_observers_keep_qparams(self) -> None:
        layer = QATConv2d.from_float(nn.Conv2d(3, 4, 1))
        layer.train()
        layer(torch.rand(1, 3, 32, 32))
        scale = layer.input_fake_quant.scale.clone()
        layer.eval()
        layer(torch.full((1, 3, 32, 32), 100.0))
        self.assertTrue(torch.equal(scale, layer.input_fake_quant.scale))
        layer.train()
        set_observers(layer, False)
        layer(torch.full((1, 3, 32, 32), 100.0))
        self.assertTrue(torch.equal(scale, layer.input_fake_quant.scale))
        self.assertEqual(layer.input_fake_quant.fake_quant_enabled.item(), 1)

    def test_qat_export_packs_int8_weights_and_matches_pytorch(self) -> None:
        layer = QATConv2d.from_float(nn.Conv2d(3, 8, 3, padding=1))
        inputs = torch.rand(1, 3, 32, 32)
        layer.train()
        layer(inputs)
        layer.eval()
        expected = layer(inputs).detach().numpy()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "qat.onnx"
            torch.onnx.export(layer, inputs, str(path), opset_version=13,
                              input_names=["images"], output_names=["output0"])
            pack_qdq_weights(path)
            details = inspect_int8(path, (32, 32))
            self.assertEqual(details["int8_conv_count"], 1)
            options = ort.SessionOptions()
            options.intra_op_num_threads = 2
            session = ort.InferenceSession(str(path), options, providers=["CPUExecutionProvider"])
            actual = session.run(None, {"images": inputs.numpy()})[0]
            np.testing.assert_allclose(actual, expected, atol=1e-5, rtol=1e-5)
            self.assertTrue(any(item.data_type == onnx.TensorProto.INT8
                                for item in onnx.load(str(path)).graph.initializer))

    def test_ptq_uses_matching_input_weight_qdq_profile(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target = root / "fp32.onnx", root / "ptq.onnx"
            conv = nn.Conv2d(3, 8, 3, padding=1).eval()
            torch.onnx.export(conv, torch.zeros(1, 3, 32, 32), str(source),
                              opset_version=13, input_names=["images"])
            image = root / "calibration.png"
            self.assertTrue(cv2.imwrite(str(image), np.full((32, 32, 3), 128, np.uint8)))
            quantize_ptq(source, target, [image], (32, 32))
            self.assertEqual(inspect_int8(target, (32, 32))["int8_conv_count"], 1)
            graph = onnx.load(str(target))
            conv = next(node for node in graph.graph.node if node.op_type == "Conv")
            self.assertFalse(any(node.op_type == "QuantizeLinear" and node.input[0] == conv.output[0]
                                 for node in graph.graph.node))

    def test_qat_packing_resolves_shared_zero_point_aliases(self) -> None:
        model = nn.Sequential(QATConv2d.from_float(nn.Conv2d(3, 8, 1)),
                              QATConv2d.from_float(nn.Conv2d(8, 8, 1)))
        inputs = torch.rand(1, 3, 32, 32)
        model(inputs)
        model.eval()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "aliases.onnx"
            torch.onnx.export(model, inputs, str(path), opset_version=13,
                              input_names=["images"])
            pack_qdq_weights(path)
            self.assertEqual(inspect_int8(path, (32, 32))["int8_conv_count"], 2)

    def test_fp32_graph_is_rejected_as_int8(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "float.onnx"
            torch.onnx.export(nn.Conv2d(3, 4, 1), torch.zeros(1, 3, 32, 32),
                              str(path), opset_version=13)
            with self.assertRaisesRegex(ValueError, "packed INT8"):
                inspect_int8(path, (32, 32))

    def test_training_paths_are_yaml_relative_and_missing_splits_fail_locally(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "dataset/train").mkdir(parents=True)
            (root / "dataset/val").mkdir()
            path = root / "data.yaml"
            path.write_text(yaml.safe_dump({"path": "dataset", "train": "train",
                                            "val": "val", "names": ["car"]}))
            data = load_training_data(path)
            self.assertEqual(data["train"], [str(root / "dataset/train")])
            self.assertEqual(data["val"], [str(root / "dataset/val")])
            path.write_text(yaml.safe_dump({"path": "dataset", "train": "train",
                                            "val": "missing", "names": ["car"],
                                            "download": "https://example.invalid/dataset.zip"}))
            with self.assertRaises(FileNotFoundError):
                load_training_data(path)

    def test_yolo26_training_heads_and_detection_loss_survive_qat(self) -> None:
        from ultralytics import YOLO
        from ultralytics.cfg import get_cfg

        model = YOLO("yolo26n.yaml").model
        model.args = get_cfg()
        count = prepare_qat_model(model)
        self.assertGreater(count, 0)
        self.assertTrue(model.model[-1].one2many)
        model.train()
        batch = {"img": torch.rand(2, 3, 64, 64), "batch_idx": torch.tensor([0, 1]),
                 "cls": torch.tensor([[0.0], [1.0]]),
                 "bboxes": torch.tensor([[0.5, 0.5, 0.3, 0.3], [0.4, 0.4, 0.2, 0.2]])}
        for parameter in model.parameters():
            parameter.requires_grad_(True)
        loss, _ = model(batch)
        self.assertTrue(torch.isfinite(loss).all())
        loss.sum().backward()
        self.assertTrue(any(parameter.grad is not None and parameter.grad.abs().sum() > 0
                            for parameter in model.parameters()))


if __name__ == "__main__":
    unittest.main()
