"""One-off High model PTQ experiment. Never overwrite existing artifacts."""
from __future__ import annotations

import argparse
import importlib.util
import json
import shutil
import time
from collections import Counter
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnxruntime.quantization import (
    CalibrationMethod, QuantFormat, QuantType, quantize_static,
)
from onnxruntime.quantization.calibrate import CalibraterBase
from onnxruntime.quantization.shape_inference import quant_pre_process


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--calibration-count", type=int, default=200)
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.source.parent / "classes.json", out / "classes.json")
    spec = importlib.util.spec_from_file_location("project_export", args.repo / "model/onnx.py")
    exporter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(exporter)
    original = onnx.load(str(args.source))
    dims = [d.dim_value for d in original.graph.input[0].type.tensor_type.shape.dim]
    if dims != [1, 3, 736, 1280]:
        raise ValueError(f"Expected deployed High input, got {dims}")
    # Upgrade operator schemas without re-exporting or changing trained weights.
    fp32 = onnx.version_converter.convert_version(original, 13)
    fp32_path = out / "high_fp32_opset13.onnx"
    onnx.checker.check_model(fp32)
    onnx.save(fp32, str(fp32_path))
    images = exporter.collect_calibration_images(args.data, "val", args.calibration_count)
    if len(images) != args.calibration_count:
        raise ValueError("Insufficient calibration images")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 8
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    before = ort.InferenceSession(str(args.source), sess_options=options)
    after = ort.InferenceSession(str(fp32_path), sess_options=options)
    parity = []
    for path in images[:3]:
        inp = exporter.preprocess_image(path, (736, 1280))
        left = before.run(None, {before.get_inputs()[0].name: inp})[0]
        right = after.run(None, {after.get_inputs()[0].name: inp})[0]
        np.testing.assert_allclose(left, right, rtol=1e-5, atol=1e-4)
        parity.append({"image": str(path), "maximum_absolute_difference": float(np.max(np.abs(left-right)))})
    del before, after
    preprocessed = out / "high_fp32_preprocessed.onnx"
    quant_pre_process(str(fp32_path), str(preprocessed), auto_merge=True)
    input_name = fp32.graph.input[0].name

    class Reader(exporter.ImageCalibrationDataReader):
        def get_next(self) -> dict | None:
            data = super().get_next()
            if data is not None:
                self.count = getattr(self, "count", 0) + 1
                if self.count % 20 == 0:
                    print(f"calibration {self.count}/{len(self.image_paths)}", flush=True)
            return data

    # The installed quantizer exposes no thread argument for calibration sessions.
    def bounded_session(self) -> None:
        calibration_options = ort.SessionOptions()
        calibration_options.intra_op_num_threads = 8
        calibration_options.inter_op_num_threads = 1
        calibration_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
        self.infer_session = ort.InferenceSession(
            self.augmented_model_path, sess_options=calibration_options,
            providers=self.execution_providers,
        )

    CalibraterBase.create_inference_session = bounded_session
    start = time.monotonic()
    int8_path = out / "high_int8_u8s8_perchannel.onnx"
    quantize_static(
        str(preprocessed), str(int8_path), Reader(input_name, images, (736, 1280)),
        quant_format=QuantFormat.QDQ, activation_type=QuantType.QUInt8,
        weight_type=QuantType.QInt8, calibrate_method=CalibrationMethod.MinMax,
        per_channel=True, op_types_to_quantize=["Conv"],
    )
    quantized = onnx.load(str(int8_path))
    onnx.checker.check_model(quantized)
    import hashlib

    def sha(path: Path) -> str:
        with path.open("rb") as stream:
            return hashlib.file_digest(stream, "sha256").hexdigest() if hasattr(hashlib, "file_digest") else hashlib.sha256(stream.read()).hexdigest()

    metadata = {
        "source": {"path": str(args.source.resolve()), "sha256": sha(args.source)},
        "fp32": {"path": str(fp32_path), "sha256": sha(fp32_path), "size_bytes": fp32_path.stat().st_size},
        "int8": {"path": str(int8_path), "sha256": sha(int8_path), "size_bytes": int8_path.stat().st_size},
        "shape": dims, "profile": "QDQ Conv only; U8 activations, S8 per-channel weights; MinMax; opset13",
        "calibration_seconds": time.monotonic()-start,
        "calibration_split": "val", "calibration_images": [{"path": str(p), "sha256": sha(p)} for p in images],
        "opset_conversion_parity": parity, "quantized_ops": dict(Counter(n.op_type for n in quantized.graph.node)),
        "versions": {"onnx": onnx.__version__, "onnxruntime": ort.__version__},
        "note": "YOLO26 end-to-end top-k output [1,300,6]; source metadata nms=False; no graph NonMaxSuppression op",
    }
    (out / "quantization.json").write_text(json.dumps(metadata, indent=2)+"\n")
    print(json.dumps({"fp32": str(fp32_path), "int8": str(int8_path), "seconds": metadata["calibration_seconds"]}), flush=True)


if __name__ == "__main__":
    main()
