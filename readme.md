# 把一段交通视频摊开来看：VisionTrack 的架构与实现

道路视频检测真正困难的部分，不是让 YOLO 在一张图片上画出框，而是让一段视频在有限 CPU 资源上持续产生可信、可解释、可部署的结果。

每帧都运行高分辨率模型，精度基线清楚，但吞吐和延迟很快成为瓶颈；简单抽帧，车辆、行人和交通灯的轨迹又会在检测帧之间断开。异步推理还会带来另一个问题：模型返回的是过去某一帧的结果，系统必须把它校正到当前时间线，才能避免轨迹回跳。

VisionTrack 围绕这个工程矛盾展开：

- 用 YOLO26 完成图片和视频强检测；
- 用动态 stride 控制模型调用频率；
- 在跳过的帧上使用 LK 光流和 ByteTrack 续轨；
- 用高低分辨率模型协同处理速度与精度的取舍；
- 将训练环境与 C++ 部署环境分开；
- 通过 Drogon HTTP 接口、Web 工作台和 Qt 客户端交付结果。

项目当前面向 BDD100K 道路场景，识别人、车辆、交通灯和交通标志等 10 类目标。

## 系统边界

开发环境中，浏览器首先访问 Vite。Vite 只代理推理请求，真正的 Gateway、API 和视频 worker 都位于同一个 **yolo_api** 进程中。curl 或其它客户端可以绕过 Vite，直接访问 Drogon 的 8080 端口。

~~~mermaid
flowchart LR
    Browser[Browser / React workbench]
    Client[curl / other client]
    Vite[Vite dev server<br/>UI and reverse proxy]

    subgraph Service[yolo_api process]
        Gateway[ApiGateway<br/>routes and limits]
        ImageAPI[Image handler]
        VideoAPI[Video handler]
        Temp[Temporary video]
        CV[OpenCV preprocess]
        Scheduler[Dynamic scheduler]
        Worker[AsyncInferWorker<br/>in-process thread]
        Tracking[LK flow and trackers]
        Engine[YoloEngine<br/>ORT or OpenVINO]
        JSON[response_json]
    end

    Models[(Models, classes and config)]

    Browser --> Vite --> Gateway
    Client --> Gateway
    Gateway --> ImageAPI --> CV --> Engine
    Gateway --> VideoAPI --> Temp --> Scheduler
    Scheduler --> Worker --> Engine
    Scheduler --> Tracking
    Models --> Engine
    Engine --> JSON
    Tracking --> JSON
    JSON --> Gateway --> Browser
~~~

这里没有数据库、Redis、消息队列或向量库：

- 模型、类别文件和 YAML 配置是磁盘上的持久化输入；
- 上传视频会写入系统临时目录，请求结束后删除；
- 帧、轨迹、异步队列、动态 stride 和指标都是单次请求内的内存状态；
- HTTP 响应返回后，服务端不保存任务或检测结果；
- **yolo_onnx_cpp/test_outputs/** 保存本地生成的大型运行产物；纳入版本管理的历史验证快照集中在 **docs/test-results/**。它们都不是在线数据库。

因此，Gateway 是进程内 HTTP 边界，不是独立微服务；Worker 是内存工作线程，也不是可恢复的队列消费者。

## 关键架构取舍

### 训练与部署环境分开

训练、验证和 ONNX 导出使用 **yolo** Conda 环境，可以使用 PyTorch 和 CUDA。在线服务使用 CMake、GCC15 和 **/root/vcpkg** 中的 Drogon、OpenCV、ONNX Runtime 或 OpenVINO，不依赖 Python 运行时。

仓库中的 **rag-api** 环境不参与 VisionTrack 推理链路。

### 强检测与弱跟踪分工

模型强检测负责重新确认类别和几何位置；光流只负责短间隔传播已有框；ByteTrack 负责轨迹 ID 和生命周期。稳定场景不必每帧调用模型，光流质量下降、尺度或速度突变时则提前触发强检测。

光流不是检测器，它只承担检测帧之间的时间连续性。

### 结构体先行，JSON 只出现在边界

推理过程使用 **InferResult**、**VideoFrameTracks** 和 **VideoInferResult** 等 C++ 结构体。逐帧循环不构造 JSON；请求完成后，**response_json.cpp** 才生成外部响应。

### 外部请求同步，内部模型调用异步

视频接口对调用方仍是同步 HTTP：客户端上传完整视频，等待处理完毕，再一次性接收 JSON。内部 **AsyncInferWorker** 可以并行执行强检测；旧帧结果返回时，系统先做运动补偿，再校正当前轨迹。

这提高了流水线利用率，但不等于异步任务 API。

## 真实数据流

### 图片

~~~text
multipart image
-> ApiGateway
-> ImageInferenceHandler
-> OpenCV decode and letterbox
-> NCHW float tensor
-> YoloEngine
-> InferResult
-> response_json
-> HTTP JSON
~~~

图片请求不会创建后台任务。响应包含检测框、输出 shape、阶段耗时、CPU 利用率和 RSS 内存。

### 普通视频

