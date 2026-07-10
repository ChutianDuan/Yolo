#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import sys

import cv2
import numpy as np
import onnxruntime as ort
import openvino as ov


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare ONNX Runtime and OpenVINO raw model outputs on the same frame."
    )
    parser.add_argument("--onnx", required=True, help="Input ONNX model path")
    parser.add_argument(
        "--ir",
        help="Optional OpenVINO IR .xml path. If omitted, the ONNX model is converted in memory.",
    )
    parser.add_argument("--image", help="Image path used as input")
    parser.add_argument("--video", help="Video path used as input")
    parser.add_argument(
        "--frame-index",
        type=int,
        default=0,
        help="Zero-based video frame index to compare when --video is used.",
    )
    parser.add_argument("--input-width", type=int, help="Model input width")
    parser.add_argument("--input-height", type=int, help="Model input height")
    parser.add_argument("--device", default="CPU", help="OpenVINO device name")
    parser.add_argument("--direct-resize", action="store_true", help="Disable letterbox padding")
    parser.add_argument("--max-abs-tol", type=float, help="Optional max absolute diff limit")
    parser.add_argument("--mean-abs-tol", type=float, help="Optional mean absolute diff limit")
    return parser.parse_args()


def static_input_hw(session: ort.InferenceSession) -> tuple[int | None, int | None]:
    shape = session.get_inputs()[0].shape
    if len(shape) != 4:
        return None, None
    height = shape[2] if isinstance(shape[2], int) else None
    width = shape[3] if isinstance(shape[3], int) else None
    return height, width


def read_frame(args: argparse.Namespace) -> np.ndarray:
    if bool(args.image) == bool(args.video):
        raise ValueError("Set exactly one of --image or --video")

    if args.image:
        image = cv2.imread(str(pathlib.Path(args.image)))
        if image is None:
            raise FileNotFoundError(f"failed to read image: {args.image}")
        return image

    if args.frame_index < 0:
        raise ValueError("--frame-index must be non-negative")

    capture = cv2.VideoCapture(str(pathlib.Path(args.video)))
    if not capture.isOpened():
        raise FileNotFoundError(f"failed to open video: {args.video}")
    if args.frame_index:
        capture.set(cv2.CAP_PROP_POS_FRAMES, args.frame_index)
    ok, frame = capture.read()
    capture.release()
    if not ok or frame is None:
        raise RuntimeError(f"failed to read frame {args.frame_index} from {args.video}")
    return frame


def letterbox(image: np.ndarray, target_width: int, target_height: int) -> np.ndarray:
    image_height, image_width = image.shape[:2]
    ratio = min(target_width / image_width, target_height / image_height)
    resized_width = min(target_width, max(1, int(round(image_width * ratio))))
    resized_height = min(target_height, max(1, int(round(image_height * ratio))))

    if resized_width == image_width and resized_height == image_height:
        resized = image
    else:
        resized = cv2.resize(image, (resized_width, resized_height))

    pad_width = target_width - resized_width
    pad_height = target_height - resized_height
    left = pad_width // 2
    right = pad_width - left
    top = pad_height // 2
    bottom = pad_height - top
    return cv2.copyMakeBorder(
        resized,
        top,
        bottom,
        left,
        right,
        cv2.BORDER_CONSTANT,
        value=(114, 114, 114),
    )


def preprocess(
    image: np.ndarray,
    input_width: int,
    input_height: int,
    use_letterbox: bool,
) -> np.ndarray:
    if use_letterbox:
        model_image = letterbox(image, input_width, input_height)
    else:
        model_image = cv2.resize(image, (input_width, input_height))

    rgb = cv2.cvtColor(model_image, cv2.COLOR_BGR2RGB)
    chw = np.transpose(rgb, (2, 0, 1)).astype(np.float32) / 255.0
    return np.expand_dims(chw, axis=0)


