#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import shutil
import sys

import openvino as ov


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert an ONNX model to OpenVINO IR (.xml/.bin)."
    )
    parser.add_argument("--model", required=True, help="Input ONNX model path")
    parser.add_argument(
        "--output",
        required=True,
        help="Output .xml path. Existing files are not overwritten unless --overwrite is set.",
    )
    parser.add_argument(
        "--fp16",
        action="store_true",
        help="Save compressed FP16 weights. Default keeps FP32 for closer ONNX parity.",
    )
    parser.add_argument(
        "--fp32",
        action="store_true",
        help="Deprecated no-op kept for compatibility; FP32 is the default.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing .xml/.bin output files.",
    )
    parser.add_argument(
        "--copy-classes",
        action="store_true",
        help="Copy classes.json from the ONNX model directory to the IR output directory.",
    )
    return parser.parse_args()


def ensure_output_path(raw_output: str) -> pathlib.Path:
    output = pathlib.Path(raw_output)
    if output.suffix.lower() != ".xml":
        raise ValueError("--output must end with .xml")
    return output


def main() -> int:
    args = parse_args()
    model_path = pathlib.Path(args.model).resolve()
    output_xml = ensure_output_path(args.output).resolve()
    output_bin = output_xml.with_suffix(".bin")

    if not model_path.is_file():
        raise FileNotFoundError(f"ONNX model not found: {model_path}")
    if model_path.suffix.lower() != ".onnx":
        raise ValueError(f"Expected an ONNX model, got: {model_path}")
    if not args.overwrite and (output_xml.exists() or output_bin.exists()):
        raise FileExistsError(
            f"Refusing to overwrite existing OpenVINO files: {output_xml} / {output_bin}"
        )

    output_xml.parent.mkdir(parents=True, exist_ok=True)
    model = ov.convert_model(str(model_path))
    compress_to_fp16 = args.fp16 and not args.fp32
    ov.save_model(model, str(output_xml), compress_to_fp16=compress_to_fp16)

    if args.copy_classes:
        classes_path = model_path.parent / "classes.json"
        if classes_path.exists():
            shutil.copy2(classes_path, output_xml.parent / "classes.json")

    print(f"converted_model={model_path}")
    print(f"openvino_xml={output_xml}")
    print(f"openvino_bin={output_bin}")
    print(f"precision={'FP16' if compress_to_fp16 else 'FP32'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
