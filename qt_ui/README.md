# YOLO Qt UI

这是一个独立的 Qt Widgets 桌面界面，只做图片选择、模型加载、单图推理和检测框显示。

## 依赖

Qt 通过系统已有的 `/root/vcpkg` 安装，不需要安装全局 Qt，也不在 `qt_ui` 目录维护单独的 vcpkg 安装树。当前界面依赖：

- `qtbase`
- `opencv4`
- `onnxruntime`
- `jsoncpp`
- `ffmpeg`，包含 `ffmpeg`、`ffprobe` 和 `x264`

如果沿用项目现有的 `/root/vcpkg` 和 `x64-linux-gcc15` triplet，可以先在仓库根目录检查环境：

```bash
bash qt_ui/check_env.sh
```

不要在 `qt_ui` 目录直接运行不带 `--classic` 的 `vcpkg install`，否则 vcpkg 会按 manifest 模式生成 `qt_ui/vcpkg_installed/`。如需补库，统一安装到 `/root/vcpkg/installed/x64-linux-gcc15`。

当前环境没有使用系统 autotools；相关构建工具放在 `/root/vcpkg/downloads/tools`，包括 `autoconf`、`automake`、`libtoolize`、`gperf`、`flex`、`bison` 和 `nasm`。Qt 的 XCB cursor 依赖通过 `/root/vcpkg/custom-ports/xcb-cursor` 这个 overlay port 管理。
同一个 vcpkg triplet 也维护 C++ HTTP 服务使用的 `drogon`，避免 Qt 和服务端分成两套 native 依赖树。

```bash
export PATH=/root/vcpkg/downloads/tools/gperf-3.1/bin:/root/vcpkg/downloads/tools/flex-2.6.4/bin:/root/vcpkg/downloads/tools/bison-3.8.2/bin:/root/vcpkg/downloads/tools/autotools/bin:/root/vcpkg/downloads/tools/nasm-3.01/bin:/root/vcpkg/.toolchains/gcc15/bin:/root/vcpkg:${PATH}
export ACLOCAL_PATH=/root/vcpkg/installed/x64-linux-gcc15/share/aclocal:/root/vcpkg/downloads/tools/autotools/share/aclocal
export LD_LIBRARY_PATH=/root/vcpkg/downloads/tools/autotools/lib:/root/vcpkg/.toolchains/gcc15/lib:${LD_LIBRARY_PATH:-}
/root/vcpkg/vcpkg install 'qtbase[core,widgets,xcb,xrender,fontconfig,png,jpeg]' \
  'opencv4[core,ffmpeg,jpeg,png,tiff,webp]' onnxruntime drogon jsoncpp 'ffmpeg[ffmpeg,ffprobe,x264]' \
  --classic \
  --triplet x64-linux-gcc15 \
  --host-triplet x64-linux-gcc15 \
  --overlay-triplets=/root/vcpkg/custom-triplets \
  --overlay-ports=/root/vcpkg/custom-ports \
  --recurse
```

## 构建

```bash
cd qt_ui
cmake --preset vcpkg-gcc15-release
cmake --build --preset vcpkg-gcc15-release
```

如果不用 preset，也可以在仓库根目录手动配置：

```bash
cmake -S qt_ui -B build-qt -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/root/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_MANIFEST_MODE=OFF \
  -DVCPKG_TARGET_TRIPLET=x64-linux-gcc15 \
  -DVCPKG_HOST_TRIPLET=x64-linux-gcc15 \
  -DVCPKG_OVERLAY_TRIPLETS=/root/vcpkg/custom-triplets \
  -DVCPKG_OVERLAY_PORTS=/root/vcpkg/custom-ports \
  -DCMAKE_C_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-g++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build build-qt
```

## 运行

```bash
./build-qt/yolo_qt
```

界面默认会尝试查找 `yolo_onnx_cpp/config.yaml`。也可以在界面里手动选择其它配置文件。

Linux 服务器如果没有桌面/X11 会无法显示 Qt 窗口，需要在有图形会话的环境运行，或通过 SSH X11 转发运行。
