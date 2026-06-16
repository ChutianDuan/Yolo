# yolo 目标识别

预测效果：

<video src="Readme/dynamic_onnx_flow_detections.mp4" controls muted playsinline>
  浏览器不支持直接播放视频时，可打开 `Readme/dynamic_onnx_flow_detections.mp4` 查看预测效果。
</video>

## 环境配置

本项目建议把环境拆成两套维护：

- `conda` 训练环境：运行 YOLO 训练、验证、ONNX 导出、INT8 量化和 Python 测试脚本。
- `CMake + vcpkg` 部署环境：编译并运行 `yolo_onnx_cpp/` 下的 Drogon HTTP 服务，使用 ONNX Runtime CPU 后端推理。

两套环境边界要保持清晰：训练环境可以使用 CUDA/PyTorch；C++ 部署服务默认只依赖 CMake/vcpkg 提供的 OpenCV、Drogon、ONNX Runtime CPU，不依赖 Python 运行时。

### Conda：YOLO 训练与导出环境

用途：

- 运行 `model/train.py` 训练 YOLO 检测模型。
- 运行 `model/onnx.py` 导出 `best.onnx` / `best_int8.onnx` 到 `yolo_onnx_cpp/deploy/`。
- 运行 `model/data/` 下的数据转换、切片、评估和对比脚本。
- 运行 `yolo_onnx_cpp/test/*.py` 做 HTTP 端到端测试和视频对比实验。

推荐创建独立 conda 环境：

```bash
conda create -n yolo-train python=3.10 -y
conda activate yolo-train
```

安装 PyTorch。按机器 CUDA 版本选择对应命令；如果只做 CPU 导出和脚本测试，也可以安装 CPU 版 PyTorch。示例：

```bash
# CUDA 12.1 示例，按实际驱动和 CUDA 版本调整。
pip install torch torchvision --index-url https://download.pytorch.org/whl/cu121
```

安装训练、导出和测试脚本依赖：

```bash
pip install ultralytics opencv-python pillow pyyaml numpy tqdm
pip install onnx onnxruntime onnxruntime-tools
```

如果需要 INT8 量化，`model/onnx.py` 会使用 `onnxruntime.quantization`。确认依赖可用：

```bash
python -c "import torch, ultralytics, onnx, onnxruntime, cv2; print('ok')"
```

本机训练默认建议只暴露物理 GPU 4 和 GPU 5：

```bash
export CUDA_VISIBLE_DEVICES=4,5
python model/train.py
```

此时训练脚本里的 `device: 0,1` 对应可见设备 `cuda:0,cuda:1`，也就是物理 GPU 4、5。单卡调试时使用：

```bash
export CUDA_VISIBLE_DEVICES=4
python model/train.py
```

常用命令：

```bash
# 训练
python model/train.py

# 导出 FP32 ONNX，并可选生成 INT8 ONNX
python model/onnx.py --pt model/runs/detect/bdd100k_yolo26s_det_1280x736/weights/best.pt --data model/data/bdd100k_yolo_det/data.yaml

# 只导出 FP32，不做 INT8
python model/onnx.py --skip-int8
```

导出后 C++ 服务默认读取：

```text
yolo_onnx_cpp/deploy/best.onnx
yolo_onnx_cpp/deploy/classes.json
```

### CMake：Drogon + ONNX Runtime CPU 部署环境

用途：

- 编译 `yolo_onnx_cpp/` 下的 C++ HTTP 推理服务 `build/yolo_api`。
- 使用 Drogon 提供 `/infer` 和 `/infer_video` 接口。
- 使用 ONNX Runtime CPU 加载 `deploy/best.onnx`。
- 使用 OpenCV 做图片/视频读取、预处理、光流弱跟踪和坐标还原。

当前项目按 vcpkg 管理 C++ 依赖，主要依赖：

```text
Drogon
ONNX Runtime CPU
OpenCV core/imgproc/imgcodecs/video/videoio
OpenCV ffmpeg feature（用于读取 mp4/mov/qt 等常见视频容器）
JsonCpp
```

