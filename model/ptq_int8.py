"""Export a local YOLO checkpoint and run static Conv-only PTQ INT8."""
from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path = [str(SCRIPT_DIR.parent)] + [p for p in sys.path if not p or Path(p).resolve() != SCRIPT_DIR]

import onnxruntime as ort
from onnxruntime.quantization import CalibrationMethod, QuantFormat, QuantType, quantize_static
from onnxruntime.quantization.shape_inference import quant_pre_process
from ultralytics import YOLO

from model.onnx import ImageCalibrationDataReader, export_fp32_onnx, save_classes
from model.quantization import add_common_arguments, inspect_int8, prepare_inputs, validate_classes, write_manifest


PTQ_OPTIONS = {"ActivationSymmetric": False, "WeightSymmetric": True,
               "QuantizeBias": False, "OpTypesToExcludeOutputQuantization": ["Conv"]}


def validate_exclusions(graph, excluded: list[str]) -> None:
    conv_names = {node.name for node in graph.graph.node if node.op_type == "Conv"}
    unknown = set(excluded) - conv_names
    if unknown:
        raise ValueError(f"Unknown Conv nodes: {sorted(unknown)}")


def quantize_ptq(fp32: Path, target: Path, images: list[Path], imgsz: tuple[int, int],
                 excluded: list[str] | None = None) -> None:
    import onnx

    if target.exists():
        raise FileExistsError(f"Refusing to overwrite: {target}")
    excluded = excluded or []
    with tempfile.TemporaryDirectory(prefix="yolo_ptq_") as directory:
        prepared = Path(directory) / "prepared.onnx"
        quant_pre_process(str(fp32), str(prepared), auto_merge=True)
        validate_exclusions(onnx.load(str(prepared)), excluded)
        options = ort.SessionOptions()
        options.intra_op_num_threads = 2
        session = ort.InferenceSession(str(prepared), options, providers=["CPUExecutionProvider"])
        reader = ImageCalibrationDataReader(session.get_inputs()[0].name, images, imgsz)
        quantize_static(
            str(prepared), str(target), reader,
            quant_format=QuantFormat.QDQ, activation_type=QuantType.QUInt8,
            weight_type=QuantType.QInt8, per_channel=True,
            calibrate_method=CalibrationMethod.MinMax, op_types_to_quantize=["Conv"],
            nodes_to_exclude=excluded, extra_options=PTQ_OPTIONS,
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    add_common_arguments(parser)
    parser.add_argument("--exclude-node", action="append", default=[],
                        help="Exact preprocessed Conv node name to keep floating; repeatable")
    args = parser.parse_args()
    pt, data, images = prepare_inputs(args)
    validate_classes(YOLO(str(pt)), data)
    out = Path(args.output_dir).expanduser().resolve()
    out.mkdir(parents=True, exist_ok=False)
    imgsz = tuple(args.imgsz)
    names = export_fp32_onnx(pt, out / "fp32.onnx", imgsz, 13, False, "cpu")
    save_classes(names, out / "classes.json")
    quantize_ptq(out / "fp32.onnx", out / "ptq_int8.onnx", images, imgsz, args.exclude_node)
    details = inspect_int8(out / "ptq_int8.onnx", imgsz, args.exclude_node)
    write_manifest(out, "ptq", pt, data, images, args, details)
    print(f"PTQ complete: {out / 'ptq_int8.onnx'}")


if __name__ == "__main__":
    main()
