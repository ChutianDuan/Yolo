"""Shared Conv-only U8/S8 QDQ helpers for the PTQ and QAT experiments."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import torch
from onnx import numpy_helper
from torch import nn
from torch.ao.quantization import FakeQuantize, MinMaxObserver, PerChannelMinMaxObserver

from model.onnx import collect_calibration_images, iter_split_entries, load_data_yaml, preprocess_image


PROFILE = {
    "format": "QDQ", "op_types": ["Conv"],
    "activations": "uint8_per_tensor_affine",
    "weights": "int8_per_channel_symmetric",
    "bias": "float32", "conv_output": "float32",
    "input_output": "float32", "opset": 13,
}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ordered_names(names: dict | list | tuple) -> list[str]:
    if isinstance(names, dict):
        return [str(names[key]) for key in sorted(names, key=int)]
    return [str(name) for name in names]


def prepare_inputs(args: argparse.Namespace) -> tuple[Path, Path, list[Path]]:
    pt = Path(args.pt).expanduser().resolve()
    data = Path(args.data).expanduser().resolve()
    if not pt.is_file() or pt.suffix != ".pt":
        raise FileNotFoundError(f"A local .pt detection checkpoint is required: {pt}")
    if any(size <= 0 or size % 32 for size in args.imgsz):
        raise ValueError("--imgsz HEIGHT WIDTH must be positive multiples of 32")
    if args.calib_limit <= 0:
        raise ValueError("--calib-limit must be positive")
    images = collect_calibration_images(data, args.calib_split, args.calib_limit)
    return pt, data, images


def validate_classes(model, data: Path) -> None:
    expected = ordered_names(load_data_yaml(data)["names"])
    if model.task != "detect" or ordered_names(model.names) != expected:
        raise ValueError("Use a detection checkpoint with the same class order as data.yaml")


def load_training_data(path: Path) -> dict:
    """Resolve local splits exactly as calibration does, without auto-downloads."""
    data = load_data_yaml(path)
    root = Path(data.get("path", path.parent)).expanduser()
    if not root.is_absolute():
        root = path.parent / root
    names = ordered_names(data["names"])
    if data.get("nc", len(names)) != len(names) or data.get("channels", 3) != 3:
        raise ValueError("Expected matching nc/names and three-channel images")
    for split in ("train", "val"):
        if split not in data:
            raise ValueError(f"QAT requires a local {split} split")
        entries = list(iter_split_entries(data[split], root, path.parent))
        if not entries or not all(entry.exists() for entry in entries):
            raise FileNotFoundError(f"QAT {split} paths must exist locally: {entries}")
        data[split] = [str(entry.resolve()) for entry in entries]
    data.update(path=str(root.resolve()), yaml_file=str(path),
                names=dict(enumerate(names)), nc=len(names), channels=3)
    return data


class TrainingFakeQuantize(FakeQuantize):
    """Validation and export use fixed qparams even before observers are frozen."""

    def forward(self, inputs: torch.Tensor) -> torch.Tensor:
        if self.training:
            return super().forward(inputs)
        if not torch.onnx.is_in_onnx_export() and self.fake_quant_enabled[0] == 0:
            return inputs
        if self.is_per_channel:
            return torch.fake_quantize_per_channel_affine(
                inputs, self.scale, self.zero_point, self.ch_axis,
                self.quant_min, self.quant_max,
            )
        return torch.fake_quantize_per_tensor_affine(
            inputs, self.scale, self.zero_point, self.quant_min, self.quant_max,
        )


class QATConv2d(nn.Conv2d):
    """Fake-quantize Conv inputs and weights; keep biases and outputs floating."""

    @classmethod
    def from_float(cls, conv: nn.Conv2d) -> "QATConv2d":
        layer = cls(
            conv.in_channels, conv.out_channels, conv.kernel_size,
            conv.stride, conv.padding, conv.dilation, conv.groups,
            conv.bias is not None, conv.padding_mode,
        ).to(device=conv.weight.device, dtype=conv.weight.dtype)
        layer.weight = conv.weight
        layer.bias = conv.bias
        layer.input_fake_quant = TrainingFakeQuantize(
            observer=MinMaxObserver, quant_min=0, quant_max=255,
            dtype=torch.quint8, qscheme=torch.per_tensor_affine,
        ).to(conv.weight.device)
        layer.weight_fake_quant = TrainingFakeQuantize(
            observer=PerChannelMinMaxObserver, quant_min=-128, quant_max=127,
            dtype=torch.qint8, qscheme=torch.per_channel_symmetric, ch_axis=0,
        ).to(conv.weight.device)
        return layer

    def forward(self, inputs: torch.Tensor) -> torch.Tensor:
        return self._conv_forward(
            self.input_fake_quant(inputs), self.weight_fake_quant(self.weight), self.bias,
        )


def prepare_qat_model(model: nn.Module) -> int:
    """Fold Conv/BN without calling YOLO.fuse(), which removes a training head."""
    from ultralytics.utils.torch_utils import fuse_conv_and_bn

    model.eval()
    for module in list(model.modules()):
        if (isinstance(getattr(module, "conv", None), nn.Conv2d)
                and isinstance(getattr(module, "bn", None), nn.BatchNorm2d)
                and hasattr(module, "forward_fuse")):
            trainable = module.conv.weight.requires_grad
            module.conv = fuse_conv_and_bn(module.conv, module.bn)
            module.conv.weight.requires_grad_(trainable)
            if module.conv.bias is not None:
                module.conv.bias.requires_grad_(trainable)
            delattr(module, "bn")
            module.forward = module.forward_fuse
    if any(isinstance(m, nn.modules.batchnorm._BatchNorm) for m in model.modules()):
        raise ValueError("Unsupported BatchNorm layout; expected standard YOLO Conv/BN blocks")
    count = 0
    for parent in list(model.modules()):
        for name, child in list(parent.named_children()):
            if type(child) is nn.Conv2d:
                parent.add_module(name, QATConv2d.from_float(child))
                count += 1
    if not count:
        raise ValueError("No Conv2d layers found")
    # Ultralytics re-enables trainable parameters and freezes DFL in setup_train.
    return count


def set_observers(model: nn.Module, enabled: bool) -> None:
    for module in model.modules():
        if isinstance(module, FakeQuantize):
            module.enable_observer(enabled)


def calibrate_qat(model: nn.Module, images: list[Path], imgsz: tuple[int, int]) -> None:
    device = next(model.parameters()).device
    model.train()  # Observe both one2many and one2one heads; BN is already folded.
    with torch.no_grad():
        for path in images:
            model(torch.from_numpy(preprocess_image(path, imgsz)).to(device))
    model.eval()


def pack_qdq_weights(path: Path) -> None:
    """Fold constant weight QuantizeLinear nodes into actual INT8 initializers."""
    graph = onnx.load(str(path))
    constants = {item.name: numpy_helper.to_array(item) for item in graph.graph.initializer}
    for node in graph.graph.node:
        if node.op_type == "Constant":
            for attr in node.attribute:
                if attr.name == "value":
                    constants[node.output[0]] = numpy_helper.to_array(attr.t)
        elif node.op_type == "Identity" and node.input[0] in constants:
            constants[node.output[0]] = constants[node.input[0]]
        elif node.op_type == "Cast" and node.input[0] in constants:
            dtype = next(attr.i for attr in node.attribute if attr.name == "to")
            constants[node.output[0]] = constants[node.input[0]].astype(
                onnx.helper.tensor_dtype_to_np_dtype(dtype),
            )
    remove = []
    for node in graph.graph.node:
        if node.op_type != "QuantizeLinear" or not all(name in constants for name in node.input):
            continue
        weight, scale, zero = (constants[name] for name in node.input)
        if weight.ndim != 4 or zero.dtype != np.int8:
            continue
        axis = next((attr.i for attr in node.attribute if attr.name == "axis"), 1)
        # Preserve the fake-quant kernel's FP32 reciprocal/rounding at half bins.
        # NumPy weight / scale can round a borderline weight one level differently.
        tensor = torch.from_numpy(weight.copy())
        if scale.ndim:
            rounded = torch.fake_quantize_per_channel_affine(
                tensor, torch.from_numpy(scale.copy()),
                torch.from_numpy(zero.astype(np.int32)), axis, -128, 127,
            ).numpy()
        else:
            rounded = torch.fake_quantize_per_tensor_affine(
                tensor, float(scale), int(zero), -128, 127,
            ).numpy()
        shape = [1] * weight.ndim
        shape[axis] = -1
        scale = scale.reshape(shape) if scale.ndim else scale
        zero = zero.reshape(shape) if zero.ndim else zero
        quantized = np.clip(np.rint(rounded / scale) + zero, -128, 127).astype(np.int8)
        graph.graph.initializer.append(numpy_helper.from_array(quantized, node.output[0]))
        remove.append(node)
    for node in remove:
        graph.graph.node.remove(node)
    used = {name for node in graph.graph.node for name in node.input}
    used.update(item.name for item in graph.graph.output)
    keep = [item for item in graph.graph.initializer if item.name in used]
    del graph.graph.initializer[:]
    graph.graph.initializer.extend(keep)
    onnx.checker.check_model(graph)
    onnx.save(graph, str(path))


def inspect_int8(path: Path, imgsz: tuple[int, int],
                 excluded: list[str] | None = None) -> dict:
    graph = onnx.load(str(path))
    onnx.checker.check_model(graph)
    producers = {name: node for node in graph.graph.node for name in node.output}
    initializers = {item.name: item for item in graph.graph.initializer}
    convs = [node for node in graph.graph.node if node.op_type == "Conv"]
    excluded_set = set(excluded or [])
    unknown = excluded_set - {node.name for node in convs}
    if unknown:
        raise ValueError(f"Unknown Conv nodes: {sorted(unknown)}")
    quantized = 0
    quantized_names = set()
    shared_inputs = []
    for conv in convs:
        input_dq = producers.get(conv.input[0])
        weight_dq = producers.get(conv.input[1])
        if conv.name in excluded_set:
            weight = initializers.get(conv.input[1])
            if weight is None or weight.data_type != onnx.TensorProto.FLOAT:
                raise ValueError(f"Excluded Conv must retain FP32 weights: {conv.name}")
            if input_dq is not None and input_dq.op_type == "DequantizeLinear":
                shared_inputs.append(conv.name)
            continue
        if input_dq is None or weight_dq is None:
            continue
        if input_dq.op_type != "DequantizeLinear" or weight_dq.op_type != "DequantizeLinear":
            continue
        weight = initializers.get(weight_dq.input[0])
        if weight is not None and weight.data_type == onnx.TensorProto.INT8:
            quantized += 1
            quantized_names.add(conv.name)
    expected_names = {conv.name for conv in convs} - excluded_set
    if (not convs or quantized_names != expected_names
            or quantized != len(convs) - len(excluded_set)):
        raise ValueError(f"Expected packed INT8 QDQ for non-excluded Conv: {quantized}/{len(expected_names)}")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(str(path), options, providers=["CPUExecutionProvider"])
    input_info = session.get_inputs()[0]
    expected = [1, 3, *imgsz]
    if input_info.type != "tensor(float)" or input_info.shape != expected:
        raise ValueError(f"Expected float32 input {expected}, got {input_info.type} {input_info.shape}")
    outputs = session.run(None, {input_info.name: np.zeros(expected, dtype=np.float32)})
    if not all(np.isfinite(item).all() for item in outputs):
        raise ValueError("INT8 smoke inference produced non-finite output")
    return {"conv_count": len(convs), "int8_conv_count": quantized,
            "fp32_conv_count": len(excluded_set), "excluded_nodes": sorted(excluded_set),
            "excluded_with_shared_input_qdq": shared_inputs,
            "input_shape": expected, "output_shapes": [list(item.shape) for item in outputs]}


def write_manifest(out: Path, method: str, pt: Path, data: Path,
                   images: list[Path], args: argparse.Namespace, details: dict) -> None:
    (out / "calibration_images.txt").write_text(
        "".join(f"{path}\n" for path in images), encoding="utf-8",
    )
    manifest = {
        "method": method, "profile": PROFILE, "source_pt": str(pt),
        "source_sha256": file_sha256(pt), "data_yaml": str(data),
        "data_sha256": file_sha256(data), "calibration_split": args.calib_split,
        "calibration_count": len(images), "arguments": vars(args),
        "calibration_content_sha256": hashlib.sha256(
            "".join(file_sha256(image) for image in images).encode("ascii"),
        ).hexdigest(),
        "versions": {"torch": torch.__version__, "onnx": onnx.__version__, "onnxruntime": ort.__version__},
        "artifacts": {p.name: {"bytes": p.stat().st_size, "sha256": file_sha256(p)}
                      for p in sorted(out.glob("*.onnx"))}, **details,
    }
    (out / "quantization.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--pt", required=True, help="Local trained YOLO detection checkpoint")
    parser.add_argument("--data", required=True, help="Local YOLO dataset YAML with matching class order")
    parser.add_argument("--output-dir", required=True, help="New output directory; must not exist")
    parser.add_argument("--imgsz", nargs=2, type=int, default=[384, 640], metavar=("HEIGHT", "WIDTH"))
    parser.add_argument("--calib-split", default="train", help="Use train to avoid calibration on evaluation data")
    parser.add_argument("--calib-limit", type=int, default=200)