如果重建 C++ 部署环境，建议确保 OpenCV 带 FFmpeg 后端。当前 vcpkg 环境对应命令示例：

```bash
/root/vcpkg/vcpkg install 'opencv4[core,ffmpeg,jpeg,png,tiff,webp]' onnxruntime drogon \
  --triplet x64-linux-gcc15 \
  --overlay-triplets=/root/vcpkg/custom-triplets
```

当前 VSCode/CMake 配置使用 `/root/vcpkg` 下的 GCC15 toolchain 和 vcpkg triplet。命令行配置示例：

```bash
cmake -S yolo_onnx_cpp -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DYOLO_CONFIG_PROFILE=default -DCMAKE_TOOLCHAIN_FILE=/root/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-linux-gcc15 -DVCPKG_OVERLAY_TRIPLETS=/root/vcpkg/custom-triplets -DCMAKE_C_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-gcc -DCMAKE_CXX_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-g++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

`YOLO_CONFIG_PROFILE` 控制编译进二进制的默认配置文件：

- `default`：默认使用 `yolo_onnx_cpp/config.yaml`，适合正常服务运行。
- `reference`：默认使用 `yolo_onnx_cpp/config.reference.yaml`，适合全帧 ONNX reference 测试。

即使编译时选择了默认配置，运行时也可以通过第一个启动参数覆盖配置文件：

```bash
./build/yolo_api yolo_onnx_cpp/config.yaml
./build/yolo_api yolo_onnx_cpp/config.reference.yaml
```

部署服务不需要激活 `yolo-train` conda 环境；只要 `build/yolo_api` 能找到 vcpkg 依赖并且 `model_path` 指向存在的 ONNX 文件即可。

构建完成后建议确认：

```bash
./build/yolo_api yolo_onnx_cpp/config.yaml
```

服务启动后监听 `0.0.0.0:8080`。如果 `model_path` 是相对路径，会按配置文件所在目录解析，例如 `./deploy/best.onnx` 会解析到 `yolo_onnx_cpp/deploy/best.onnx`。


## 数据

## yolo train


## yolo 部署
部署目录为 `yolo_onnx_cpp/`，整体方案采用：

- `Drogon`：提供 HTTP 推理接口。
- `ONNX Runtime`：加载并执行 ONNX 模型。
- `OpenCV`：读取图片/视频帧、预处理图片/帧、坐标还原，并在视频跳过帧上执行 LK 光流弱跟踪。
- `ByteTrack`：轨迹管理，根据检测框或光流弱跟踪框分配并维护 `track_id`。

当前服务端代码按职责拆分为：

- `drogon/api_server.cpp`：只处理 HTTP 路由、multipart 上传、临时视频文件和错误响应。
- `video/video_inference.cpp`：执行视频推理主流程，包括异步 ONNX 强检测、LK 光流弱跟踪、动态抽帧和插值兜底。
- `video/optical_flow_tracker.cpp` / `video/optical_flow_tracker.h`：封装 LK 光流弱跟踪、帧间运动估计、运动补偿和光流质量判断。
- `drogon/response_json.cpp`：统一把 `InferResult` / `VideoInferResult` 转成 API JSON 响应。
- `model/inference_types.h`：定义内部推理结构体，例如 `Detection`、`TrackedDetection` 和 `InferResult`。

推理过程内部不把每帧结果当作 JSON 传递。单图检测、视频帧 tracks、统计信息都先保存在结构体中；只有最终 `/infer` 或 `/infer_video` 返回 HTTP 响应时才序列化为 JSON。测试脚本生成的 `full_onnx_infer_video_response.json`、`onnx_flow_infer_video_response.json`、`comparison.json`、JSONL 和 Markdown 仍是实验产物，不是服务端内部数据格式。

当前 C++ 推理适配的 ONNX 输入输出为：

- input: `images(1, 3, 736, 1280): float32`
- output: `output0(1, 300, 6): float32`
- `output0` 每行按 `[x1, y1, x2, y2, score, class_id]` 解析。

### 配置文件

默认配置文件为 `yolo_onnx_cpp/config.yaml`，可通过启动参数指定其它配置文件。

```yaml
model_path: ./deploy/best.onnx
input_width: 1280
input_height: 736
conf_threshold: 0.25
iou_threshold: 0.45
num_classes: 10
class_names:
  - person
  - rider
  - car
  - truck
  - bus
  - train
  - motor
  - bike
  - traffic light
  - traffic sign
