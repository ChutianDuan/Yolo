# Repository Instructions

适用范围：本文件位于仓库根目录，默认适用于整个项目。

## 环境边界

- C++ 代码、CMake 配置、Qt UI 和 Drogon/ONNX Runtime 服务全部走 `/root/vcpkg`，不要改用系统包、全局 Qt/OpenCV、Conda 里的 C++ 库，或项目子目录里的 `vcpkg_installed/`。
- C++ 默认使用 `/root/vcpkg` 的 `x64-linux-gcc15` triplet、GCC15 toolchain 和 vcpkg toolchain file。服务端构建从 `yolo_onnx_cpp/` 目录运行：

```bash
cd yolo_onnx_cpp
cmake --preset vcpkg-gcc15-release
cmake --build --preset vcpkg-gcc15-release
```

- 如果需要手动配置 CMake，保持这些关键参数一致：

```bash
-DCMAKE_TOOLCHAIN_FILE=/root/vcpkg/scripts/buildsystems/vcpkg.cmake
-DVCPKG_MANIFEST_MODE=OFF
-DVCPKG_TARGET_TRIPLET=x64-linux-gcc15
-DVCPKG_HOST_TRIPLET=x64-linux-gcc15
-DVCPKG_OVERLAY_TRIPLETS=/root/vcpkg/custom-triplets
-DCMAKE_C_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-gcc
-DCMAKE_CXX_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-g++
```

- Qt 相关构建如果需要 overlay ports，使用 `/root/vcpkg/custom-ports`；不要在 `qt_ui/` 下运行会生成本地 `vcpkg_installed/` 的 vcpkg manifest 安装。
- Python 代码不要用系统 Python 或 Conda `base` 环境直接跑。按用途进入或通过 `conda run` 调用对应环境：

```bash
conda run -n yolo python ...
conda run -n rag-api python ...
```

- `yolo` 环境用于 YOLO 训练、验证、ONNX 导出和 Python 实验脚本，规格见 `envs/yolo.yml`。
- `rag-api` 环境用于 RAG/FastAPI 服务，规格见 `envs/rag-api.yml`。
- 不要合并 `yolo` 和 `rag-api` 环境；它们的 PyTorch/CUDA wheel 和 NumPy 版本边界不同。
- 修改 Python 依赖后，优先验证：

```bash
conda run -n yolo python -m pip check
conda run -n rag-api python -m pip check
```

- 修改 C++ 依赖或 CMake 配置后，优先验证：

```bash
bash qt_ui/check_env.sh
(cd yolo_onnx_cpp && cmake --preset vcpkg-gcc15-release && cmake --build --preset vcpkg-gcc15-release)
(cd qt_ui && cmake --preset vcpkg-gcc15-release && cmake --build --preset vcpkg-gcc15-release)
ctest --test-dir build --output-on-failure
```
