#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import pathlib
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Smoke test OpenVINO conversion and ONNX/OpenVINO output parity."
    )
    parser.add_argument("--onnx", required=True, help="Input ONNX model path")
    parser.add_argument("--video", required=True, help="Video used for one-frame comparison")
    parser.add_argument("--output-dir", required=True, help="Temporary output directory")
    parser.add_argument("--input-width", type=int, required=True)
    parser.add_argument("--input-height", type=int, required=True)
    parser.add_argument("--device", default="CPU")
    parser.add_argument("--max-abs-tol", type=float, default=0.001)
    parser.add_argument("--mean-abs-tol", type=float, default=0.0001)
    return parser.parse_args()


def module_available(name: str) -> bool:
    try:
        return importlib.util.find_spec(name) is not None
    except ModuleNotFoundError:
        return False


def skip(message: str) -> int:
    print(f"SKIP_OPENVINO: {message}")
    return 0


def run(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True)


def main() -> int:
    args = parse_args()
    onnx_path = pathlib.Path(args.onnx).resolve()
    video_path = pathlib.Path(args.video).resolve()
    output_dir = pathlib.Path(args.output_dir).resolve()

    if not onnx_path.is_file():
        return skip(f"missing ONNX model: {onnx_path}")
    if not video_path.is_file():
        return skip(f"missing video: {video_path}")

    required_modules = ("openvino", "onnxruntime", "cv2", "numpy")
    missing = [name for name in required_modules if not module_available(name)]
    if missing:
        return skip("missing Python modules: " + ", ".join(missing))

    tools_dir = pathlib.Path(__file__).resolve().parent
    output_dir.mkdir(parents=True, exist_ok=True)
    output_xml = output_dir / (onnx_path.stem + ".xml")

    run([
        sys.executable,
        str(tools_dir / "convert_to_openvino.py"),
        "--model",
        str(onnx_path),
        "--output",
        str(output_xml),
        "--copy-classes",
        "--overwrite",
    ])
    run([
        sys.executable,
        str(tools_dir / "compare_openvino_onnx.py"),
        "--onnx",
        str(onnx_path),
        "--ir",
        str(output_xml),
        "--video",
        str(video_path),
        "--input-width",
        str(args.input_width),
        "--input-height",
        str(args.input_height),
        "--device",
        args.device,
        "--max-abs-tol",
        str(args.max_abs_tol),
        "--mean-abs-tol",
        str(args.mean_abs_tol),
    ])
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode)