thread_num: 4
use_letterbox: true
# 0 disables video frame sampling and runs detection on every frame.
video_detect_fps: 4.0
client_max_body_mb: 256
```

字段说明：

- `model_path`：ONNX 模型路径。相对路径按配置文件所在目录解析。
- `input_width` / `input_height`：模型输入宽高，对应 NCHW 中的 `W/H`。
- `conf_threshold`：检测置信度阈值。
- `iou_threshold`：NMS IoU 阈值。
- `num_classes`：类别数量，应与 `class_names` 数量一致。
- `class_names`：类别名称，接口返回时会根据 `class_id` 增加 `class_name`。
- `thread_num`：ONNX Runtime intra-op 线程数，同时用于 Drogon 服务线程数。
- `use_letterbox`：为 `true` 时自动将上传图片 letterbox 到模型输入尺寸；为 `false` 时要求上传图片尺寸严格等于 `input_width x input_height`。
- `video_detect_fps`：视频强检测目标帧率。大于 `0` 时按源视频 FPS 自动计算抽帧间隔，例如 24 FPS 输入、`video_detect_fps: 4.0` 时每 6 帧跑一次 YOLO；跳过帧使用光流弱跟踪补齐。设为 `0` 时每帧都跑 YOLO。
- `client_max_body_mb`：Drogon 接收上传请求体的最大大小，视频上传较大时需要调高。

### 构建

项目使用 CMake。当前 VSCode 配置中使用的是 `/root/vcpkg` 的 GCC15/vcpkg 环境：

```bash
cmake -S yolo_onnx_cpp -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/root/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux-gcc15 \
  -DVCPKG_OVERLAY_TRIPLETS=/root/vcpkg/custom-triplets \
  -DCMAKE_C_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-g++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build build
```

也可以直接使用 VSCode CMake Tools，配置来源见 `.vscode/settings.json`。做速度测试时建议确认 `build/CMakeCache.txt` 中 `CMAKE_BUILD_TYPE=Release`，否则 Debug 构建会显著拖慢视频推理。

### 启动

使用默认配置：

```bash
./build/yolo_api
```

指定配置文件：

```bash
./build/yolo_api yolo_onnx_cpp/config.yaml
```

服务默认监听：

```text
0.0.0.0:8080
```

### HTTP 推理接口

#### 图片推理

接口：

```text
POST /infer
Content-Type: multipart/form-data
form field: image
```

示例：

```bash
curl -X POST http://127.0.0.1:8080/infer \
  -F "image=@/path/to/image.jpg"
```

返回示例：

```json
{
  "code": 0,
  "message": "success",
  "output_shapes": [[1, 300, 6]],
  "detections": [
    {
      "class_id": 2,
      "class_name": "car",
      "score": 0.91,
      "box": {
        "x1": 100.0,
        "y1": 120.0,
        "x2": 260.0,
        "y2": 220.0
      }
    }
  ]
}
```

错误返回：

- `400`：表单解析失败、没有上传图片、图片解码失败，或在 `use_letterbox: false` 时图片尺寸不匹配。
- `500`：模型推理或服务端内部异常。

#### 视频抽帧强检测 + 光流弱跟踪

接口：

```text
POST /infer_video
Content-Type: multipart/form-data
form field: video
```

示例：

```bash
curl -X POST http://127.0.0.1:8080/infer_video \
  -F "video=@/path/to/video.mp4"
