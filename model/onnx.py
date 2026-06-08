from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Iterable

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path = [
    entry
    for entry in sys.path
    if not entry or Path(entry).resolve() != SCRIPT_DIR
]

import cv2
import numpy as np
import onnx
import onnxruntime as ort
import yaml
from onnxruntime.quantization import (
    CalibrationDataReader,
    CalibrationMethod,
    QuantFormat,
    QuantType,
    quantize_static,
)
from onnxruntime.quantization.shape_inference import quant_pre_process
from ultralytics import YOLO

ROOT = Path(__file__).resolve().parent

IMAGE_WIDTH = 1280
IMAGE_HEIGHT = 736
IMG_SIZE = (IMAGE_HEIGHT, IMAGE_WIDTH)  # Ultralytics: (h, w)

DEFAULT_PT_PATH = ROOT / "runs/detect/bdd100k_yolo26s_det_1280x736/weights/best.pt"
DEFAULT_DATA_YAML = ROOT / "data/bdd100k_yolo_det/data.yaml"
DEFAULT_DEPLOY_DIR = ROOT.parent / "yolo_onnx_cpp" / "deploy"
DEFAULT_CALIB_LIMIT = 200
QUANTIZE_OP_TYPES = ["Conv"]

IMAGE_SUFFIXES = {".bmp", ".dng", ".jpeg", ".jpg", ".mpo", ".png", ".tif", ".tiff", ".webp"}


class ImageCalibrationDataReader(CalibrationDataReader):
    def __init__(self, input_name: str, image_paths: list[Path], imgsz: tuple[int, int]):
        self.input_name = input_name
        self.image_paths = image_paths
        self.imgsz = imgsz
        self._iterator = iter(self.image_paths)

    def get_next(self) -> dict[str, np.ndarray] | None:
        try:
            image_path = next(self._iterator)
        except StopIteration:
            return None
        return {self.input_name: preprocess_image(image_path, self.imgsz)}

    def rewind(self) -> None:
        self._iterator = iter(self.image_paths)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export YOLO-seg ONNX and ONNX Runtime INT8 model.")
    parser.add_argument("--pt", default=str(DEFAULT_PT_PATH), help="YOLO .pt weight path")
    parser.add_argument("--data", default=str(DEFAULT_DATA_YAML), help="YOLO data.yaml for INT8 calibration")
    parser.add_argument("--deploy-dir", default=str(DEFAULT_DEPLOY_DIR), help="Directory for exported deploy files")
    parser.add_argument("--fp32-name", default="best.onnx", help="FP32 ONNX filename under deploy dir")
    parser.add_argument("--int8-name", default="best_int8.onnx", help="INT8 ONNX filename under deploy dir")
    parser.add_argument("--classes-name", default="classes.json", help="Class metadata filename under deploy dir")
    parser.add_argument("--imgsz", nargs=2, type=int, default=IMG_SIZE, metavar=("HEIGHT", "WIDTH"))
    parser.add_argument("--opset", type=int, default=12)
    parser.add_argument("--device", default="cpu", help="Ultralytics export device, e.g. cpu or cuda:0")
    parser.add_argument("--calib-split", default="val", help="Dataset split used for INT8 calibration")
    parser.add_argument(
        "--calib-limit",
        type=int,
        default=DEFAULT_CALIB_LIMIT,
        help="Max calibration images; 0 means all",
    )
    parser.add_argument("--skip-int8", action="store_true", help="Only export FP32 ONNX")
    parser.add_argument("--no-simplify", action="store_true", help="Disable ONNX slimming/simplification")
    parser.add_argument("--no-validate", action="store_true", help="Skip ONNX checker and ONNX Runtime smoke test")
    return parser.parse_args()


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"{label} not found: {path}")