~~~text
multipart video
-> temporary file
-> OpenCV VideoCapture
-> calculate base stride
-> LK optical flow on readable frames
-> schedule model detection when due or quality degrades
-> compensate completed async detections
-> update ByteTrack
-> interpolate only when no tracking result is usable
-> collect VideoInferResult
-> serialize one HTTP response
-> delete temporary file
~~~

以 24 FPS 视频和目标检测帧率 4 FPS 为例，基础 stride 是 6。稳定阶段大约每 6 帧调度一次模型；复杂运动或光流质量下降时，动态策略会缩短间隔。

每帧通过 **tracks_source** 说明来源：

- **async_corrected**：异步模型结果已经过时间补偿；
- **detected**：同步或兼容路径的强检测结果；
- **weak_tracked**：使用 LK 光流传播；
- **interpolated**：相邻检测帧之间的插值兜底；
- **empty**：没有可用结果。

### 高低分辨率协同

**/infer_video_high_low** 使用高分辨率模型维护权威状态，低分辨率模型承担更频繁的轻量刷新。AuthorityTracker 在 high-res、low-res、flow 及异步 replay 后更新 stable、provisional、去重和生命周期状态。

最终三场景回归中，pooled precision 从 0.5048 提高到 0.7249，F1 从 0.6384 提高到 0.7726，FP 减少约 63%。完整记录见 [优化计划](docs/reports/优化计划.md) 和 [视频算法对比报告](docs/reports/video_algorithm_comparison_20260710.md)。

预测视频：[dynamic_onnx_flow_detections.mp4](Readme/dynamic_onnx_flow_detections.mp4)。

## 当前工作台

![VisionTrack workbench overview](docs/assets/visiontrack-workbench-overview.png)

截图由当前 **front/** 代码正式构建后，在 1440 × 900 视口生成，使用内置 BDD100K Demo replay。它展示的是真实界面状态，但不是一次真实 Drogon 响应，因此明确标记了 Demo、Not checked 和 not reported。

界面可以观察：

- endpoint、multipart 字段和请求状态；
- 强检测、跳帧、光流、动态 stride、ByteTrack 和 JSON 节点；
- 当前帧目标框、类别、置信度与结果来源；
- 轨迹生命周期和可见状态；
- 模型、光流、跟踪及端到端指标；
- 当前响应未提供的字段，而不是前端生成的虚构数据。

前端当前调用 **/infer** 和 **/infer_video**。High/Low endpoint 已由后端提供，但尚未加入工作台模式选择。

## 状态、可观测性与失败语义

项目没有 SSE 或 WebSocket，也没有 event ID、Last-Event-ID 和断线续传：

- 连接断开后不能从某一帧继续；
- 客户端重试会创建新的完整推理请求；
- 服务没有幂等键，不能复用上次内存状态；
- HTTP 2xx 且 JSON 中 code 为 0 表示成功；
- 上传、媒体或参数错误返回 HTTP 400；
- 模型和内部异常返回 HTTP 500；
- 没有 done 事件，HTTP 响应结束就是终止边界。

服务日志写到标准输出和标准错误，包含 route、文件名、字节数、耗时、检测数、队列长度、丢帧数、CPU 和 RSS。响应中的 timing、metrics、帧计数与 high_low_diagnostics 用于请求级诊断。

当前没有 **/health**。下面只能确认 Gateway 可达，不能证明真实模型推理成功：

~~~bash
ss -ltn | grep ':8080'
curl -i -X POST http://127.0.0.1:8080/infer
~~~

第二条命令应返回 HTTP 400 和 multipart 解析错误。

## 最短运行路径

以下路径假设 **/root/vcpkg** 已按仓库约束准备好。项目没有 .env、数据库初始化或一键整栈脚本。

### 1. 确认本地模型

项目不会自动下载权重：

~~~bash
ls -lh yolo_onnx_cpp/deploy/best.onnx yolo_onnx_cpp/deploy/best_640x384.onnx yolo_onnx_cpp/deploy/classes.json
~~~

### 2. 构建并启动后端

~~~bash
cd yolo_onnx_cpp
cmake --preset vcpkg-gcc15-release
cmake --build --preset vcpkg-gcc15-release
../build/yolo_api config.yaml
~~~

服务监听 0.0.0.0:8080，日志直接输出到终端。项目不创建 PID 文件或固定日志目录；使用 Ctrl-C 停止。

### 3. 安装并启动前端

在另一个终端运行：

~~~bash
cd front
npm ci
npm run dev -- --host 0.0.0.0
~~~

Vite 默认代理图片和视频接口到 127.0.0.1:8080。连接其它后端时使用：

~~~bash
VITE_API_BASE_URL=http://127.0.0.1:8080 npm run dev -- --host 0.0.0.0
~~~

前后端分别运行在两个前台终端，可以独立使用 Ctrl-C 重启或停止。当前没有统一 PID、日志和服务编排脚本。

### 4. 直接验证 API

~~~bash
curl -X POST http://127.0.0.1:8080/infer -F "image=@/path/to/image.jpg"

curl -X POST "http://127.0.0.1:8080/infer_video?include_frames=false" -F "video=@/path/to/video.mp4"
~~~