```

当前视频处理不是简单逐帧 YOLO，而是采用“异步强检测 + 弱跟踪 + 动态抽帧 + 兜底插值”：

```text
C++ OpenCV 读取原始视频每一帧
        ↓
按 source_fps / video_detect_fps 计算 base_frame_stride
        ↓
根据光流质量、轨迹变化和 base_frame_stride 动态决定是否调度 ONNX 强检测
        ↓
非强检测帧：LK 光流弱跟踪上一帧 track 框，并用 ByteTrack updateTracked 续轨
        ↓
异步 ONNX 返回后：根据帧间运动补偿检测框，再用 ByteTrack update 校正轨迹
        ↓
光流失败帧：检测关键帧之间 bbox 插值兜底
        ↓
生成 VideoInferResult，最终由 response_json.cpp 序列化为 JSON
```

例如源视频为 24 FPS，`video_detect_fps: 4.0` 时，基础间隔为 6 帧。稳定场景会接近每 6 帧调度一次 YOLO；复杂运动、光流质量下降或轨迹突变时会临时缩短间隔，尽快用 ONNX 校正。这样可以保持最终展示仍为原始 24 FPS，同时避免每帧都跑 ONNX。

视频主流程在 `video/video_inference.cpp`，LK 光流弱跟踪细节在 `video/optical_flow_tracker.cpp`：

- `inferVideoFile` 是视频推理入口，返回结构体 `VideoInferResult`。
- 每帧先调用 `weakTrackWithOpticalFlow(previous_gray, current_gray, previous_frame_tracks, ...)` 尝试快速续轨。
- `weakTrackWithOpticalFlow` 在上一帧每个 track 框内用 `cv::goodFeaturesToTrack` 选角点，再用 `cv::calcOpticalFlowPyrLK` 追踪到当前帧。
- 对每个 track 的角点位移取中位数，平移 bbox，生成带原 `track_id` 的弱跟踪框。
- `ByteTracker::updateTracked` 按 `track_id` 更新已有轨迹状态，不创建新 ID。
- `shouldRunScheduledDetection` 和光流质量评估决定是否调用 `launchAsyncInfer` 调度强检测。
- 异步强检测完成后，`motionCompensatedDetections` 将检测框补偿到当前帧，再调用 `ByteTracker::update` 校正轨迹。

返回示例：

```json
{
  "code": 0,
  "message": "success",
  "tracking_status": "active",
  "fps": 24.0,
  "source_fps": 24.0,
  "target_detect_fps": 4.0,
  "effective_detect_fps": 4.0,
  "frame_stride": 6,
  "stride_mode": "async_dynamic",
  "onnx_async": true,
  "base_frame_stride": 6,
  "min_frame_stride_used": 2,
  "max_frame_stride_used": 8,
  "final_frame_stride": 6,
  "width": 1280,
  "height": 720,
  "frame_count": 962,
  "source_frame_count": 962,
  "processed_frame_count": 161,
  "display_frame_count": 962,
  "detected_frame_count": 161,
  "async_infer_request_count": 161,
  "async_correction_count": 161,
  "async_corrected_frame_count": 161,
  "forced_detection_count": 12,
  "scheduled_detection_count": 149,
  "skipped_detection_count": 0,
  "weak_tracked_frame_count": 801,
  "interpolated_frame_count": 0,
  "empty_frame_count": 0,
  "output_shapes": [[1, 300, 6]],
  "frames": [
    {
      "frame_index": 0,
      "timestamp_ms": 0.0,
      "is_detection_frame": true,
      "tracks_source": "async_corrected",
      "tracks": [
        {
          "track_id": 1,
          "class_id": 2,
          "class_name": "car",
          "score": 0.91,
          "box": {
            "x1": 100.0,
            "y1": 120.0,
            "x2": 260.0,
            "y2": 220.0
          }
        }
      ]
    },
    {
      "frame_index": 1,
      "timestamp_ms": 41.67,
      "is_detection_frame": false,
      "tracks_source": "weak_tracked",
      "tracks": []
    }
  ]
}
```

`tracks_source` 取值说明：

- `async_corrected`：该帧已用异步 ONNX 结果校正。
- `detected`：兼容保留的强检测来源标记；当前异步视频流程通常使用 `async_corrected`。
- `weak_tracked`：该帧没有跑 YOLO，使用 LK 光流弱跟踪更新框。
- `interpolated`：光流没有可用结果时，用相邻检测帧做 bbox 插值兜底。
- `empty`：没有检测、弱跟踪或插值结果。

错误返回：

- `400`：表单解析失败、没有上传视频、视频无法打开、没有可读帧、没有采样帧被处理，或在 `use_letterbox: false` 时视频帧尺寸不匹配。
- `500`：模型推理、临时视频文件写入或服务端内部异常。

### 全帧 ONNX 与 ONNX + 光流对比实验

对比实验入口为 `yolo_onnx_cpp/test/compare_full_onnx_vs_flow.py`。脚本会对同一个视频连续跑三次 `/infer_video`：

- `full_onnx`：临时配置 `video_detect_fps: 0`，每一帧都跑 ONNX，结果作为伪标签。
- `dynamic_onnx_flow`：临时配置 `video_detect_fps: --flow-detect-fps`、`video_stride_mode: dynamic`，强检测帧跑 ONNX，跳过帧使用 LK 光流弱跟踪，并允许运行时调整 stride。
- `fixed_onnx_flow`：临时配置 `video_detect_fps: --fixed-flow-detect-fps`、`video_stride_mode: fixed`，用固定 stride 跑 ONNX + 光流；未显式传入时复用 `--flow-detect-fps`。
- 对比方式：按帧、按类别做贪心 IoU 匹配，输出 precision、recall、F1、mean matched IoU、FP/FN，并按 `tracks_source` 和类别拆分统计。

示例命令：

```bash
python3 yolo_onnx_cpp/test/compare_full_onnx_vs_flow.py \
  --video yolo_onnx_cpp/test_outputs/video_tracking/00a0f008-3c67908e_fullfps24_20260608_003610/00a0f008-3c67908e_24fps_mjpeg.avi \
  --flow-detect-fps 4 \
  --iou-thresholds 0.3,0.5,0.7