def openvino_outputs(
    onnx_path: pathlib.Path,
    ir_path: pathlib.Path | None,
    device: str,
    input_tensor: np.ndarray,
) -> list[np.ndarray]:
    core = ov.Core()
    if ir_path is None:
        model = ov.convert_model(str(onnx_path))
    else:
        model = core.read_model(str(ir_path))

    compiled = core.compile_model(model, device)
    result = compiled([input_tensor])
    return [np.asarray(result[output]) for output in compiled.outputs]


def compare_outputs(
    ort_outputs: list[np.ndarray],
    ov_outputs: list[np.ndarray],
    max_abs_tol: float | None,
    mean_abs_tol: float | None,
) -> bool:
    if len(ort_outputs) != len(ov_outputs):
        print(f"output_count_mismatch: onnx={len(ort_outputs)} openvino={len(ov_outputs)}")
        return False

    passed = True
    for index, (ort_output, ov_output) in enumerate(zip(ort_outputs, ov_outputs)):
        if ort_output.shape != ov_output.shape:
            print(
                f"output[{index}].shape_mismatch: "
                f"onnx={ort_output.shape} openvino={ov_output.shape}"
            )
            passed = False
            continue

        ort_float = ort_output.astype(np.float32, copy=False)
        ov_float = ov_output.astype(np.float32, copy=False)
        abs_diff = np.abs(ort_float - ov_float)
        max_abs = float(abs_diff.max()) if abs_diff.size else 0.0
        mean_abs = float(abs_diff.mean()) if abs_diff.size else 0.0
        denom = np.maximum(np.abs(ort_float), 1.0e-6)
        rel_diff = abs_diff / denom
        max_rel = float(rel_diff.max()) if rel_diff.size else 0.0
        mean_rel = float(rel_diff.mean()) if rel_diff.size else 0.0

        print(f"output[{index}].shape={list(ort_output.shape)}")
        print(f"output[{index}].max_abs_diff={max_abs:.8f}")
        print(f"output[{index}].mean_abs_diff={mean_abs:.8f}")
        print(f"output[{index}].max_rel_diff={max_rel:.8f}")
        print(f"output[{index}].mean_rel_diff={mean_rel:.8f}")

        if max_abs_tol is not None and max_abs > max_abs_tol:
            print(f"output[{index}].max_abs_exceeds_tol={max_abs_tol}")
            passed = False
        if mean_abs_tol is not None and mean_abs > mean_abs_tol:
            print(f"output[{index}].mean_abs_exceeds_tol={mean_abs_tol}")
            passed = False

    return passed


def main() -> int:
    args = parse_args()
    onnx_path = pathlib.Path(args.onnx).resolve()
    ir_path = pathlib.Path(args.ir).resolve() if args.ir else None

    if not onnx_path.is_file():
        raise FileNotFoundError(f"ONNX model not found: {onnx_path}")
    if ir_path is not None and not ir_path.is_file():
        raise FileNotFoundError(f"OpenVINO IR not found: {ir_path}")

    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    shape_height, shape_width = static_input_hw(session)
    input_width = args.input_width or shape_width
    input_height = args.input_height or shape_height
    if input_width is None or input_height is None:
        raise ValueError("Set --input-width and --input-height for dynamic-shape models")
    if input_width <= 0 or input_height <= 0:
        raise ValueError("input width and height must be positive")

    frame = read_frame(args)
    input_tensor = preprocess(
        frame,
        input_width=input_width,
        input_height=input_height,
        use_letterbox=not args.direct_resize,
    )

    input_name = session.get_inputs()[0].name
    output_names = [output.name for output in session.get_outputs()]
    ort_outputs = session.run(output_names, {input_name: input_tensor})
    ov_raw_outputs = openvino_outputs(onnx_path, ir_path, args.device, input_tensor)

    print(f"onnx_model={onnx_path}")
    print(f"openvino_model={ir_path if ir_path is not None else 'in_memory_conversion'}")
    print(f"input_shape={list(input_tensor.shape)}")
    print(f"openvino_device={args.device}")
    ok = compare_outputs(
        ort_outputs,
        ov_raw_outputs,
        max_abs_tol=args.max_abs_tol,
        mean_abs_tol=args.mean_abs_tol,
    )
    return 0 if ok else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