在线请求不写数据库；视频只会短暂写入系统临时目录。训练、导出和对比脚本会写入 runs、deploy 或显式输出目录，执行前应单独确认。

## 代码分层

~~~text
front/                         React 工作台、HTTP client 和 Demo replay
model/                         YOLO26 训练、BDD100K 工具和 ONNX 导出
yolo_onnx_cpp/
  main.cpp                     配置、模型加载和启动
  config/                      YAML 配置解析
  drogon/api_contract.h        路由与上传字段契约
  drogon/api_gateway.*         路由注册、监听和请求限制
  drogon/inference_handlers.*  图片和视频 HTTP 编排
  drogon/request_utils.*       查询参数和临时文件
  drogon/response_json.*       外部 JSON schema
  image/                       解码、letterbox、tensor 和输出解析
  model/                       ONNX Runtime / OpenVINO 推理
  video/                       调度、worker、光流和 High/Low
  tracking/                    ByteTrack
  metrics/                     延迟、CPU、内存和队列指标
  test_cpp/                    C++ 回归测试
qt_ui/                         不经过 HTTP 的本地 Qt 图片客户端
docs/                          文档索引、正式报告、测试快照和截图资源
~~~

推荐阅读顺序：**main.cpp → api_gateway.cpp → inference_handlers.cpp → video_inference.cpp → video_inference_detail.cpp → optical_flow_tracker.cpp / byte_tracker.cpp → yolo_engine.cpp**。

## 常用 API

| Method | Path | 上传字段 | 用途 |
| --- | --- | --- | --- |
| POST | /infer | image | 单张图片检测 |
| POST | /infer_video | video | 动态 stride、异步强检测、光流和 ByteTrack |
| POST | /infer_video_high_low | video | 高低分辨率协同与 AuthorityTracker |

视频接口支持：

- **include_frames=false**：只返回摘要和指标；
- **frame_offset=N**：从第 N 个结果帧开始；
- **frame_limit=N**：最多返回 N 帧，必须为正整数。

这些参数只裁剪响应序列化范围，不会减少已经完成的视频推理工作。完整字段以 [response_json.cpp](yolo_onnx_cpp/drogon/response_json.cpp) 和 [Gateway 契约测试](yolo_onnx_cpp/test_cpp/api_gateway_contract_test.cpp) 为准。

## 模型训练与导出

训练环境规格位于 **envs/yolo.yml**：

~~~bash
conda env create -f envs/yolo.yml
conda run -n yolo python -m pip check
~~~

当前 **model/train.py** 会把物理 GPU 4、5 映射为进程内的逻辑设备 0、1：

~~~bash
conda run -n yolo python model/train.py
~~~

导出不会由服务自动触发：

~~~bash
conda run -n yolo python model/onnx.py --pt model/runs/detect/bdd100k_yolo26s_det_1280x736/weights/best.pt --data model/data/bdd100k_yolo_det/data.yaml
~~~

当前 YOLO26 ONNX 输出为 **output0(1, 300, 6)**，每行按 **x1, y1, x2, y2, score, class_id** 解析。解码器同时保留旧 YOLO 原始候选输出的兼容路径。

## 验证

~~~bash
cd yolo_onnx_cpp
cmake --preset vcpkg-gcc15-release
cmake --build --preset vcpkg-gcc15-release
ctest --preset vcpkg-gcc15-release

cd ../front
npm run build
~~~

C++ 测试覆盖配置、图片处理、AuthorityTracker、视频对比、响应 JSON、Gateway 契约和 OpenVINO Python smoke test。

历史报告和已纳入版本管理的验证结果统一从 [docs 文档索引](docs/README.md) 查阅。新的测试运行仍写入 **yolo_onnx_cpp/test_outputs/**，不会覆盖文档快照。

环境检查：

~~~bash
conda run -n yolo python -m pip check
conda run -n rag-api python -m pip check
bash qt_ui/check_env.sh
~~~

Qt 检查失败时应阅读输出；不要改用系统 Qt，也不要在 qt_ui 下生成独立 vcpkg_installed。

## 当前限制与开放问题

1. **视频 HTTP 请求仍是同步的。** 内部 worker 异步不改变外部等待方式。
2. **默认响应可能很大。** 摘要模式减少响应大小，但不减少推理工作。
3. **没有任务持久化和续传。** 进程退出、断线或重试都不能恢复任务。
4. **没有独立健康检查。** 端口可达不等于模型 ready。
5. **前端尚未选择 High/Low endpoint。**
6. **YOLO26 端到端输出后仍执行兼容 NMS。** 需要用拥挤交通场景 A/B 回归后再决定是否跳过。
7. **两个性能目标尚未达到。** weak-tracked mean IoU 为 0.8614，目标 0.88；pooled FPS 为 24.71，目标 28。
8. **全帧模型结果只是伪标签。** 它不能替代人工标注评估。
下一步最有价值的演进不是继续增加字段，而是把长视频改为可查询的异步任务，明确结果持久化边界，再决定是否增加 SSE 进度流、任务恢复和结果分页存储。