```

注意：如果输入视频本身 FPS 不高，且 `source_fps <= --flow-detect-fps`，服务端会得到 `frame_stride=1`，此时 ONNX+光流实际不会跳帧，无法体现光流方案的性能收益。24 FPS 视频配 `--flow-detect-fps 4` 时会每 6 帧跑一次 ONNX，其余帧走光流弱跟踪。

脚本默认输出到 `yolo_onnx_cpp/test_outputs/video_compare/<video_stem>_full_dynamic_fixed_onnx_flow_<timestamp>/`，主要产物包括：

- `full_onnx_infer_video_response.json`：全帧 ONNX 原始响应。
- `full_onnx_pseudo_labels.jsonl`：由全帧 ONNX 生成的逐帧伪标签，便于后续复用。
- `dynamic_onnx_flow_infer_video_response.json`：动态 stride ONNX+光流原始响应。
- `fixed_onnx_flow_infer_video_response.json`：固定 stride ONNX+光流原始响应。
- `comparison.json`：机器可读的效果和性能指标。
- `comparison.md`：便于阅读的实验摘要。
- `full_onnx_config.yaml` / `dynamic_onnx_flow_config.yaml` / `fixed_onnx_flow_config.yaml`：本次实验实际使用的临时配置。
- `full_onnx_detections.mp4` / `dynamic_onnx_flow_detections.mp4` / `fixed_onnx_flow_detections.mp4`：逐帧画框和 track id 的可视化视频；传入 `--no-save-videos` 时不会自动生成，但可用已保存响应补渲染。

`comparison.json` 中需要重点看：

- `runs.<run>.unique_track_count`：该 run 内出现过的唯一 track id 数量，便于观察 ID 碎片化是否下降。
- `performance_vs_full_onnx.<run>.elapsed_speedup_full_over_run`：全帧 ONNX 耗时 / 指定 ONNX+光流 run 耗时，大于 1 表示该 run 更快。
- `performance_vs_full_onnx.<run>.onnx_frame_reduction_ratio`：指定 run 少跑 ONNX 的帧比例。
- `performance_vs_full_onnx.<run>.full_onnx_display_fps` / `run_display_fps`：端到端展示帧吞吐，包含上传、服务端处理和 JSON 响应读取。
- `quality_vs_full_onnx_labels.<run>[].overall`：以全帧 ONNX 为伪标签的总体 precision、recall、F1 和 IoU。
- `quality_vs_full_onnx_labels.<run>[].by_flow_frame_source`：分别查看强检测帧、光流帧、插值帧的匹配质量。
- `quality_vs_full_onnx_labels.<run>[].by_class`：按类别查看误差来源。

这里的全帧 ONNX 只是伪标签，不等价于人工标注真值；该实验用于衡量“少跑 ONNX + 光流补帧”相对全帧 ONNX 的一致性和端到端性能收益。

#### 2026-06-15 ByteTrack 升级验证

使用 `yolo_onnx_cpp/test_outputs/video_inputs/02a46296-f95ec53f_full_mjpeg.avi` 重新跑 `compare_full_onnx_vs_flow.py --flow-detect-fps 4 --iou-thresholds 0.3,0.5,0.7 --no-save-videos`，输出目录为：

`yolo_onnx_cpp/test_outputs/video_compare/02a46296-f95ec53f_full_mjpeg_full_dynamic_fixed_onnx_flow_20260615_083955/`

本次推理阶段使用了 `--no-save-videos`，随后用已保存的 `*_infer_video_response.json` 补渲染了可视化视频，并回填到 `comparison.json` / `comparison.md`：

- `full_onnx_detections.mp4`
- `dynamic_onnx_flow_detections.mp4`
- `fixed_onnx_flow_detections.mp4`

与 `20260608_121415` 的旧结果相比，唯一 track id 数量明显下降：

| run | 旧 unique track ids | 新 unique track ids | 变化 |
| --- | ---: | ---: | ---: |
| `full_onnx` | 774 | 395 | -379 (-48.97%) |
| `dynamic_onnx_flow` | 517 | 286 | -231 (-44.68%) |
| `fixed_onnx_flow` | 420 | 272 | -148 (-35.24%) |

这说明完整 ByteTrack 匹配逻辑后，ID 碎片化有明显改善。需要注意的是，质量指标仍以当前 `full_onnx` 作为伪标签，因此跨版本比较 precision/recall 时要同时看伪标签本身是否变化。

### 代码流程

#### image / OpenCV

- `preprocessImageContent`：读取上传图片内容，按配置决定是否 letterbox，并生成 `TensorInput`。
- `preprocessImageMat`：处理已读取的 `cv::Mat`，供图片解码后和视频逐帧推理共用。
- `preprocessImage`：执行 `BGR -> RGB`、归一化到 `[0, 1]`、转 NCHW float tensor。
- `decode`：解析 ONNX 输出。当前优先匹配 `output0(1, 300, 6)`，并按 `[x1, y1, x2, y2, score, class_id]` 转成 `Detection`；同时保留旧 YOLO 原始输出的兼容解析。

#### ONNX Runtime

- `YoloEngine` 初始化时根据配置加载 `model_path`。
- `infer` 执行 ONNX Runtime 推理，记录 `output_shapes`，并将输出解码成检测框。
- `conf_threshold` 和 `iou_threshold` 均来自配置文件。

#### Drogon / response_json

- `runApiServer` 注册 `/infer` 和 `/infer_video` 接口并启动 HTTP 服务。
- `/infer` 接收图片文件，`/infer_video` 接收视频文件；接口接收的是上传文件内容，不是本地路径字符串。
- `api_server.cpp` 不承载视频推理细节，只负责请求解析、临时文件管理、调用 `inferVideoFile` 和返回响应。
- `response_json.cpp` 负责最终序列化：`InferResult` 转 `/infer` 响应，`VideoInferResult` 转 `/infer_video` 响应。
- 响应中的 `detections` 包含 `class_id`、可选 `class_name`、`score` 和 `box`。
- 视频响应中的 `frames[].tracks` 包含 `track_id`、`class_id`、可选 `class_name`、`score` 和 `box`。
- 视频响应中的 `frames[].is_detection_frame` 和 `frames[].tracks_source` 用于区分强检测帧、光流弱跟踪帧和插值兜底帧。

#### video / 内部结构体

- `VideoFrameTracks` 保存单帧视频结果：`frame_index`、`timestamp_ms`、检测帧标记、校正延迟、`tracks_source` 和 `std::vector<TrackedDetection>`。
- `VideoInferResult` 保存视频响应所需的全部统计信息、`output_shapes` 和逐帧 `VideoFrameTracks`。
- 视频推理过程中不构造逐帧 JSON；`frames` 和 `tracks` 都是结构体容器，直到 HTTP 响应阶段才由 `response_json.cpp` 转 JSON。

#### tracking / ByteTrack 与光流弱跟踪

- `ByteTracker::update` 维护视频请求内的强检测轨迹状态，当前已升级为完整 ByteTrack 风格流程：Kalman 状态为 `[cx, cy, a, h, vx, vy, va, vh]`，每帧先预测 `Tracked/Lost` 轨迹，再把检测按高低置信度分段匹配。
- 第一阶段用 Hungarian 匹配 `Tracked + Lost` 轨迹和高置信检测，并融合检测分数；第二阶段只用仍未匹配的 active 轨迹匹配低置信检测。
- 未确认轨迹会单独和剩余高置信检测匹配；仍未匹配的高置信检测才创建新 `track_id`。
- 轨迹生命周期包含 `Tracked`、`Lost`、`Removed`，并通过 `track_buffer` 保留短暂丢失轨迹；同类高 IoU 重复轨迹会按轨迹寿命移除较短的一条。
- 匹配仍保留类别约束，避免不同类别之间抢占同一个 ID。
- `ByteTracker::updateTracked` 接收已经带有 `track_id` 的弱跟踪框，只更新已有轨迹的 Kalman 状态和生命周期，不创建新轨迹。它用于跳过帧的光流弱跟踪结果。
- `weakTrackWithOpticalFlow` 是轻量弱检查逻辑：在上一帧 track 框内选角点，使用 LK 光流追踪到当前帧，对每个目标取角点位移中位数并平移 bbox。
- 光流弱跟踪的作用是让跳过帧上的框跟随真实图像运动，减少纯线性插值带来的框漂移；强检测帧仍由 YOLO 定期校正。

### 注意事项

- `model/onnx.py` 当前导出参数里 `nms=False`，这种导出通常不是 `output0(1, 300, 6)`。如果部署使用 `[1,300,6]` 输出的模型，需要确认导出的 ONNX 已包含 NMS 或经过后处理导出。
- `model_path` 指向的模型文件需要存在，例如 `yolo_onnx_cpp/deploy/best.onnx`。
- 如果 `num_classes` 和 `class_names` 同时配置，两者数量必须一致。
- `/infer_video` 当前会把原始展示帧都放进一次 JSON 响应，即使 ONNX 只处理抽帧点，长视频响应体仍会较大；真实生产服务建议增加帧数上限、分页或异步任务输出。
- `.mov` / `.qt` 视频会优先尝试 OpenCV FFmpeg 后端；如果仍无法打开，先确认 vcpkg 中安装的是 `opencv4[ffmpeg]`，或将输入转码为当前 OpenCV 后端可读的 mp4/avi。
- LK 光流弱跟踪适合短间隔跳过帧。若 `video_detect_fps` 过低、目标快速形变、严重遮挡或相机剧烈运动，仍可能出现漂移；此时应提高强检测帧率或只对 `car/truck/bus/person/rider/motor/bike` 等动态类别做跟踪。