def load_data_yaml(data_yaml: Path) -> dict:
    require_file(data_yaml, "data.yaml")
    with data_yaml.open("r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    if not isinstance(data, dict):
        raise ValueError(f"data.yaml must be a mapping: {data_yaml}")
    return data


def resolve_path(value: str | Path, root: Path, fallback_base: Path) -> Path:
    path = Path(value).expanduser()
    if path.is_absolute():
        return path

    root_path = root / path
    if root_path.exists():
        return root_path
    return fallback_base / path


def iter_split_entries(value: str | list, root: Path, yaml_dir: Path) -> Iterable[Path]:
    if isinstance(value, (list, tuple)):
        for item in value:
            yield from iter_split_entries(item, root, yaml_dir)
        return
    if not isinstance(value, str):
        raise TypeError(f"Unsupported split entry: {value!r}")
    yield resolve_path(value, root, yaml_dir)


def collect_from_path(path: Path, root: Path) -> list[Path]:
    if path.is_dir():
        return [p for p in path.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES]

    if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES:
        return [path]

    if path.is_file():
        images: list[Path] = []
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                item = resolve_path(line, root, path.parent)
                images.extend(collect_from_path(item, root))
        return images

    return []


def collect_calibration_images(data_yaml: Path, split: str, limit: int) -> list[Path]:
    data = load_data_yaml(data_yaml)
    root_value = data.get("path", data_yaml.parent)
    root = Path(root_value).expanduser()
    if not root.is_absolute():
        root = data_yaml.parent / root

    if split not in data:
        available = ", ".join(str(k) for k in data.keys())
        raise KeyError(f"split '{split}' not found in {data_yaml}; available keys: {available}")

    images: list[Path] = []
    for entry in iter_split_entries(data[split], root, data_yaml.parent):
        images.extend(collect_from_path(entry, root))

    images = sorted(dict.fromkeys(p.resolve() for p in images))
    if limit > 0:
        images = images[:limit]
    if not images:
        raise FileNotFoundError(f"No calibration images found for split '{split}' in {data_yaml}")
    return images


def letterbox_bgr(image: np.ndarray, imgsz: tuple[int, int]) -> np.ndarray:
    target_h, target_w = imgsz
    image_h, image_w = image.shape[:2]
    ratio = min(target_w / image_w, target_h / image_h)
    resized_w = min(target_w, max(1, int(round(image_w * ratio))))
    resized_h = min(target_h, max(1, int(round(image_h * ratio))))

    if image_w != resized_w or image_h != resized_h:
        image = cv2.resize(image, (resized_w, resized_h), interpolation=cv2.INTER_LINEAR)

    pad_w = target_w - resized_w
    pad_h = target_h - resized_h
    left = pad_w // 2
    right = pad_w - left
    top = pad_h // 2
    bottom = pad_h - top
    return cv2.copyMakeBorder(image, top, bottom, left, right, cv2.BORDER_CONSTANT, value=(114, 114, 114))


def preprocess_image(image_path: Path, imgsz: tuple[int, int]) -> np.ndarray:
    image = cv2.imread(str(image_path), cv2.IMREAD_COLOR)
    if image is None or image.size == 0:
        raise ValueError(f"Failed to read image: {image_path}")

    if image.shape[0] != imgsz[0] or image.shape[1] != imgsz[1]:
        image = letterbox_bgr(image, imgsz)

    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    chw = np.transpose(image, (2, 0, 1))
    return np.ascontiguousarray(chw[None, :, :, :], dtype=np.float32)


def export_fp32_onnx(
    pt_path: Path,
    target_path: Path,
    imgsz: tuple[int, int],
    opset: int,
    simplify: bool,
    device: str,
) -> dict:
    target_path.parent.mkdir(parents=True, exist_ok=True)

    # Ultralytics writes ONNX beside the .pt file, so export from a temp copy to keep runs/ clean.
    with tempfile.TemporaryDirectory(prefix="yolo_onnx_export_") as tmp_dir:
        tmp_pt = Path(tmp_dir) / pt_path.name
        shutil.copy2(pt_path, tmp_pt)
        model = YOLO(str(tmp_pt))
        exported_path = Path(
            model.export(
                format="onnx",
                imgsz=imgsz,
                opset=opset,
                simplify=simplify,
                dynamic=False,
                half=False,
                nms=False,
                batch=1,
                device=device,
            )
        )
        shutil.copy2(exported_path, target_path)
        return dict(model.names)


def save_classes(names: dict, classes_path: Path) -> None:
    classes_path.parent.mkdir(parents=True, exist_ok=True)
    with classes_path.open("w", encoding="utf-8") as f:
        json.dump(names, f, ensure_ascii=False, indent=2)
        f.write("\n")


def quantize_int8_onnx(
    fp32_path: Path,
    int8_path: Path,
    data_yaml: Path,
    split: str,
    imgsz: tuple[int, int],
    calib_limit: int,
) -> None:
    images = collect_calibration_images(data_yaml, split, calib_limit)
    print(f"INT8 calibration images: {len(images)} ({split})")
    print("INT8 quantized op types: " + ", ".join(QUANTIZE_OP_TYPES))

    with tempfile.TemporaryDirectory(prefix="yolo_onnx_quant_") as tmp_dir:
        quant_input = Path(tmp_dir) / "preprocessed.onnx"
        try:
            quant_pre_process(
                input_model=str(fp32_path),
                output_model_path=str(quant_input),
                auto_merge=True,
            )
        except Exception as exc:
            print(f"Quantization pre-process failed, using original ONNX: {exc}")
            quant_input = fp32_path

        session = ort.InferenceSession(str(quant_input), providers=["CPUExecutionProvider"])
        input_name = session.get_inputs()[0].name
        reader = ImageCalibrationDataReader(input_name, images, imgsz)

        quantize_static(
            model_input=str(quant_input),
            model_output=str(int8_path),
            calibration_data_reader=reader,
            quant_format=QuantFormat.QDQ,
            activation_type=QuantType.QInt8,
            weight_type=QuantType.QInt8,
            calibrate_method=CalibrationMethod.MinMax,
            per_channel=False,
            op_types_to_quantize=QUANTIZE_OP_TYPES,
        )


def input_shape_for_dummy(input_shape: list, imgsz: tuple[int, int]) -> list[int]:
    fallback = [1, 3, imgsz[0], imgsz[1]]
    shape: list[int] = []
    for i, dim in enumerate(input_shape):
        if isinstance(dim, int) and dim > 0:
            shape.append(dim)
        elif i < len(fallback):
            shape.append(fallback[i])
        else:
            raise ValueError(f"Cannot build dummy input for dynamic shape: {input_shape}")
    return shape


def validate_onnx(path: Path, imgsz: tuple[int, int]) -> None:
    onnx.checker.check_model(str(path))
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    input_info = session.get_inputs()[0]
    dummy_shape = input_shape_for_dummy(list(input_info.shape), imgsz)
    dummy = np.zeros(dummy_shape, dtype=np.float32)
    outputs = session.run(None, {input_info.name: dummy})
    output_desc = ", ".join(
        f"{meta.name}{tuple(value.shape)}:{value.dtype}"
        for meta, value in zip(session.get_outputs(), outputs)
    )
    print(f"Validated {path}: input={input_info.name}{tuple(dummy_shape)}, outputs={output_desc}")


def main() -> None:
    args = parse_args()
    pt_path = Path(args.pt).expanduser().resolve()
    data_yaml = Path(args.data).expanduser().resolve()
    deploy_dir = Path(args.deploy_dir).expanduser().resolve()
    imgsz = (int(args.imgsz[0]), int(args.imgsz[1]))

    require_file(pt_path, ".pt weight")
    if not args.skip_int8:
        require_file(data_yaml, "data.yaml")

    fp32_path = deploy_dir / args.fp32_name
    int8_path = deploy_dir / args.int8_name
    classes_path = deploy_dir / args.classes_name

    names = export_fp32_onnx(
        pt_path=pt_path,
        target_path=fp32_path,
        imgsz=imgsz,
        opset=args.opset,
        simplify=not args.no_simplify,
        device=args.device,
    )
    save_classes(names, classes_path)
    print(f"FP32 ONNX exported to: {fp32_path}")
    print(f"Classes saved to: {classes_path}")

    if not args.no_validate:
        validate_onnx(fp32_path, imgsz)

    if not args.skip_int8:
        quantize_int8_onnx(
            fp32_path=fp32_path,
            int8_path=int8_path,
            data_yaml=data_yaml,
            split=args.calib_split,
            imgsz=imgsz,
            calib_limit=args.calib_limit,
        )
        print(f"INT8 ONNX exported to: {int8_path}")
        if not args.no_validate:
            validate_onnx(int8_path, imgsz)


if __name__ == "__main__":
    main()
