#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import pathlib
import subprocess
import sys


def module_status(name: str) -> str:
    try:
        return "yes" if importlib.util.find_spec(name) is not None else "no"
    except ModuleNotFoundError:
        return "no"


def run(command: list[str]) -> tuple[int, str]:
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return completed.returncode, completed.stdout.strip()


def main() -> int:
    print(f"python: {sys.executable}")
    print(f"python_version: {sys.version.split()[0]}")

    for module in ("openvino", "onnx", "onnxruntime", "numpy", "torch", "cv2"):
        print(f"module.{module}: {module_status(module)}")

    if module_status("openvino") == "yes":
        import openvino as ov

        root = pathlib.Path(ov.__file__).resolve().parent
        print(f"openvino_version: {ov.__version__}")
        print(f"openvino_root: {root}")
        print(f"openvino_cpp_headers: {(root / 'include' / 'openvino' / 'openvino.hpp').exists()}")
        print(f"openvino_runtime_libs: {(root / 'libs').exists()}")

    if module_status("onnxruntime") == "yes":
        import onnxruntime as ort

        print(f"onnxruntime_version: {ort.__version__}")
        print(f"onnxruntime_providers: {','.join(ort.get_available_providers())}")

    code, output = run([sys.executable, "-m", "pip", "check"])
    print(f"pip_check_exit_code: {code}")
    if output:
        print(output)
    if code != 0:
        return code

    vcpkg_share = pathlib.Path("/root/vcpkg/installed/x64-linux-gcc15/share/openvino")
    vcpkg_mlas = pathlib.Path("/root/vcpkg/installed/x64-linux-gcc15/lib/libmlas.a")
    print(f"vcpkg_openvino_installed: {vcpkg_share.exists()}")
    print(f"vcpkg_openvino_static_mlas_conflict: {vcpkg_mlas.exists()}")
    if not vcpkg_share.exists():
        print(
            "vcpkg_openvino_hint: /root/vcpkg/vcpkg install "
            "'openvino[core,cpu,ir,onnx]:x64-linux-gcc15' "
            "--overlay-triplets=/root/vcpkg/custom-triplets "
            "--overlay-ports=/root/vcpkg/custom-ports"
        )
    if vcpkg_mlas.exists():
        print(
            "vcpkg_openvino_conflict: static OpenVINO libmlas.a conflicts with "
            "static ONNX Runtime libonnxruntime_mlas.a; use the custom OpenVINO "
            "overlay port with -DENABLE_MLAS_FOR_CPU=OFF."
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
