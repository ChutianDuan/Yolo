# 初始化到多 IP 任务提交完整流程

> 编写日期：2026-09-09。依据当前工作区源码，包含已有未提交改动，并非只依据 Git HEAD。本文只整理实现，没有修改服务、配置或依赖，也没有启动摄像头或压力测试。代码行号是编写时快照，后续修改后请结合函数名定位。

本文围绕一个问题展开：**服务启动时创建了什么，多个客户端提交的工作如何进入系统，多路摄像头的每一帧又如何共享模型并返回自己的结果？**

先记住三条调用链：

- 图片：HTTP 请求 → 请求门禁 → 图片预处理 → 共享模型 → 当前 HTTP 响应。
- 上传视频：HTTP 请求 → 请求门禁 → 临时文件 → 整段视频任务池 → 视频算法 → 共享推理调度器 → 模型 → 原 HTTP 响应。
- 实时流：创建请求 → 请求门禁 → 注册流并启动两条线程 → 解码帧队列 → 本路处理器 → 共享推理调度器 → 模型 → 本路跟踪结果 → SSE；另用 HTTP 查询状态、删除流。

## 阅读目录

1. [整体架构与身份概念](#architecture)
2. [启动前配置与运行入口](#configuration)
3. [关键初始化顺序、共享对象与线程](#initialization)
4. [一次图片或上传视频任务](#upload)
5. [多个客户端 IP 同时提交](#client-ips)
6. [多个摄像头 IP 创建实时任务](#camera-ips)
7. [跟随一帧进入模型并返回](#one-frame)
8. [结果订阅、异常和任务结束](#lifecycle)
9. [完整操作示例与排查](#examples)
10. [源码阅读索引和验证说明](#verification)

建议先读第 1、3 节建立对象关系；重点理解提交过程时读第 4、5 节；重点理解多路推理时连续读第 6～8 节。第 9 节命令供后续手动执行，不代表本文已经执行过。

所有标为“原始代码节选”的代码块直接摘自所链接源码；仅保留指定行段，外围代码未展示。带“示意”的图、算例和响应是解释材料，不是运行记录。

<a id="architecture"></a>
## 1. 整体架构与身份概念

### 1.1 所有请求先到同一个服务进程

```mermaid
flowchart TD
    A[客户端 A / 客户端 B] --> D[Drogon：监听 0.0.0.0:8080]
    D --> G[RequestGate：鉴权与按来源 IP 限流]
    G --> I[POST /infer：图片 Handler]
    G --> V[POST /infer_video 或 /infer_video_high_low]
    G --> S[POST /streams：创建实时流]
    I --> H[共享高模型 YoloEngine]
    V --> T[临时文件与 VideoJobExecutor]
    T --> O[每个上传任务的视频算法与跟踪状态]
    S --> M[RealtimeStreamManager]
    M --> C[每路：decodeLoop → 帧队列 → processLoop]
    CAM[摄像头 IP：视频源] --> C
    C --> P[本路 StreamProcessor]
    O --> SH[共享高模型 InferenceScheduler]
    O --> SL[共享低模型 InferenceScheduler]
    P --> SH
    P --> SL
    SH --> H
    SL --> L[共享低模型 YoloEngine：可选]
    H --> R[结果经本次调用 / future 返回调用方]
    L --> R
    R --> HR[图片或上传视频：原 HTTP 响应]
    R --> SR[实时流：本路跟踪、状态与 SSE]
```

图中的结果节点是概念汇合点，代码中没有一个按 IP 分发模型结果的中央路由器。上传任务持有自己的 HTTP 回调，调度任务持有自己的 promise，实时流持有自己的上下文及订阅者。

`0.0.0.0` 是服务绑定所有 IPv4 本地接口的监听地址。客户端访问的是服务器可达的实际地址，例如 `http://192.168.10.10:8080`。当前端口在 `main` 中固定为 8080，没有读取端口配置项。

### 1.2 不要把四种地址和三种编号混在一起

| 名称 | 示例（示意值） | 代码用途 |
| --- | --- | --- |
| 客户端 IP | `192.168.10.21`、`192.168.10.22` | `peerAddr().toIp()` 取得 TCP 对端 IP，作为请求令牌桶的键 |
| 服务监听地址 | `0.0.0.0:8080` | Drogon 接收 HTTP/HTTPS 请求 |
| 客户端访问的服务地址 | `192.168.10.10:8080` | 请求要发往的服务器，不是任务身份 |
| 摄像头源地址 `source` | `rtsp://192.168.20.31:554/stream1` | 服务端 `VideoCapture` 主动打开和读取的视频来源 |
| `stream_id` | `cam-east`、`client-a-run-001` | 实时流注册键、模型调度队列键；不自动等于 IP |
| `frame_index` | `0`、`1`、`2` | 本路/本次上传中的源帧编号，实时源丢帧后输出编号可以跳跃 |
| `track_id` | `1`、`2` | 本路跟踪器生成的轨迹身份，不是全局唯一编号 |

一个客户端可创建多个摄像头任务；多个客户端也可以操作同一个已知 `stream_id`。当前门禁使用一个配置的共享 Bearer token，没有按客户端 IP 绑定流所有权或提供租户隔离。图中的“独立”主要指每路运行状态，而非访问权限。

两个摄像头可以分别产生 `track_id=1`。合并业务结果时要连同 `stream_id` 使用；如果删除后重建同名流，调用方还需区分前后两次运行，现有接口没有额外的运行代次字段。

<a id="configuration"></a>
## 2. 启动前配置与运行入口

### 2.1 构建与启动命令

命令在仓库根目录开始执行。C++ 依赖沿用 `/root/vcpkg`、`x64-linux-gcc15` triplet 和项目 GCC15 toolchain，见 [CMakePresets.json](../../yolo_onnx_cpp/CMakePresets.json)。不要另外安装系统 C++ 库或在项目内生成一套 vcpkg 依赖树。

**ONNX Runtime 路径：**

```bash
cd yolo_onnx_cpp
cmake --preset vcpkg-gcc15-release
cmake --build --preset vcpkg-gcc15-release
../build/yolo_api ./config.yaml
```

当前 `config.yaml` 配置 `model_backend: auto`，模型是 `.onnx`，所以选择 ONNX Runtime。

**OpenVINO 多路路径：另开一个环境或先结束上一服务，不能同时占用 8080。**

```bash
cd yolo_onnx_cpp
cmake --preset vcpkg-gcc15-openvino
cmake --build --preset vcpkg-gcc15-openvino
../build-openvino/yolo_api ./config.multistream.yaml
```

OpenVINO preset 继承同一套 vcpkg/GCC15 环境，并打开 `YOLO_ENABLE_OPENVINO=ON`。构建要求当前 vcpkg 环境能找到 OpenVINO Runtime；本文不安装依赖。多路模板显式选择 `openvino`，可直接读取其中配置的 ONNX 模型，不要求为了本流程先转成 `.xml`。

两套配置都引用既有 `deploy/best.onnx` 和 `deploy/best_640x384.onnx`。使用前应保证这些文件可用，静态输入形状与配置一致；本文不下载或覆盖模型。服务若因运行库搜索路径启动失败，应使用项目现有的 vcpkg/GCC15 运行环境，不要切换到系统库。

### 2.2 配置是怎样确定的

**作用：** 先确定配置文件，再把文本配置转换成完整 `AppConfig`。

`main` 优先使用第一个命令行参数。没有参数时使用编译宏 `YOLO_DEFAULT_CONFIG_PATH`；正常 CMake 构建把它设为所选 profile 对应的配置路径。源码中的 `"config.yaml"` 只是宏没有定义时的后备值，不代表所有二进制始终从当前目录找配置。

`loadAppConfig` 的顺序是：配置路径相对当前工作目录转绝对路径 → 创建带默认值的 `AppConfig` → 按行解析标量和支持的列表 → `validateConfig` → 解析模型及 TLS 文件路径。这里是项目自带的简化配置解析器，不能把它当作支持所有 YAML 功能的通用解析器；`setScalar` 未匹配的标量键当前会被忽略，所以拼写错误未必报错。

**原始代码节选：** `loadAppConfig` 中高模型路径的解析。

来源：[yolo_onnx_cpp/config/app_config.cpp](../../yolo_onnx_cpp/config/app_config.cpp#L466-L472)，编写时第 466～472 行。

```cpp
    validateConfig(config);

    std::filesystem::path model_path(config.model_path);
    if (model_path.is_relative()) {
        model_path = path.parent_path() / model_path;
    }
    config.model_path = model_path.lexically_normal().string();
```

因此把 `./deploy/best.onnx` 写在 `yolo_onnx_cpp/config.yaml` 中时，模型相对的是**配置文件所在目录**。低模型和非空 TLS 路径也执行同类解析。`POST /streams` 的 `source` 是另一个入口，不经过此配置路径解析；相对本地视频路径由服务进程的工作目录解释。

校验涵盖：后端枚举、正数输入尺寸、阈值范围、低模型所需尺寸、线程和请求数非负约束、队列/任务线程正数约束、帧龄非负、类别数与类别列表匹配、环境变量名以及 TLS 证书/私钥成对配置。模型文件实际能否加载和输入能否推理，还需要模型初始化与实际调用验证。

**下一步：** `makeHighResAppConfig`、`makeLowResAppConfig` 派生各模型配置，然后构造模型。

### 2.3 线程、请求池和队列配置表

“源码默认”来自 [AppConfig](../../yolo_onnx_cpp/config/app_config.h#L8)；“多路模板”来自 [config.multistream.yaml](../../yolo_onnx_cpp/config.multistream.yaml)。这些数值是配置起点，不是吞吐或延迟的实测保证。

| 配置项 | 源码默认 | 多路模板 | 生效位置与含义 |
| --- | --- | --- | --- |
| `model_backend` | `auto` | `openvino` | `resolveBackend` 选择模型执行后端 |
| `openvino_device` | `CPU` | `CPU` | OpenVINO 编译设备 |
| `thread_num` | 4 | 8 | 老配置兼容值，供部分分域线程设置回退 |
| `server_io_threads` | 0 | 8 | 大于 0 用自身，否则回退 `thread_num`；Drogon I/O 线程 |
| `opencv_threads` | 0 | 4 | 大于 0 才调用 `cv::setNumThreads`；0 保留 OpenCV 当前设置，不回退 `thread_num` |
| `high_model_threads` | 0 | 8 | 大于 0 覆盖高模型的 `thread_num` |
| `low_model_threads` | 0 | 8 | 大于 0 用自身，否则继承派生后高模型线程数 |
| `infer_request_count` | 1 | 0 | 高模型；OpenVINO 为正数时建指定大小请求池，0 查询后端建议值；ORT 调度并发值至少为 1 |
| `low_res_infer_request_count` | 0 | 0 | 大于 0 覆盖低模型请求数；0 先继承 `infer_request_count` |
| `openvino_performance_mode` | `latency` | `throughput` | OpenVINO 编译提示，与请求池数量是不同设置 |
| `max_streams` | 4 | 4 | 实时流注册表容量；不限制上传视频数量 |
| `per_stream_queue_depth` | 2 | 2 | 同时用于每路已解码图像队列，以及每个模型调度器内每个流的等待任务队列；是两份不同队列 |
| `video_job_threads` | 2 | 2 | 执行整段上传视频的 worker 数，不是模型内部线程数 |
| `video_job_queue_depth` | 4 | 2 | 等待执行的整段视频任务数量，不含正在执行的任务 |
| `client_max_body_mb` | 256 | 256 | Drogon 请求体总量配置 |
| `client_max_memory_body_mb` | 16 | 16 | Drogon 内存请求体阈值；大请求还有框架临时存储，不等于视频任务池容量 |

低模型请求数的继承尤其值得单独看：高模型设置 4、低模型设置 0，低模型最终采用 4；只有继承后的值仍为 0，OpenVINO 才为低模型查询自己的建议请求数。多路模板两项都是 0，因此两级分别查询各自编译模型的建议值。

### 2.4 时效、检测节奏与接入配置表

| 配置项 | 源码默认 | 多路模板 | 生效位置与含义 |
| --- | --- | --- | --- |
| `max_request_age_ms` | 300 | 300 | 调度任务提交到开始执行前的年龄上限；0 禁用；上传视频也经过该调度器 |
| `max_result_age_ms` | 300 | 300 | 实时源本地解码完成到结果发布的年龄上限；0 禁用；本地文件不按此丢帧 |
| `max_stream_duration_seconds` | 0 | 0 | 实时流管理器生命周期检查；0 不自动到期，并非精确强制中断定时器 |
| `stream_max_consecutive_errors` | 5 | 5 | 连续模型/预处理等处理错误达到阈值后终止该流，不是摄像头连接失败重试次数 |
| `video_detect_fps` | 4.0 | 4.0 | 实时双模型的低模节奏；单模型的检测节奏；离线入口用于计算抽帧间隔 |
| `video_high_detect_fps` | 1.0 | 1.0 | 双模型周期高模刷新；0 关闭周期高模，首帧/紧急刷新仍可执行 |
| `video_stride_mode` | `dynamic` | `dynamic` | 离线上传视频的抽帧策略；实时 `processLoop` 使用 `DetectionCadence` |
| `video_model_async` | `true` | `true` | 离线视频异步检测开关；实时入口不读此开关 |
| `use_letterbox` | `true` | `true` | 按比例缩放并填充；否则直接缩放 |
| `api_bearer_token_env` | 空 | 空 | 非空时指定读取 token 的环境变量名，不是 token 本身 |
| `api_rate_limit_requests_per_second` | 0 | 0 | 每来源 IP 每秒补充的令牌数；0 关闭限流 |
| `api_rate_limit_burst` | 20 | 20 | 每 IP 令牌桶最大容量/初始令牌数 |
| `tls_certificate_path` / `tls_private_key_path` | 均空 | 均空 | 两项非空启用 Drogon TLS；模板默认 HTTP |

因此默认模板不会因为连续请求而触发 401/429：它没有启用 Bearer 鉴权或速率限制；队列满、流容量满仍会独立生效。

<a id="initialization"></a>
## 3. 关键初始化顺序、共享对象与线程

### 3.1 main：先完成模型加载，再开始监听

**作用：** 确保配置和模型初始化成功之后才向外提供 HTTP 服务。

**原始代码节选：** `main` 的完整启动主体，保留配置/模型异常处理与低模型初始化。

来源：[yolo_onnx_cpp/main.cpp](../../yolo_onnx_cpp/main.cpp#L17-L65)，编写时第 17～65 行。

```cpp
int main(int argc, char* argv[]) {
    const std::string config_path = argc > 1 ? argv[1] : YOLO_DEFAULT_CONFIG_PATH;
    constexpr uint16_t port = 8080;

    yolo::AppConfig config;
    try {
        config = yolo::loadAppConfig(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << config_path << '\n'
                  << e.what() << '\n';
        return 1;
    }

    if (config.opencv_threads > 0) {
        cv::setNumThreads(config.opencv_threads);
    }

    const yolo::AppConfig high_res_config = yolo::makeHighResAppConfig(config);
    std::shared_ptr<yolo::YoloEngine> engine;
    try {
        engine = std::make_shared<yolo::YoloEngine>(high_res_config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load model: " << high_res_config.model_path << '\n'
                  << e.what() << '\n';
        return 1;
    }

    std::shared_ptr<yolo::YoloEngine> low_res_engine;
    if (yolo::hasLowResModelConfig(config)) {
        const yolo::AppConfig low_res_config = yolo::makeLowResAppConfig(config);
        try {
            low_res_engine = std::make_shared<yolo::YoloEngine>(low_res_config);
        } catch (const std::exception& e) {
            std::cerr << "Failed to load low-res model: "
                      << low_res_config.model_path << '\n'
                      << e.what() << '\n';
            return 1;
        }
    }

    try {
        yolo::runApiServer(engine, low_res_engine, config, port);
    } catch (const std::exception& e) {
        std::cerr << "Failed to run API server:\n" << e.what() << '\n';
        return 1;
    }

    return 0;
}
```

后续高模型构造异常会打印错误并返回 1。接着检查 `hasLowResModelConfig(config)`，非空低模型路径触发低模型构造；低模型加载失败也直接结束启动，不会悄悄退化为单模型。

`makeHighResAppConfig` 复制原配置，只按需要覆盖高模型线程数。`makeLowResAppConfig` 复制原配置，再替换路径、输入尺寸、阈值、线程和请求数；末尾清理副本中的低模型嵌套字段，副本表示一台具体模型。类别阈值列表为空时继承规则、元素为 -1 时回退标量阈值的规则也在此链路生效。

**原始代码节选：** 低模型独立配置的派生部分，后续代码继续清理副本中的低模型嵌套字段。

来源：[yolo_onnx_cpp/config/app_config.cpp](../../yolo_onnx_cpp/config/app_config.cpp#L516-L536)，编写时第 516～536 行。

```cpp
AppConfig makeLowResAppConfig(const AppConfig& config) {
    AppConfig low_res = config;
    low_res.model_path = config.low_res_model_path;
    low_res.input_width = config.low_res_input_width;
    low_res.input_height = config.low_res_input_height;
    low_res.conf_threshold = config.low_res_conf_threshold >= 0.0F
        ? config.low_res_conf_threshold
        : config.conf_threshold;
    low_res.class_conf_thresholds = config.low_res_class_conf_thresholds.empty()
        ? config.class_conf_thresholds
        : config.low_res_class_conf_thresholds;
    low_res.iou_threshold = config.low_res_iou_threshold >= 0.0F
        ? config.low_res_iou_threshold
        : config.iou_threshold;
    low_res.thread_num = config.low_model_threads > 0
        ? config.low_model_threads
        : makeHighResAppConfig(config).thread_num;
    if (config.low_res_infer_request_count > 0) {
        low_res.infer_request_count = config.low_res_infer_request_count;
    }
    low_res.low_res_model_path.clear();
```

**下一步：** `runApiServer(engine, low_res_engine, config, port)` 创建 `ApiGateway` 并调用 `run`。

### 3.2 YoloEngine：每级加载一份模型，调用方共享

**后端选择。** `model_backend=onnx` 强制 ORT，`openvino` 强制 OpenVINO；`auto` 仅根据模型扩展名选择，`.xml` 用 OpenVINO，其他扩展名走 ORT。`auto` 不是硬件测速后自动择优，也没有加载失败后切换后端的逻辑。

模型构造还初始化类别信息、阈值和 CPU tensor 内存描述；类别数优先看配置，必要时读取模型同目录的 `classes.json`。类别阈值列表再次检查大小及值是否合法。

**ONNX Runtime 初始化。**

来源：[yolo_onnx_cpp/model/yolo_engine.cpp](../../yolo_onnx_cpp/model/yolo_engine.cpp#L256-L260)，编写时第 256～260 行。

```cpp
    void initOnnxRuntime(const AppConfig& config) {
        session_options_.SetIntraOpNumThreads(config.thread_num);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        session_ = Ort::Session(env_, config.model_path.c_str(), session_options_);
```

后续检查模型必须恰好一个输入、至少一个输出；把输入输出名称存入字符串容器，再创建指向这些字符串的名称指针，供以后 `session_.Run` 使用。当前初始化没有注册 CUDA Execution Provider，本文描述的是当前 CPU 路径。

ORT 一台引擎持有一个共享 `Ort::Session`，每次调用新建本次输入 tensor、输出 tensor 和 `InferResult`。`maxConcurrency()` 初值为 `max(infer_request_count, 1)`，用来给调度器确定 worker 数；它不是 ORT 引擎内的额外信号量，图片直接调用不会经过该调度限制。

**OpenVINO 初始化。** `initOpenVino` 先 `read_model`，设置模型线程数、`LATENCY/THROUGHPUT` 提示，正数请求数还写入 `num_requests` 提示，随后 `compile_model`。

**原始代码节选：** 编译之后确定并创建请求池。

来源：[yolo_onnx_cpp/model/yolo_engine.cpp](../../yolo_onnx_cpp/model/yolo_engine.cpp#L386-L397)，编写时第 386～397 行。

```cpp
        const size_t request_count = config.infer_request_count > 0
            ? static_cast<size_t>(config.infer_request_count)
            : static_cast<size_t>(
                ov_compiled_model_.get_property(ov::optimal_number_of_infer_requests)
            );
        ov_infer_requests_.reserve(std::max<size_t>(request_count, 1));
        max_concurrency_ = std::max<size_t>(request_count, 1);
        ov_available_requests_.reserve(std::max<size_t>(request_count, 1));
        for (size_t i = 0; i < std::max<size_t>(request_count, 1); ++i) {
            ov_infer_requests_.push_back(ov_compiled_model_.create_infer_request());
            ov_available_requests_.push_back(i);
        }
```

每个 `ov::InferRequest` 有自己的输入输出绑定；空闲索引保存在 `ov_available_requests_`。这里创建的是请求对象池，不是一张每摄像头一个模型的表。若构建未启用 OpenVINO 却选择该后端，代码抛出启动错误，不会自动改用 ORT。

**下一步：** 网关用 `engine->maxConcurrency()` 给对应调度器设置 worker 数；推理时如何借还请求，见第 7.5 节。

### 3.3 ApiGateway：把共享组件组装起来

**作用：** 所有路由使用同一组门禁、上传任务池和高低模型调度器。

**原始代码节选：** `ApiGateway` 的成员初始化列表。

来源：[yolo_onnx_cpp/drogon/api_gateway.cpp](../../yolo_onnx_cpp/drogon/api_gateway.cpp#L67-L103)，编写时第 67～103 行。

```cpp
ApiGateway::ApiGateway(
    std::shared_ptr<YoloEngine> engine,
    std::shared_ptr<YoloEngine> low_res_engine,
    AppConfig config
) : engine_(std::move(engine)),
    low_res_engine_(std::move(low_res_engine)),
    config_(std::move(config)),
    request_gate_(std::make_shared<RequestGate>(
        loadBearerToken(config_),
        static_cast<double>(config_.api_rate_limit_requests_per_second),
        static_cast<size_t>(config_.api_rate_limit_burst)
    )),
    video_job_executor_(std::make_shared<VideoJobExecutor>(
        static_cast<size_t>(config_.video_job_threads),
        static_cast<size_t>(config_.video_job_queue_depth)
    )),
    high_res_scheduler_(std::make_shared<InferenceScheduler>(
        engine_,
        engine_->maxConcurrency(),
        static_cast<size_t>(config_.per_stream_queue_depth),
        std::chrono::milliseconds(config_.max_request_age_ms)
    )),
    low_res_scheduler_(low_res_engine_ != nullptr
        ? std::make_shared<InferenceScheduler>(
            low_res_engine_,
            low_res_engine_->maxConcurrency(),
            static_cast<size_t>(config_.per_stream_queue_depth),
            std::chrono::milliseconds(config_.max_request_age_ms)
        )
        : nullptr),
    stream_manager_(std::make_shared<RealtimeStreamManager>(
        engine_,
        low_res_engine_,
        high_res_scheduler_,
        low_res_scheduler_,
        config_
    )) {}
```

这里发生的工作依次是：保存模型与配置 → 加载 token 并构造门禁 → 创建整段视频 worker → 创建高模调度 worker → 可选创建低模调度 worker → 构造实时流管理器。

`loadBearerToken` 在配置环境变量名非空时调用 `getenv`；变量缺失或内容为空会抛异常并终止服务启动。门禁不自动读取 `.env`，本文也不修改敏感文件。

流管理器构造时只保存共享资源和注册表；摄像头解码/处理线程要等 `POST /streams` 才创建。高低调度器和视频任务池则在构造时已经创建 worker，空闲时在条件变量上等待。

**下一步：** `run` 先注册门禁和路由，再设置监听参数并进入事件循环。

### 3.4 路由和网络服务初始化

**原始代码节选：** 监听与 I/O 线程设置。

来源：[yolo_onnx_cpp/drogon/api_gateway.cpp](../../yolo_onnx_cpp/drogon/api_gateway.cpp#L115-L126)，编写时第 115～126 行。

```cpp
    drogon::app()
        .addListener(
            "0.0.0.0",
            port,
            !config_.tls_certificate_path.empty(),
            config_.tls_certificate_path,
            config_.tls_private_key_path
        )
        .setThreadNum(serverIoThreadCount(config_))
        .setClientMaxBodySize(max_body_size)
        .setClientMaxMemoryBodySize(max_memory_body_size)
        .run();
```

在这之前，`registerRequestGate` 安装 pre-routing advice；`registerRoutes` 注册图片、两种上传视频和流管理/观测接口。上传视频 runner 是捕获共享模型、调度器和配置的 lambda，稍后由视频 worker 调用。

`setThreadNum` 控制 HTTP I/O，并不等于模型并发数量。实际线程还包括 OpenCV/模型内部线程、视频 worker、调度 worker以及每路摄像头的两个线程。

### 3.5 对象与线程归属表

| 对象 | 创建时刻与数量 | 线程/状态归属 | 生命周期与释放 |
| --- | --- | --- | --- |
| 高/低 `YoloEngine` | 启动时，高一份、低可选一份 | 共享后端、模型和请求池；本次结果独立 | `shared_ptr` 最后持有者释放时析构 |
| `RequestGate` | 网关构造，一份 | mutex 保护各客户端桶和统计，无专用线程 | 随最后持有者释放 |
| `VideoJobExecutor` | 网关构造，一份 | `video_job_threads` 个 worker 和一份待执行队列 | 析构停止接收，worker 继续排空已接收任务并 join |
| 高/低 `InferenceScheduler` | 网关构造，各模型一份 | 各自 worker、流队列、ready 队列和 in-flight 集合 | 析构取消等待任务、唤醒 worker，等待正在执行的调用结束并 join |
| `RealtimeStreamManager` | 网关构造，一份 | 全局流注册表及累计统计 | 析构逐流 `stopContext` |
| `StreamContext` | 每次成功创建实时流一份 | 独立 mutex、图像队列、状态、订阅者；一个 decoder 和一个 processor 线程 | DELETE join 两线程并移出注册表；自然终态不自动移除 |
| `StreamProcessor` | 每路 `processLoop` 局部创建；上传任务也各自创建 | 本路轨迹、高低权威跟踪器或 ByteTracker、上一帧灰度 | 所属处理函数结束时释放 |
| `AsyncInferWorker` | 离线上传启用异步时按任务创建 | 视频算法自身的异步线程与请求/结果缓冲 | 任务结束析构并 join；实时入口没有它 |
| `TempVideoFile` 和 HTTP callback | 每次上传视频 | 由该任务闭包持有，不依赖 Handler 栈上变量继续存活 | 闭包/最后引用释放后临时文件尝试删除；回调发送该请求结果 |

路由闭包也持有 `shared_ptr`，所以不能简单声称 `runApiServer` 的临时网关对象析构就立即释放所有共享资源。需要区分“对象已无引用后的析构行为”和 Drogon 注册回调仍持有对象的情况。

<a id="upload"></a>
## 4. 一次图片或上传视频任务

### 4.1 先确认 HTTP 接口契约

接口常量见 [api_contract.h](../../yolo_onnx_cpp/drogon/api_contract.h#L25)，入口实现见 [inference_handlers.cpp](../../yolo_onnx_cpp/drogon/inference_handlers.cpp)。

| 路径 | 请求 | 执行入口 | 正常响应 |
| --- | --- | --- | --- |
| `POST /infer` | multipart 文件字段 `image` | `ImageInferenceHandler::handle` | HTTP 200，检测框、输出形状、时延等 JSON |
| `POST /infer_video` | multipart 文件字段 `video` | `VideoInferenceHandler` → `inferVideoFile` | 处理完成后 HTTP 200，视频汇总及按选项输出的帧结果 |
| `POST /infer_video_high_low` | multipart 文件字段 `video` | 同一视频 Handler 类型 → `inferVideoFileHighLow` | 同上，并含高低模型相关诊断 |

当前查找上传文件时优先匹配约定字段，匹配不到会兼容性使用第一个文件；建议客户端仍使用正确字段，避免多文件请求时选错内容。它不是批量图片/视频接口。

两个视频接口支持查询参数 `include_frames`、`frame_offset`、`frame_limit` 和 `stream_id`。前三者控制最终 JSON 中的帧输出，不是视频处理范围；`frame_limit=10` 不等于只推理前 10 帧。省略参数时默认包含帧、偏移 0，内部限制值 0 表示不限制；显式提供 `frame_limit` 时必须为正数，`frame_limit=0` 反而返回 400。`include_frames=false` 时响应保留 `frames: []`。数值/布尔解析错误也返回 400。

### 4.2 图片：在当前 Handler 内完成推理

**作用：** 把上传图片转为模型输入，然后构造本次请求的检测结果。

`handle` 先解析 multipart，找文件，取 `fileContent()`，随后调用 `preprocessImageContent`。该函数解码图片，转入 `preprocessImageMat`。

**原始代码节选：** 预处理成功后的推理调用。

来源：[yolo_onnx_cpp/drogon/inference_handlers.cpp](../../yolo_onnx_cpp/drogon/inference_handlers.cpp#L173-L178)，编写时第 173～178 行。

```cpp
        InferResult result = engine_->infer(input.value());
        result.preprocess_ms = preprocess_ms;
        result.timing_samples.preprocess_ms.push_back(preprocess_ms);
        result.timing_samples.queue_wait_ms.push_back(0.0);
        result.end_to_end_ms = elapsedMs(request_start);
        result.timing_samples.end_to_end_ms.push_back(result.end_to_end_ms);
```

关键点是 `engine_->infer`：图片没有调用 `VideoJobExecutor` 或 `InferenceScheduler`。当前路由也没有为这段计算显式切换到后台池，因此 Handler 所在线程会等待预处理、模型执行和后处理完成。OpenVINO 请求池忙时，还可能在借用请求对象处等待。

接着 `inferResultToJson` 转成 JSON，调用本次 `callback`。成功体包含 `code: 0`、`message`、`detections`、`output_shapes`、`timing_ms` 等，结构见 [response_json.cpp](../../yolo_onnx_cpp/drogon/response_json.cpp)。解析/解码失败为 400，推理等异常为 500。

这里人为记录的 `queue_wait_ms=0` 表示没有调度器排队计时，不保证图片完全没有等待模型请求池。

**下一步：** HTTP 响应直接回到上传图片的客户端；它不会创建可用 `/streams/{id}` 查询的实时流。

### 4.3 视频：把整个处理过程封装成一个任务

**作用：** 让整段视频推理离开 HTTP Handler，把本次请求所需资源交给任务闭包。

Handler 先解析帧输出选项及 multipart，再生成调度身份。未指定查询参数 `stream_id` 时，使用进程内静态原子计数生成 `upload-1`、`upload-2` 等。

**原始代码节选：** 任务身份、临时文件和闭包捕获。

来源：[yolo_onnx_cpp/drogon/inference_handlers.cpp](../../yolo_onnx_cpp/drogon/inference_handlers.cpp#L281-L314)，编写时第 281～314 行。

```cpp
    static std::atomic<uint64_t> next_stream_id{1};
    std::string stream_id = request->getParameter("stream_id");
    if (stream_id.empty()) {
        stream_id = "upload-" + std::to_string(next_stream_id.fetch_add(1));
    }

    try {
        auto temp_video = std::make_shared<TempVideoFile>(content, extension);
        ResponseCallback job_callback = callback;
        auto job = [
            route = route_,
            file_name = file->getFileName(),
            content_size = content.size(),
            temp_video,
            stream_id,
            frame_json_options,
            runner = runner_,
            class_names = config_.class_names,
            job_callback = std::move(job_callback),
            request_start
        ]() mutable {
            runVideoJob(
                route,
                file_name,
                content_size,
                temp_video,
                stream_id,
                frame_json_options,
                runner,
                class_names,
                std::move(job_callback),
                request_start
            );
        };
```

闭包捕获 `temp_video`、`stream_id`、runner、类别名和自己的 HTTP callback，避免 Handler 返回之后继续依赖局部 parser 或文件内容视图。`TempVideoFile` 构造期间就将视频字节写到系统临时目录，文件名包含单调时钟值和原子计数；其正常析构路径尝试删除临时文件。

这意味着临时文件写入发生在任务准入检查之前，排队上限并不阻止已经到达 Handler 的上传内容先进行临时文件落盘。不要把“视频计算离开 I/O 线程”理解成“所有上传处理都已经后台化”。

**下一步：** `executor_->submit(std::move(job))`；拒绝时当前 Handler 返回 503，接受时稍后由 worker 调用 `runVideoJob`。

### 4.4 VideoJobExecutor：整段视频的有界 FIFO

**原始代码节选：** `submit` 中的准入与唤醒。

来源：[yolo_onnx_cpp/drogon/video_job_executor.cpp](../../yolo_onnx_cpp/drogon/video_job_executor.cpp#L38-L55)，编写时第 38～55 行。

```cpp
    bool submit(std::function<void()> job) {
        if (!job) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || jobs_.size() >= max_queue_depth_) {
                ++stats_.rejected_count;
                return false;
            }
            jobs_.push_back(std::move(job));
            ++stats_.submitted_count;
            stats_.queued_count = jobs_.size();
        }
        ready_.notify_one();
        return true;
    }

```

每个 worker 在条件变量上等待任务，取 `jobs_.front()`、弹出队首、增加 in-flight 计数，然后**释放队列锁再执行 `job()`**。耗时推理不会持有整个任务队列的 mutex。

多路模板是 2 个视频 worker、2 个等待名额。在两个 worker 已忙且另有两个任务排队的状态下，再提交一个视频任务会被拒绝。这个容量属于两种上传视频路由共用的任务池，不按客户端 IP 分池，也不为每个 IP 预留 worker。

上传任务仍可能在执行过程中等待模型调度；“已被视频 worker 取出”不代表立即获得模型。反过来，视频任务池满了也不直接阻止创建实时流，它们是不同的准入环节，但后续竞争同一组模型。

### 4.5 单模型与高低模型视频算法的区别

`ApiGateway::registerRoutes` 让 `/infer_video` 的 runner 调用 `inferVideoFile`，让 `/infer_video_high_low` 调用 `inferVideoFileHighLow`。

| 层级 | 单模型上传视频 | 高低模型上传视频 |
| --- | --- | --- |
| 初始化 | 打开临时视频，读 FPS/尺寸/帧数，计算基础 stride，创建 `StreamProcessor(false)` | 打开视频，派生低模配置，计算节奏，创建 `StreamProcessor(true)` 和 replay 状态 |
| 使用模型 | 高模型 | 高模型与低模型；低模型未配置时函数回退单模型入口 |
| 异步条件 | `video_model_async && base_frame_stride > 1` 才建 `AsyncInferWorker` | `video_model_async` 控制高模型异步 worker；低模型走同步调用路径 |
| 调度调用 | 单帧预处理后 `scheduler->infer(...)`，内部等价于 `submit(...).get()` | 高低分别进入对应共享调度器；同步路径可以传递 urgent 标记 |
| 跟踪与结果 | 检测帧更新 ByteTracker，间隔帧用光流，异步返回后校正相关帧结果 | 高模权威信息、低模几何/候选、光流传播；高模异步结果到达后通过 replay 更新轨迹和已积累帧结果 |
| 对外响应 | 算法完成后返回完整 `VideoInferResult` 给 runner | 同样在算法完成后一次性返回，并不是 SSE |

代码入口：[inferVideoFile](../../yolo_onnx_cpp/video/video_inference.cpp#L51)、[inferVideoFileHighLow](../../yolo_onnx_cpp/video/video_inference_high_low.cpp#L197)、[AsyncInferWorker](../../yolo_onnx_cpp/video/video_inference_detail.cpp#L401)。

离线异步 worker 还有自己的请求/结果缓冲，并在有 pending 工作时拒绝新提交；这是视频算法内部机制，不是 `video_job_queue_depth` 或实时图像队列。高低模型离线路径可能在等待高模结果期间继续低模处理，不能套用实时“单路一次只等一个模型”的结论。

两个上传入口复用的调度器仍有 `max_request_age_ms`。非 Completed 的调度结果会在 `InferenceScheduler::infer` 转为异常，交给视频算法的错误处理；不能把实时入口对 `Stale/Replaced` 的光流降级行为直接套用到上传视频。

### 4.6 为什么结果不会串到另一个客户端

**原始代码节选：** `runVideoJob` 完成后回到本次请求的 callback。

来源：[yolo_onnx_cpp/drogon/inference_handlers.cpp](../../yolo_onnx_cpp/drogon/inference_handlers.cpp#L81-L83)，编写时第 81～83 行。

```cpp
        callback(drogon::HttpResponse::newHttpJsonResponse(
            videoInferResultToJson(result, class_names, frame_json_options)
        ));
```

A 的闭包保存 A 的回调，B 的闭包保存 B 的回调；模型调度的每个任务同样持有独立 promise。`stream_id` 用于排队分组和追踪上下文，不是 HTTP 回调查找键。

当前视频接口没有“先返回 202/job_id，再轮询任务结果”的协议。HTTP 请求会保持等待，直到 runner 完成后回调发送 JSON。客户端关闭连接也没有一条已实现的上传任务自动取消链路；不能把 curl 超时当成服务端已停止计算。

### 4.7 已有 Web 前端的入口

[visionApi.ts](../../front/src/services/visionApi.ts) 中的 `requestMultipart` 创建 `FormData`，执行 `fetch` 后等待完整响应，再映射为前端结果。当前 `DROGON_API` 明确列出图片 `/infer` 和单模型视频 `/infer_video`；这里没有摄像头流创建/SSE UI，也没有自动添加 Bearer 请求头。

默认使用 [Vite 代理配置](../../front/vite.config.ts) 转到 `127.0.0.1:8080`，或由 `VITE_API_BASE_URL` 指定服务地址。代理情况下，后端门禁识别的是代理连接来源，不能把浏览器用户数量直接当作后端识别的 IP 数量。

<a id="client-ips"></a>
## 5. 多个客户端 IP 同时提交

### 5.1 请求门禁怎样识别 IP

**作用：** 在进入业务路由前，按请求头和对端 IP 决定是否放行。

**原始代码节选：** `ApiGateway::registerRequestGate`。

来源：[yolo_onnx_cpp/drogon/api_gateway.cpp](../../yolo_onnx_cpp/drogon/api_gateway.cpp#L135-L148)，编写时第 135～148 行。

```cpp
            const std::string& path = request->path();
            if (path == "/health" || path == "/ready") {
                chain_callback();
                return;
            }

            const RequestGateDecision decision = gate->check(
                request->getHeader("Authorization"),
                request->peerAddr().toIp()
            );
            if (decision.status == RequestGateStatus::Allowed) {
                chain_callback();
                return;
            }
```

`/health` 和 `/ready` 按路径直接豁免。其余请求包括上传、创建流、查询、删除、SSE 建连和 `/metrics` 都会经过门禁。SSE 后续同一连接内的事件不重复消耗 HTTP 请求令牌。

门禁取 TCP 对端 IP，没有读取 `X-Forwarded-For` 或 `X-Real-IP`。因此：同一主机多个进程通常共享桶；不同机器直接连接且来源 IP 不同才进入不同桶；经过 NAT 或反向代理后，桶按服务器实际看到的对端 IP 合并。改变请求头不会在当前实现中改变限流身份。

### 5.2 令牌桶与鉴权的实际执行顺序

`RequestGate` 在 mutex 内保护全部桶与计数。每个新 IP 的初始令牌数为 `burst`；请求到来时按经过的单调时间补充令牌，不需要后台定时线程。

**原始代码节选：** 补充令牌和消费之后的鉴权决定。

来源：[yolo_onnx_cpp/drogon/request_gate.cpp](../../yolo_onnx_cpp/drogon/request_gate.cpp#L82-L112)，编写时第 82～112 行。

```cpp
        Bucket& bucket = found->second;
        const double elapsed_seconds =
            std::chrono::duration<double>(now - bucket.updated_at).count();
        if (elapsed_seconds > 0.0) {
            bucket.tokens = std::min(
                static_cast<double>(burst_),
                bucket.tokens + elapsed_seconds * rate_
            );
            bucket.updated_at = now;
        }
        bucket.last_seen = now;

        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            if (!authorized) {
                ++stats_.unauthorized_count;
                stats_.tracked_client_count = buckets_.size();
                return {RequestGateStatus::Unauthorized, 0};
            }
            ++stats_.allowed_count;
            stats_.tracked_client_count = buckets_.size();
            return {};
        }

        ++stats_.rate_limited_count;
        stats_.tracked_client_count = buckets_.size();
        const int retry_after = std::max(
            1,
            static_cast<int>(std::ceil((1.0 - bucket.tokens) / rate_))
        );
        return {RequestGateStatus::RateLimited, retry_after};
```

可把公式写成：`tokens = min(burst, old_tokens + elapsed_seconds × rate)`。有至少一个令牌就先消耗 1，再按 `authorized` 返回 Allowed 或 Unauthorized；无令牌直接 RateLimited。

这与“鉴权失败不计入限流”不同：当前错误 token 也会消耗令牌。`authorized` 在加锁前已计算，但开启限流后，最终返回 401 还是 429 取决于桶状态。限流关闭时才直接按 token 决定允许或 401。

**示意算例：** `rate=2`、`burst=2`，A 在同一时刻前两次请求各消耗一个令牌，第三次 429；B 是新 IP，仍有自己的两个令牌。500ms 后 A 补回一个令牌。限流统计的是请求次数，不是图片张数、视频帧数或 CPU 时间。

实现还限制最多跟踪 16384 个客户端，定期清除空闲超过 10 分钟的桶；空间不足且清理后仍满时，新客户端得到 429。`Retry-After` 是至少 1 秒的整数等待建议。

### 5.3 两个客户端同时上传的时序

下图是假设两次请求都放行、任务池有空间的示意。A/B 的 worker 取出顺序与最终完成顺序不作保证。

```mermaid
sequenceDiagram
    participant A as 客户端 A
    participant B as 客户端 B
    participant G as Drogon / RequestGate
    participant V as VideoJobExecutor
    participant S as 共享模型调度器
    participant E as YoloEngine
    par A 上传
        A->>G: POST /infer_video，视频 A
        G->>G: 用来源 IP A 检查令牌和 token
        G->>V: 提交 job A，持有 callback A
    and B 上传
        B->>G: POST /infer_video，视频 B
        G->>G: 用来源 IP B 检查令牌和 token
        G->>V: 提交 job B，持有 callback B
    end
    Note over A,B: 两个原 HTTP 请求均在等待最终响应
    V->>S: worker A 提交 stream A 的某个检测帧
    V->>S: worker B 提交 stream B 的某个检测帧
    S->>E: 按流选择任务；并发量受 worker / 后端约束
    E-->>S: 本次推理结果
    S-->>V: 对应任务 promise / future
    Note over V,E: 各视频继续解码、检测、跟踪，直到处理结束
    V-->>A: callback A：HTTP 200 + 视频 A 的 JSON
    V-->>B: callback B：HTTP 200 + 视频 B 的 JSON
```

A、B 通过门禁，只说明允许进入业务入口；不表示获得独占模型、不表示每 IP 分到相同 CPU，也不表示整段视频会以固定时限完成。

### 5.4 IP 限流与 stream_id 调度的不同边界

| 层级 | 分组键/容量 | 资源不足时 |
| --- | --- | --- |
| HTTP 门禁 | 对端 IP 的令牌桶 | 429；鉴权失败且有令牌时 401 |
| 整段视频任务池 | 所有上传视频共享的 FIFO | 等待队列满返回 503，不替换已接收视频 |
| 实时流注册表 | 全局 `stream_id`，受 `max_streams` 限制 | 重复 ID 或注册容量满返回 409 |
| 高/低推理调度器 | 各自按 `stream_id` 分组 | 每流等待队列满时替换最旧等待任务，返回内部 `Replaced` 状态 |
| 模型执行 | 调度 worker / OpenVINO 请求池 | 等待执行或等待空闲请求；不是按 IP 预留 |

上传视频查询参数 `stream_id` 没有实时注册入口那套唯一性检查。A 和 B 如果同时使用相同字符串，会在同级调度器里共享一条等待队列和 in-flight 限制，甚至影响对方等待任务的替换；但仍各自拿回自己的 future 和 HTTP 回调，跟踪器也仍是任务各自的局部对象。

上传 ID 也可能与摄像头 ID 相同，因为两类请求使用同一调度器，实时注册表不负责为上传 ID 去重。自动生成的 `upload-N` 只对自动计数路径提供进程内区分，不保证避开用户自定义同名 ID。示例采用 `client-a-*`、`client-b-*`、`cam-*` 前缀减少人为混用。

<a id="camera-ips"></a>
## 6. 多个摄像头 IP 创建实时任务

### 6.1 创建请求：客户端告诉服务器去读取哪个源

客户端提交 JSON：`{"stream_id":"cam-east","source":"rtsp://192.168.20.31:554/stream1"}`。真正连接摄像头的是运行 C++ 服务的服务器，因此服务器要能访问这个摄像头地址。创建请求的客户端 IP 不必与摄像头 IP 相同。

`registerRealtimeStreamRoutes` 检查 JSON 对象及两个字符串字段后，调用 `RealtimeStreamManager::create`。`create` 检查 ID 长度为 1～64、字符为字母数字及 `.`、`_`、`-`，`source` 非空；目前不在请求线程内验证源能否打开。

### 6.2 注册状态与创建两条线程

**作用：** 给一条流分配独立上下文，并在持有注册表锁期间完成线程句柄赋值。

**原始代码节选：** `RealtimeStreamManager::Impl::create`。

来源：[yolo_onnx_cpp/stream/realtime_stream_manager.cpp](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L111-L131)，编写时第 111～131 行。

```cpp
        auto context = std::make_shared<StreamContext>();
        context->stream_id = stream_id;
        context->source = source;
        context->public_source = redactedSource(source);
        context->live_source = isLiveSource(source);

        // Publish fully assigned thread handles before another caller can stop the stream.
        std::unique_lock<std::mutex> lock(mutex_);
        if (streams_.find(stream_id) != streams_.end()) {
            error_message = "stream_id already exists";
            return false;
        }
        if (streams_.size() >= static_cast<size_t>(config_.max_streams)) {
            error_message = "maximum stream count reached";
            return false;
        }
        streams_.emplace(stream_id, context);

        try {
            context->decoder = std::thread(&Impl::runWorker, this, context, true);
            context->processor = std::thread(&Impl::runWorker, this, context, false);
```

持锁完成注册和线程赋值，是为了避免另一个 DELETE 调用看见尚未赋好的线程句柄。如果创建线程抛异常，后续代码停止已启动线程、移除注册记录、归档计数并返回创建失败。

初始上下文包含 `starting` 状态、空帧队列、空事件队列、独立计数、订阅者和停止标志。源码中的 `StreamContext` 从 [第 239 行附近](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L239) 开始。

**下一步：** `runWorker(context, true)` 进入 `decodeLoop`，`runWorker(context, false)` 进入 `processLoop`。HTTP 端查询一次快照并返回 201。由于线程已经运行，201 响应中的状态可能已经变化，不能假定固定为 `starting` 或 `running`。

### 6.3 解码：连接、读帧和重连

`isLiveSource` 的现有分类很直接：包含 `://` 且不是以 `file://` 开头，按实时源处理。因此普通 HTTP 文件 URL 也走实时策略，而不是因为它指向一个视频文件就自动采用本地 FIFO 策略。

`decodeLoop` 首先设置 `connecting/reconnecting`，局部创建 `cv::VideoCapture`。实时源通过参数传入打开超时 5000ms 和读取超时 2000ms，实际支持程度取决于 OpenCV 所选视频后端；它不是程序级的强制线程中断保证。本地文件调用普通 `capture.open(source)`。

打开成功设置 `running`、宽高和源 FPS，清除连接错误。每次 `capture.read(frame)` 成功后生成：

- `frame_index`：本流递增源帧号，重连时不从零重新创建计数器。
- `captured_at`：**本地解码读帧完成时**的单调时钟时间，不是摄像头拍摄时间。
- `timestamp_ms`：优先读取 `CAP_PROP_POS_MSEC`；没有有效正数时用帧号/FPS，仍不可用时用本地流运行时间。
- `frame.clone()`：队列中独立持有图像数据，避免下一次读取复用 `cv::Mat` 缓冲造成内容变化。

实时打开失败进入 `reconnecting`，等待约 1 秒后再尝试；读取中断释放 capture、增加重连计数并重连。本地源打不开为 `failed`，本地读取结束由处理线程消化剩余队列后完成。网络连接失败不会因为 `stream_max_consecutive_errors=5` 就自动五次停止，该阈值用于处理/模型错误。

### 6.4 图像队列：实时优先新鲜，本地文件施加背压

**原始代码节选：** `decodeLoop` 入队时的满队列处理。

来源：[yolo_onnx_cpp/stream/realtime_stream_manager.cpp](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L502-L523)，编写时第 502～523 行。

```cpp
                    const size_t depth =
                        static_cast<size_t>(config_.per_stream_queue_depth);
                    if (!context->live_source) {
                        context->frame_ready.wait(lock, [&context, depth]() {
                            return context->stop_requested
                                || context->frames.size() < depth;
                        });
                    }
                    if (context->stop_requested) {
                        return;
                    }
                    while (context->live_source && context->frames.size() >= depth) {
                        context->frames.pop_front();
                        ++context->decoder_queue_drop_count;
                        ++context->dropped_frame_count;
                    }
                    context->frames.push_back(CapturedFrame{
                        frame_index,
                        timestamp_ms,
                        captured_at,
                        frame.clone()
                    });
```

实时源已满时先丢队首旧帧，增加 `decoder_queue_drop_count` 和总丢帧计数，再放入新帧。本地文件满时条件变量等待，处理线程取走一帧后才继续读入队列。

处理端的 `takeNextFrame` 还有第二次选择：实时源取 `frames.back()`，将其余旧帧合并丢弃并增加 `processor_coalesced_frame_count`；本地文件取 `frames.front()`。所以 `per_stream_queue_depth=2` 不是承诺每批两帧都能推理，它只是有界缓冲。

### 6.5 两路摄像头如何共享模型

```mermaid
flowchart LR
    CA[摄像头 A] --> DA[decode A]
    CB[摄像头 B] --> DB[decode B]
    DA --> QA[frames A]
    DB --> QB[frames B]
    QA --> PA[process A / tracker A]
    QB --> PB[process B / tracker B]
    PA --> SH[共享高模调度器]
    PB --> SH
    PA --> SL[共享低模调度器]
    PB --> SL
    SH --> HE[高模引擎 / 请求池]
    SL --> LE[低模引擎 / 请求池]
    HE --> FA[各任务独立 future]
    LE --> FA
    FA --> PA
    FA --> PB
    PA --> EA[SSE A / 状态 A]
    PB --> EB[SSE B / 状态 B]
```

两路分别解码、保存灰度、推进轨迹，不交换跟踪状态。两个处理线程可以同时等待不同模型任务；真正执行数量由两个调度器和两个后端决定。共享的是模型及其调度资源，不是所有摄像头一起维护一套 tracker。

<a id="one-frame"></a>
## 7. 跟随一帧进入模型并返回

本节主线是 [RealtimeStreamManager::processLoop](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L654)，不是离线上传视频的 replay 循环。

### 7.1 本路处理器和检测节奏初始化

**作用：** 将本路跟踪状态放在本路处理线程中，避免共享可变轨迹。

**原始代码节选：** 每条实时处理线程启动时的局部初始化。

来源：[yolo_onnx_cpp/stream/realtime_stream_manager.cpp](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L654-L665)，编写时第 654～665 行。

```cpp
    void processLoop(const std::shared_ptr<StreamContext>& context) {
        const bool high_low = low_res_engine_ != nullptr;
        StreamProcessor processor(high_low);
        auto& authority_tracker = processor.authority();
        bool pending_authority_refresh = false;
        int64_t last_high_res_frame_index = -1;
        DetectionCadence cadence(
            config_.video_detect_fps, config_.video_high_detect_fps,
            low_res_engine_ != nullptr
        );
        bool force_high_res = true;

```

有低模型就选择双模型 `StreamProcessor`，否则用单模型；`force_high_res=true` 使第一帧请求高模。`DetectionCadence` 保存本路高低检测到期时间，而不是在每帧重新创建。

**下一步：** `takeNextFrame` 阻塞等帧，取到图像后检查实时帧是否过旧。

### 7.2 准备灰度与光流，再决定本帧用什么

取到的图像为空或已经超过 `max_result_age_ms` 时直接跳过。随后 `processor.prepareFrame(...)` 转灰度，在有上一帧灰度和既有轨迹时计算光流质量、运动估计和轨迹投影。该函数是 `const`，返回准备结果，不直接推进已有跟踪器或替换上一帧灰度。

准备可能耗时，所以接着再查一次帧龄。严重光流质量下降可强制高模；双模型权威跟踪器也能请求刷新，相关最小帧间隔由现有策略常量约束。

`DetectionCadence::select(timestamp_ms, force_high_res)` 返回 `High / Low / None`，核心规则是：首帧或时间倒退时重置节奏；紧急高模优先；双模型周期高模到期优先于低模；否则低模到期用低模；都没到期就走光流。只有单模型时，用 `video_detect_fps` 的到期节奏选高模型。

**示意：** `video_detect_fps=4` 对应 250ms 低模间隔，`video_high_detect_fps=1` 对应 1000ms 高模间隔。高低同时到期时本帧只跑高模，低模到期状态保留到后续输入帧，不是同一帧串行再补一个低模。实际源帧时间、过期丢弃、紧急刷新都会影响最终执行次数，不能从配置直接断言每秒稳定检测 5 次。

`video_detect_fps=0` 使低模/单模检测每个输入帧都到期；双模型仍按高模优先规则选一级。`video_high_detect_fps=0` 关闭周期高模，不取消首帧和紧急刷新。详见 [detection_cadence.h](../../yolo_onnx_cpp/stream/detection_cadence.h#L17)。

### 7.3 预处理、上下文与 submit

**作用：** 图像转换成所选模型尺寸的张量，把“属于哪路哪帧”一起交给调度器。

预处理实现在 [image_processing.cpp](../../yolo_onnx_cpp/image/image_processing.cpp#L133)：根据配置 letterbox 或直接 resize，将 BGR 转为 RGB 通道顺序并除以 255，输出 float32、NCHW、batch=1 的连续数据。多路模板的高模型形状是 `[1,3,736,1280]`，低模型是 `[1,3,384,640]`，不是把宽高放反的 `[1,3,W,H]`。`TensorInput` 还保留原图宽高和缩放/填充信息，后处理将检测坐标映射回原图。

**原始代码节选：** `runModel` 构造上下文并等待对应模型调度。

来源：[yolo_onnx_cpp/stream/realtime_stream_manager.cpp](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L631-L647)，编写时第 631～647 行。

```cpp
        InferenceContext context;
        context.stream_id = stream_id;
        context.frame_index = frame.frame_index;
        context.timestamp_ms = frame.timestamp_ms;

        const auto& scheduler = high_res
            ? high_res_scheduler_
            : low_res_scheduler_;
        const auto& engine = high_res
            ? high_res_engine_
            : low_res_engine_;
        if (engine == nullptr) {
            throw std::runtime_error("requested realtime model is unavailable");
        }
        if (scheduler != nullptr) {
            return scheduler->submit(std::move(input.value()), std::move(context), urgent).get();
        }
```

`InferenceContext` 包含 `stream_id`、`frame_index`、`timestamp_ms`，与 tensor 一起移动进调度任务。`future.get()` 在**该路 processor 线程**等待，不是创建流的 HTTP 请求在等，也不是返回 201 前要先推理一帧。

实时入口同一路一次选择一个模型并等待结果，所以不会在高模尚未返回时持续提交这一流的后续帧；解码线程仍继续工作，必要时丢旧帧。代码留有 scheduler 为空时直接调用引擎的后备路径，但正常网关初始化会提供可用模型对应的调度器。

**下一步：** 进入所选 `InferenceScheduler::submit`。

### 7.4 调度器：每个任务一份 promise，每个流一条等待队列

**作用：** 在有限模型 worker 间选择不同流的任务，并限制单流积压。

**原始代码节选：** 创建 `ScheduledTask` 并取得本次 future。

来源：[yolo_onnx_cpp/model/inference_scheduler.cpp](../../yolo_onnx_cpp/model/inference_scheduler.cpp#L67-L77)，编写时第 67～77 行。

```cpp
    std::future<ScheduledInferenceResult> submit(
        TensorInput input,
        InferenceContext context,
        bool urgent
    ) {
        auto task = std::make_shared<ScheduledTask>();
        task->input = std::move(input);
        task->context = std::move(context);
        task->urgent = urgent;
        task->submitted_at = std::chrono::steady_clock::now();
        auto future = task->promise.get_future();
```

`ScheduledTask` 拥有 tensor、上下文、urgent 标志、提交时间和 promise。空 `stream_id` 内部归到 `__default__`，非空按原值分组。

**原始代码节选：** 每流队列容量和调度唤醒。

来源：[yolo_onnx_cpp/model/inference_scheduler.cpp](../../yolo_onnx_cpp/model/inference_scheduler.cpp#L90-L109)，编写时第 90～109 行。

```cpp
            const std::string stream_id = normalizedStreamId(task->context.stream_id);
            auto& queue = queues_[stream_id];
            if (queue.size() >= per_stream_queue_depth_) {
                replaced = std::move(queue.front());
                queue.pop_front();
                --stats_.queued_count;
                ++stats_.replaced_count;
            }

            queue.push_back(task);
            ++stats_.submitted_count;
            ++stats_.queued_count;
            stats_.max_queued_count = std::max(
                stats_.max_queued_count,
                stats_.queued_count
            );
            if (replaced != nullptr) {
                unscheduleReadyLocked(stream_id);
            }
            scheduleReadyLocked(stream_id);
```

满时替换的是**尚未执行的最旧任务**，不会取消已经进入模型的任务。锁外用被替换任务自己的 promise 返回 `Replaced`，新任务的 future 则继续等待。这也解释了为什么同名流可能相互影响排队，但不会把新任务结果误交给旧任务的 future。

`ready_streams_` 防止同一流重复进入 ready 队列；`in_flight_streams_` 阻止同一调度器并行执行同一流的两项任务。ready 队列里放流 ID，而不是把某一流所有待执行张量连续展开。

**原始代码节选：** `takeNext` 在紧急和普通流之间选择。

来源：[yolo_onnx_cpp/model/inference_scheduler.cpp](../../yolo_onnx_cpp/model/inference_scheduler.cpp#L192-L214)，编写时第 192～214 行。

```cpp
        if (!urgent_ready_.empty()
            && (normal_ready_.empty() || urgent_burst_ < kMaxUrgentBurst)) {
            stream_id = std::move(urgent_ready_.front());
            urgent_ready_.pop_front();
            ++urgent_burst_;
        } else {
            stream_id = std::move(normal_ready_.front());
            normal_ready_.pop_front();
            urgent_burst_ = 0;
        }

        ready_streams_.erase(stream_id);
        auto queue = queues_.find(stream_id);
        if (queue == queues_.end() || queue->second.empty()) {
            return nullptr;
        }

        auto task = std::move(queue->second.front());
        queue->second.pop_front();
        --stats_.queued_count;
        ++stats_.in_flight_count;
        in_flight_streams_.insert(stream_id);
        return task;
```

取出的任务被记为 in-flight。普通任务也在等待时，紧急任务最多连续被选取 3 次，之后让普通队列获得一次机会；没有普通任务时不会为了配额故意闲置 worker。是否进入紧急 ready 队列看本流队首任务的 urgent 标记，不是把任何后来到达的紧急任务都插到全局最前面。

任务结束后 `finishTask` 清除 in-flight；若该流仍有等待任务，重新加入 ready 队尾，因此可在流之间轮转。公平性体现在选择顺序，不保证各路相同 FPS、相同 CPU 占用或相同完成时间。高低模型调度器各自维护独立状态，单流 in-flight 限制也是“每个调度器内部”的限制。

**下一步：** worker 在真正调用模型前检查提交年龄。

### 7.5 worker 与模型请求池

**原始代码节选：** `InferenceScheduler::run` 的过期检查和执行。

来源：[yolo_onnx_cpp/model/inference_scheduler.cpp](../../yolo_onnx_cpp/model/inference_scheduler.cpp#L255-L289)，编写时第 255～289 行。

```cpp
            ScheduledInferenceResult output;
            const auto now = std::chrono::steady_clock::now();
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - task->submitted_at
            );
            if (max_request_age_.count() > 0 && age > max_request_age_) {
                output = statusResult(
                    ScheduledInferenceStatus::Stale,
                    "queued inference exceeded the maximum request age"
                );
            } else {
                try {
                    output.status = ScheduledInferenceStatus::Completed;
                    output.result = engine_->infer(
                        task->input,
                        std::move(task->context)
                    );
                    output.result.queue_wait_ms =
                        std::chrono::duration<double, std::milli>(
                            now - task->submitted_at
                        ).count();
                    output.result.timing_samples.queue_wait_ms.push_back(
                        output.result.queue_wait_ms
                    );
                } catch (const std::exception& e) {
                    output = statusResult(
                        ScheduledInferenceStatus::Failed,
                        e.what()
                    );
                }
            }

            const ScheduledInferenceStatus status = output.status;
            finishTask(stream_id, status);
            task->promise.set_value(std::move(output));
```

任务超过 `max_request_age_ms` 时返回 `Stale`，不会调用模型。否则执行 `engine_->infer(task->input, context)`；异常转为 `Failed`。`queue_wait_ms` 是从 submit 到 worker 开始执行前的时间，不含上传任务池等待、图像解码排队或后续借模型请求池的等待。

ONNX Runtime 路径用本次张量调用 `session_.Run`。OpenVINO 则先借一项空闲 InferRequest：

来源：[yolo_onnx_cpp/model/yolo_engine.cpp](../../yolo_onnx_cpp/model/yolo_engine.cpp#L433-L445)，编写时第 433～445 行。

```cpp
        size_t request_index = 0;
        {
            std::unique_lock<std::mutex> lock(ov_pool_mutex_);
            ov_pool_condition_.wait(lock, [this]() {
                return !ov_available_requests_.empty();
            });
            request_index = ov_available_requests_.back();
            ov_available_requests_.pop_back();
        }
        RequestIndexGuard request_guard(
            ov_pool_mutex_, ov_pool_condition_, ov_available_requests_, request_index
        );
        ov::InferRequest& infer_request = ov_infer_requests_[request_index];
```

拿到索引后立即构造 `RequestIndexGuard`。其析构在 mutex 下归还索引并通知等待者，所以正常返回和作用域内异常都会归还该项；锁仅保护索引池，不包住整个推理。

OpenVINO 接着把 float 数据复制到本次 `ov::Tensor`，绑定输入，调用 `infer_request.infer()`，读取输出。两种后端最终都进行检测输出解码、阈值筛选和同类 NMS，并返回原图坐标的 `Detection`。请求 guard 在整次函数结束时才归还索引，因此读取/后处理输出时不会被另一次调用复用同一请求对象。

调度 worker 数与 OpenVINO 请求池大小对齐，但图片入口也直接共享高模型池，所以调度 worker 仍可能等待被图片占用的请求；不存在为视频独占保留的 InferRequest。

**原始代码节选：** `YoloEngine::infer` 回填调用上下文。

来源：[yolo_onnx_cpp/model/yolo_engine.cpp](../../yolo_onnx_cpp/model/yolo_engine.cpp#L543-L547)，编写时第 543～547 行。

```cpp
InferResult YoloEngine::infer(const TensorInput& input, InferenceContext context) {
    InferResult result = impl_->infer(input);
    result.context = std::move(context);
    return result;
}
```

调度器执行 `finishTask`，再 `task->promise.set_value(...)`；提交方的 `.get()` 返回本次 `ScheduledInferenceResult`。这是一条请求对象到 promise/future 的直接返回链路，不需要根据帧号在一个全局结果数组里猜测归属。

### 7.6 回到 processLoop：更新跟踪，提交灰度

| 调度结果/本帧选择 | 实时处理动作 |
| --- | --- |
| `Completed` 且帧仍新鲜 | `applyDetections` 更新跟踪；标记 `detection_frame=true`、`model_tier=high/low`，清除相应刷新状态 |
| `Completed` 但结果已过期 | 增加过旧帧计数并跳过本帧，不应用该次检测结果 |
| `Stale` 或 `Replaced` | 增加 `skipped_inference_count`；若帧仍新鲜，则走 `applyFlow` |
| `Failed`、`Stopped` 或预处理异常 | 计入处理错误；未达到连续错误门槛且帧仍新鲜时可走光流；达到门槛则将该流设为 failed 并停止 |
| `DetectionTier::None` | 不向模型提交，使用 `applyFlow` 传播既有轨迹 |

成功 Completed 会重置连续推理错误计数（即便本帧最终因过期跳过）。解码线程顶层异常和处理线程顶层未捕获异常有单独的 `runWorker/failWorker` 隔离路径，不要都归入五次模型错误熔断。

**原始代码节选：** `StreamProcessor::applyDetections` 的跟踪器分流。

来源：[yolo_onnx_cpp/stream/stream_processor.cpp](../../yolo_onnx_cpp/stream/stream_processor.cpp#L44-L61)，编写时第 44～61 行。

```cpp
const std::vector<TrackedDetection>& StreamProcessor::applyDetections(
    const std::vector<Detection>& detections,
    int64_t frame_index,
    bool high_res,
    const std::vector<ProjectedTrack>& projected_tracks
) {
    if (high_low_) {
        tracks_ = high_res
            ? authority_tracker_.updateHighRes(detections, frame_index)
            : authority_tracker_.updateLowRes(detections, projected_tracks, frame_index);
    } else {
        if (!high_res) {
            throw std::invalid_argument("single-model processor cannot apply low-model detections");
        }
        tracks_ = byte_tracker_.update(detections);
    }
    return tracks_;
}
```

高低模式使用 `AuthorityTracker`：高模更新权威类别、分数和存在性；低模用于几何修正和待确认候选；光流传播已有轨迹并受跟踪年龄规则限制。单模型则使用 ByteTracker。本文不展开跟踪算法内部的所有门槛，详见 [authority_tracker.cpp](../../yolo_onnx_cpp/tracking/authority_tracker.cpp)。

`applyDetections/applyFlow` 已经修改本路轨迹；`finishFrame` 只是把 prepared 的当前灰度移动到 `previous_gray_`。它不是一次提交灰度和轨迹的事务。如果直到后面的 publish 才发现过龄，轨迹和灰度仍保留本次一致推进的状态，只丢弃对外输出。

**下一步：** 生成 `RealtimeFrameEvent` 并发布给本路订阅者。

### 7.7 三类主队列和两种时钟，放到同一张表里看

| 队列 | 存放内容 | 满/忙时 | 等待时间是否由 `max_request_age_ms` 直接覆盖 |
| --- | --- | --- | --- |
| `VideoJobExecutor::jobs_` | 一整个上传视频的闭包 | 已达到等待容量就拒绝新提交，503 | 否，尚未提交单帧模型任务 |
| `StreamContext::frames` | 一路已解码的 `cv::Mat` 帧 | 实时丢旧/合并，本地文件等待空位 | 否；实时帧还受 `max_result_age_ms` 约束 |
| `InferenceScheduler::queues_` | 一路等待模型执行的张量任务 | 替换最旧等待任务，内部 `Replaced` | 是，从该任务 submit 开始 |

离线上传的 `AsyncInferWorker` 请求/结果缓冲是算法内的额外一层，见第 4.5 节；OpenVINO 空闲请求索引池是执行资源池，不是这些任务队列中的任意一个。

实时新鲜度的时间线为：

```text
摄像头拍摄 → 网络/解码器内部缓存 → 本地 read 完成 → 图像排队/光流准备/预处理 → submit → 模型执行 → 发布
                                   ^ captured_at                       ^ submitted_at
                                   |---------- max_result_age_ms ----------------------|
                                                                        |--排队--|
                                                                        max_request_age_ms
```

因此这里的实时帧龄不包括摄像头、网络和解码器内部已经产生的延迟。`timestamp_ms` 用于帧时间与节奏判断，不能替代单调时钟 `captured_at` 计算本地新鲜度。源码在取帧后、准备后、推理结果处理时以及发布时分阶段检查过期；并非每阶段都强制取消正在运行的计算。

<a id="lifecycle"></a>
## 8. 结果订阅、异常和任务结束

### 8.1 publish：更新本路统计，再调用本路订阅者

**作用：** 对仍可发布的结果分配事件序号并对外通知。

**原始代码节选：** `processLoop` 在处理完成之后生成事件。

来源：[yolo_onnx_cpp/stream/realtime_stream_manager.cpp](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L798-L806)，编写时第 798～806 行。

```cpp
            processor.finishFrame(std::move(prepared));
            RealtimeFrameEvent event;
            event.frame_index = frame.frame_index;
            event.timestamp_ms = frame.timestamp_ms;
            event.detection_frame = detection_frame;
            event.model_tier = std::move(model_tier);
            event.tracks = std::move(tracks);

            publish(context, std::move(event), frame.captured_at);
```

`publish` 在流 mutex 下检查停止和帧龄；有效普通事件增加处理帧数，检测事件增加高/低检测数，记录 `latest_result_captured_at`；随后递增 sequence、保留至多 256 条事件，复制订阅者列表。

**原始代码节选：** `publish` 的停止/过期检查、计数与事件缓存部分。

来源：[yolo_onnx_cpp/stream/realtime_stream_manager.cpp](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L833-L872)，编写时第 833～872 行。

```cpp

    void publish(
        const std::shared_ptr<StreamContext>& context,
        RealtimeFrameEvent event,
        std::chrono::steady_clock::time_point captured_at = {}
    ) {
        std::vector<Subscriber> subscribers;
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            if (!event.terminal) {
                if (context->stop_requested) {
                    return;
                }
                const auto now = std::chrono::steady_clock::now();
                if (isFrameExpired(context->live_source, config_.max_result_age_ms,
                                   captured_at, now)) {
                    ++context->stale_frame_drop_count;
                    ++context->dropped_frame_count;
                    return;
                }
                event.result_age_ms =
                    std::chrono::duration<double, std::milli>(now - captured_at).count();
                ++context->processed_frame_count;
                if (event.detection_frame) {
                    ++context->detection_frame_count;
                    if (event.model_tier == "high") {
                        ++context->high_res_detection_count;
                    } else {
                        ++context->low_res_detection_count;
                    }
                }
                context->latest_result_captured_at = captured_at;
            }
            event.sequence = context->next_sequence++;
            context->events.push_back(event);
            while (context->events.size() > kMaxBufferedEvents) {
                context->events.pop_front();
            }
            subscribers = context->subscribers;
        }
```

真正调用 subscriber callback 发生在释放流 mutex 之后。返回 false 或抛异常的订阅者会被移除，避免它继续接收后续事件。回调仍由发布方线程调用；当前没有为每个订阅者另建一个后台线程。

终止事件绕过普通帧新鲜度判断，携带最终状态；不会增加正常处理帧数。

### 8.2 SSE：创建流的响应和结果连接是两个请求

客户端单独访问 `GET /streams/{id}/events`。路由先检查流存在，再创建 `newAsyncStreamResponse` 并调用 manager 的 `subscribe`。

**原始代码节选：** SSE 的事件名称、序号与数据拼接。

来源：[yolo_onnx_cpp/drogon/realtime_stream_handlers.cpp](../../yolo_onnx_cpp/drogon/realtime_stream_handlers.cpp#L271-L289)，编写时第 271～289 行。

```cpp
                            Json::StreamWriterBuilder writer;
                            writer["indentation"] = "";
                            const std::string data = Json::writeString(
                                writer,
                                streamEventToJson(event, class_names)
                            );
                            const std::string event_name =
                                event.terminal ? "end" : "detection";
                            const std::string payload =
                                "id: " + std::to_string(event.sequence)
                                + "\nevent: " + event_name
                                + "\ndata: " + data + "\n\n";
                            const bool sent = (*holder)->send(payload);
                            if (!sent || event.terminal) {
                                (*holder)->close();
                                holder->reset();
                                return false;
                            }
                            return true;
```

普通事件统一叫 `detection`，即使当前帧只是光流传播；必须结合 `detection_frame` 和 `model_tier` 判断是否执行了模型。终止事件叫 `end`，发送后关闭连接。

SSE 数据体字段为 `sequence`、`frame_index`、`timestamp_ms`、`result_age_ms`、`detection_frame`、`model_tier`、`terminal`、`status`、`tracks`。它没有 `stream_id`，客户端从订阅 URL 关联流。普通帧事件沿用 `RealtimeFrameEvent` 默认的 `status="running"`，不是发布时读取的管理器状态快照；完整运行状态仍应查询流接口。

订阅只接收注册成功后的新事件。内部虽然存 256 条事件，但当前 subscribe 没有遍历历史缓冲，也没有读取 `Last-Event-ID`；断开后重新连接不会补发遗漏结果。每路最多 32 个订阅者，终态或停止中的流拒绝订阅。若 HTTP 端检查后流状态变化或订阅失败，已经建立的 SSE 会发送 `error` 事件后关闭。

### 8.3 状态接口、响应与指标

| 接口 | 用途与正常行为 |
| --- | --- |
| `GET /health` | 返回 `{"status":"ok"}`，免门禁 |
| `GET /ready` | 仅检查流管理器是否存在，存在则 200/ready；不逐个探测摄像头或执行模型自测 |
| `GET /streams` | 返回 `{"streams":[...]}`，包括仍在注册表的终态流 |
| `GET /streams/{id}` | 返回该流快照；不存在返回 404 |
| `DELETE /streams/{id}` | 停止并删除，正常返回 `{"stream_id":"...","status":"stopped"}`；不存在/不可移除时返回 404 |
| `GET /streams/{id}/events` | 流事件连接；不存在时 404 |
| `GET /metrics` | 文本指标，汇总实时流、调度器、上传任务池和门禁；不豁免鉴权/限流 |

流快照的 `source` 使用现有 URI 凭据遮盖逻辑，例如把 `scheme://user:pass@host/...` 变为 `scheme://***@host/...`。这不是对所有可能携带秘密的查询参数做通用脱敏。

指标阅读的关键区别：

- `latest_result_age_ms` 是**查询时刻**距最近已发布帧的本地解码时间，停止出帧后继续增大；首次结果前 0 是占位值。SSE `result_age_ms` 是这一帧**发布当时**的固定年龄。
- `dropped_frame_count` 由解码队列丢帧、处理端合并和过龄源帧丢弃组成；`skipped_inference_count` 和 `inference_error_count` 单独统计。
- `yolo_streams_active` 排除终态，`yolo_streams_registered` 包含所有仍注册记录；后者才反映剩余注册容量。
- `yolo_scheduler_queue_depth{tier="high"}` 与低级对应项是等待张量数量；`yolo_scheduler_in_flight` 是正在处理任务，不代表所有任务此刻都在模型算子里运行。
- 删除流时累计计数合并到 `retired_counters_`，管理器生命周期内的总计数不会因为删除记录而回退。

### 8.4 生命周期与异常隔离

```mermaid
stateDiagram-v2
    [*] --> starting: 注册并启动线程
    starting --> connecting: 解码线程开始
    connecting --> running: 打开成功
    connecting --> reconnecting: 实时源打开失败
    connecting --> failed: 本地源打开失败
    reconnecting --> running: 再次打开成功
    running --> reconnecting: 实时源断开
    running --> completed: 本地读取结束且处理队列耗尽
    running --> failed: 连续处理错误达到阈值或 worker 异常
    running --> expired: 运行时长检查到期
    running --> stopping: DELETE 请求停止
    stopping --> stopped: 两条线程结束
```

该图是主要路径示意：停止、到期和 worker 异常也能发生在连接/重连阶段。终态优先保留 `failed/expired`，正常结束再根据停止标志选择 `stopped/completed`。终态存在于注册表，只有 DELETE 或管理器析构才完成后续清理。

`runWorker` 的顶层异常保护把异常限制在本流：设置停止标志、清队列、唤醒线程，使用固定失败原因避免直接暴露底层源凭据，再尝试发送终止事件。一条流 worker 失败不会调用全局退出去结束其他流。

### 8.5 DELETE：从停止标志到释放注册名额

**原始代码节选：** `stopContext` 的停止与 join。

来源：[yolo_onnx_cpp/stream/realtime_stream_manager.cpp](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L907-L931)，编写时第 907～931 行。

```cpp
    static void stopContext(const std::shared_ptr<StreamContext>& context) {
        bool log_stop = false;
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            log_stop = !context->stop_requested && !isTerminalRealtimeStatus(context->status);
            context->stop_requested = true;
            if (!isTerminalRealtimeStatus(context->status)) {
                context->status = "stopping";
            }
            context->frames.clear();
        }
        context->frame_ready.notify_all();
        if (log_stop) {
            logStreamEvent(StreamLogEvent::StopRequested, snapshotOf(context));
        }

        if (context->decoder.joinable()
            && context->decoder.get_id() != std::this_thread::get_id()) {
            context->decoder.join();
        }
        if (context->processor.joinable()
            && context->processor.get_id() != std::this_thread::get_id()) {
            context->processor.join();
        }
    }
```

manager 的 `stop` 在注册表锁下查找记录并标记 `removal_in_progress`，保证只有一个调用方负责 join；释放注册表锁后执行上面的停止逻辑，最后归档累计量并删除注册记录。源码还防止在该流 worker 回调里对自身执行 stop/join。

DELETE 不是瞬间终止线程：处理线程若正在 future 等待模型，需要等调用返回后检查停止标志；解码线程可能仍在后端 open/read 中。不要把配置的帧龄阈值或后端超时理解为 DELETE 的严格响应时间保证。

停止同名终态流也会删除记录，HTTP DELETE 响应仍是 `status: stopped`；如果需要保存其原来的 `failed/completed/expired` 原因，应先读取状态。上传任务不注册到这个 manager，不能用 `DELETE /streams/upload-1` 取消一个上传视频。

<a id="examples"></a>
## 9. 完整操作示例与排查

### 9.1 示例拓扑与执行前提

以下地址全部是示意值，替换为实际网络地址。视频文件路径是各客户端本地路径，摄像头 `source` 则要能从服务器访问。

| 角色 | 示例地址 | 执行位置 |
| --- | --- | --- |
| C++ 服务 | `192.168.10.10:8080` | 按第 2.1 节启动其中一套服务 |
| 客户端 A | `192.168.10.21` | 本机执行图片/视频上传命令 |
| 客户端 B | `192.168.10.22` | 本机执行另一份视频上传命令 |
| 摄像头 A | `192.168.20.31:554` | 服务端主动读取此源 |
| 摄像头 B | `192.168.20.32:554` | 服务端主动读取此源 |

这些命令按模板默认的 HTTP、鉴权关闭编写。若现有服务已经启用 token，在客户端安全地准备好相应环境变量，并给请求加 `-H "Authorization: Bearer ${YOLO_API_TOKEN}"`；其中 `YOLO_API_TOKEN` 只是客户端示例变量名，应与部署实际值相符。HTTPS 部署则将地址改为实际 `https://` 地址及端口。不要把 token 写入本文。

### 9.2 确认服务已启动，再上传图片

在客户端 A 的 Bash 终端运行：

```bash
YOLO_API_BASE='http://192.168.10.10:8080'
curl -sS -i "$YOLO_API_BASE/health"
curl -sS -i "$YOLO_API_BASE/ready"
```

正常分别返回 HTTP 200 和 `status=ok/ready`。它们只确认对应健康端点可用，不证明所有模型输入都有效或摄像头已经连通。

在同一客户端 A 上传现有图片，替换路径：

```bash
curl -sS -i -X POST "$YOLO_API_BASE/infer" \
  -F 'image=@/path/to/existing-image.jpg'
```

预期成功为 200，响应体 `code=0` 并包含 `detections`。这里的路径由 curl 从客户端读取，不是让服务器去打开 `/path/to/existing-image.jpg`。

### 9.3 两个客户端 IP 并发提交上传视频

让 A、B 在两台客户端上同时执行；两个请求处理时间有重叠即可观察共享任务池与调度竞争。文件使用现有短视频，不需要完整数据集。

**客户端 A：**

```bash
YOLO_API_BASE='http://192.168.10.10:8080'
curl -sS -i -X POST \
  "$YOLO_API_BASE/infer_video?stream_id=client-a-run-001&include_frames=false" \
  -F 'video=@/path/to/client-a-short.mp4'
```

**客户端 B：**

```bash
YOLO_API_BASE='http://192.168.10.10:8080'
curl -sS -i -X POST \
  "$YOLO_API_BASE/infer_video_high_low?stream_id=client-b-run-001&include_frames=true&frame_limit=10" \
  -F 'video=@/path/to/client-b-short.mp4'
```

正常等待各自视频计算完成后得到 200/`code=0`。A 请求省略帧详情；B 最多输出 10 个帧结果，但服务仍处理视频算法所需的完整内容。无需手工设置 multipart 的 `Content-Type`，curl 会为 `-F` 生成正确 boundary。

不要用在同一台机器上运行两次 curl 并伪造 `X-Forwarded-For` 来证明“两个 IP 独立限流”。当前门禁不读该头。如果 A/B 经同一个代理或 NAT 到达服务端，即使是两台机器也可能共用一个桶，应以服务实际识别的对端地址为准。

默认限流关闭；仅凭这两条命令不会验证令牌桶。需要观察已有开启限流的服务时，可对照第 5.2 节和门禁指标，但本文不为演示更改服务配置或发送突发压测。

### 9.4 创建两路摄像头任务

在客户端 A（或有权限的控制端）运行；`cam-east` 和 `cam-west` 应尚未注册，流注册表应至少有两个空位。

```bash
YOLO_API_BASE='http://192.168.10.10:8080'
curl -sS -i -X POST "$YOLO_API_BASE/streams" \
  -H 'Content-Type: application/json' \
  --data '{"stream_id":"cam-east","source":"rtsp://192.168.20.31:554/stream1"}'

curl -sS -i -X POST "$YOLO_API_BASE/streams" \
  -H 'Content-Type: application/json' \
  --data '{"stream_id":"cam-west","source":"rtsp://192.168.20.32:554/stream1"}'
```

路径 `/stream1`、协议和端口要按摄像头实际情况替换。两次创建可以顺序发出：第一路创建后已经运行，第二路建立后便是多路并发；无需靠“同一毫秒发请求”才能实现多路处理。

正常创建为 HTTP 201。下面是**只保留部分字段的结构示意**，不保证当时的状态或计数值：

```json
{
  "stream_id": "cam-east",
  "source": "rtsp://192.168.20.31:554/stream1",
  "status": "connecting",
  "decoded_frame_count": 0,
  "processed_frame_count": 0,
  "queue_length": 0,
  "latest_result_age_ms": 0
}
```

真实响应还包含其他统计字段，且线程可能已进入 running/reconnecting。摄像头打不开也可能先收到 201，再通过状态接口看到重连。

### 9.5 查询状态和订阅结果

在控制端查询，两路注册后应分别能查到：

```bash
curl -sS -i "$YOLO_API_BASE/streams"
curl -sS -i "$YOLO_API_BASE/streams/cam-east"
curl -sS -i "$YOLO_API_BASE/streams/cam-west"
curl -sS "$YOLO_API_BASE/metrics"
```

关注 `status`、`decoded_frame_count`、`processed_frame_count`、`reconnect_count`、`last_error`、三类丢帧计数和 `latest_result_age_ms`。成功检测到零个目标也会有正常帧事件，所以 `tracks=[]` 不能单独证明任务失败。

**订阅终端 1：**

```bash
YOLO_API_BASE='http://192.168.10.10:8080'
curl -sS -N "$YOLO_API_BASE/streams/cam-east/events"
```

**订阅终端 2：**

```bash
YOLO_API_BASE='http://192.168.10.10:8080'
curl -sS -N "$YOLO_API_BASE/streams/cam-west/events"
```

`-N` 关闭 curl 输出缓冲，使事件到达时及时显示。这两条命令会保持连接等待新事件；停止 curl 只结束该订阅连接，不会删除流。

**普通光流事件示意：**

```text
id: 12
event: detection
data: {"sequence":12,"frame_index":18,"timestamp_ms":600.0,"result_age_ms":21.5,"detection_frame":false,"model_tier":"flow","terminal":false,"status":"running","tracks":[]}

```

这里 `id/sequence` 是事件序号，不是源帧号；`frame_index=18` 和 `sequence=12` 不相等是正常情况。客户端从 URL 知道该事件属于哪路，不应期待数据中存在未实现的 `stream_id` 字段。

### 9.6 停止并释放两路注册名额

另开控制终端，在确实要结束这两条示例流时执行：

```bash
YOLO_API_BASE='http://192.168.10.10:8080'
curl -sS -i -X DELETE "$YOLO_API_BASE/streams/cam-east"
curl -sS -i -X DELETE "$YOLO_API_BASE/streams/cam-west"
curl -sS -i "$YOLO_API_BASE/streams"
```

正常 DELETE 返回 200 和该 ID 的 `status=stopped`，仍在线的订阅者收到终止事件后关闭。列表中不再包含这两路，再查询单流为 404。自然 completed/failed/expired 的记录同样需要删除才能释放名额。

### 9.7 按现象定位问题

| 现象/状态码 | 先看哪里 | 应怎样理解 |
| --- | --- | --- |
| 服务未监听，打印配置失败 | `main`、`loadAppConfig/validateConfig` | 配置打不开或校验失败，尚未进入 HTTP 主循环 |
| 高/低模型加载失败 | `YoloEngine`、派生配置、模型路径 | 低模型配置存在但加载失败也会阻止整个服务启动 |
| 提示 OpenVINO 未编译 | CMake preset、`initOpenVino` | 后端选择与构建开关不一致 |
| 401 | `registerRequestGate`、token 环境变量设置 | Bearer 缺失/不匹配；开启限流时错误 token 也消费令牌 |
| 429，含 `Retry-After` | `RequestGate::check`、实际 peer IP | 桶耗尽或客户端跟踪容量不足；代理用户可能共用桶 |
| 上传返回 400 | multipart 解析、图片解码、视频输出选项、`VideoInferError` | 输入/选项有误；具体看错误 message |
| 请求体过大被拒绝 | Drogon body 限制配置 | 请求可能在业务 Handler 前被框架拒绝；不要假定所有框架错误都带应用 JSON 格式 |
| 视频提交 503 | `VideoJobExecutor::submit` | 等待任务池满，不是摄像头 `max_streams` 满 |
| 创建流 409 | `RealtimeStreamManager::create` | ID 已存在或注册表已满；检查残留终态记录 |
| 创建流 201 后一直 reconnecting | `decodeLoop`、服务端到摄像头连通性 | 201 只表示注册创建；确认源协议、路径、权限及后端能否打开 |
| 解码帧增加但输出稀少 | `takeNextFrame`、`discardExpiredFrame`、scheduler 指标 | 实时丢旧、模型排队或结果过龄；对照各独立计数定位 |
| `skipped_inference_count` 增长 | 调度器 `Replaced/Stale` | 跳过模型任务，不等同于相同数量源图像被丢弃 |
| 一路 failed、其他路继续 | `processLoop` 错误门槛、`runWorker/failWorker` | 本路异常隔离；区分连续模型错误与 worker 顶层异常 |
| SSE 连接却没历史数据 | `subscribe` | 只推送新事件，不实现历史重放；也要排除源未出帧/持续过龄 |
| 终态流 SSE 返回 error 后关闭 | SSE 路由与 `subscribe` | 注册记录存在，但终态拒绝新订阅 |
| DELETE 等待较久 | `stopContext`、模型/视频后端调用 | join 等待线程返回，不会强制中断正在执行的推理 |
| GET/DELETE 404 | 流注册表 | ID 不存在、已移除；上传任务 ID 本来就不在流管理器中 |
| 上传一直等待，`/streams` 查不到 | `VideoInferenceHandler` | 上传接口是原 HTTP 请求等待最终 JSON，没有任务查询协议 |
| 多客户端结果互相抢排队位置 | 自定义上传 `stream_id` | 检查是否多个任务或摄像头共用了同名调度键 |

<a id="verification"></a>
## 10. 源码阅读索引和验证说明

### 10.1 按函数寻找关键实现

| 阅读目标 | 当前源码入口 |
| --- | --- |
| 从启动到监听 | [main.cpp](../../yolo_onnx_cpp/main.cpp#L17) → [runApiServer](../../yolo_onnx_cpp/drogon/api_server.cpp#L7) → [ApiGateway](../../yolo_onnx_cpp/drogon/api_gateway.cpp#L67) |
| 配置读取与高低派生 | [loadAppConfig](../../yolo_onnx_cpp/config/app_config.cpp#L399)、[makeHighResAppConfig / makeLowResAppConfig](../../yolo_onnx_cpp/config/app_config.cpp#L508) |
| 请求身份与限流 | [registerRequestGate](../../yolo_onnx_cpp/drogon/api_gateway.cpp#L129)、[RequestGate::Impl::check](../../yolo_onnx_cpp/drogon/request_gate.cpp#L42) |
| 上传与异步 HTTP 回调 | [ImageInferenceHandler / VideoInferenceHandler](../../yolo_onnx_cpp/drogon/inference_handlers.cpp#L111)、[VideoJobExecutor](../../yolo_onnx_cpp/drogon/video_job_executor.cpp#L13) |
| 单模型/双模型离线入口 | [inferVideoFile](../../yolo_onnx_cpp/video/video_inference.cpp#L51)、[inferVideoFileHighLow](../../yolo_onnx_cpp/video/video_inference_high_low.cpp#L197) |
| 实时流创建及对外接口 | [registerRealtimeStreamRoutes](../../yolo_onnx_cpp/drogon/realtime_stream_handlers.cpp#L119)、[create](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L96) |
| 实时逐帧主链 | [decodeLoop](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L421) → [takeNextFrame](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L566) → [processLoop](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L654) → [publish](../../yolo_onnx_cpp/stream/realtime_stream_manager.cpp#L833) |
| 跟踪器的准备与状态更新 | [StreamProcessor](../../yolo_onnx_cpp/stream/stream_processor.cpp#L11)、[DetectionCadence](../../yolo_onnx_cpp/stream/detection_cadence.h#L10) |
| 帧任务调度与返回 | [submit](../../yolo_onnx_cpp/model/inference_scheduler.cpp#L67)、[takeNext / finishTask / run](../../yolo_onnx_cpp/model/inference_scheduler.cpp#L183) |
| 模型后端与请求借还 | [YoloEngine](../../yolo_onnx_cpp/model/yolo_engine.cpp#L207)、[initOpenVino](../../yolo_onnx_cpp/model/yolo_engine.cpp#L362)、[inferOpenVino](../../yolo_onnx_cpp/model/yolo_engine.cpp#L431) |

补充资料：[多路实时监测代码速读记录](多路实时监测代码速读记录_20260908.md)、[多路实时监测实施记录](多路实时监测实施记录_20260905.md)。历史报告用于背景参考，本文对当前行为以源码为准。

### 10.2 已有测试实际覆盖什么

本次阅读了相关测试源码，未重新构建或运行 CTest。不要将本表视为本次测试通过报告。

| 测试源码 | 已有断言覆盖的关键场景 |
| --- | --- |
| [request_gate_test.cpp](../../yolo_onnx_cpp/test_cpp/request_gate_test.cpp) | 门禁关闭、正确/错误 token、错误 token 消费令牌、同客户端耗尽与补充、不同客户端独立、统计与 Retry-After |
| [video_job_executor_test.cpp](../../yolo_onnx_cpp/test_cpp/video_job_executor_test.cpp) | 一个 worker 被占用时仍可排队，等待队列满拒绝，已接受任务执行完毕，队列/in-flight 计数归零 |
| [inference_scheduler_test.cpp](../../yolo_onnx_cpp/test_cpp/inference_scheduler_test.cpp) | 一个任务执行期间另一流旧等待帧被新帧替换、保留新帧上下文、完成及替换计数、队列最终排空 |
| [realtime_stream_manager_test.cpp](../../yolo_onnx_cpp/test_cpp/realtime_stream_manager_test.cpp) | 非法/重复流 ID、本地 120 帧背压完整处理、终态记录占位、帧龄查询继续增长、删除后累计量保留、worker 自停保护和双路异常隔离 |

源码中的三次紧急配额和轮转分支已经逐段阅读，但现有 `inference_scheduler_test` 不能被描述成完整覆盖了所有公平性、饥饿、过期和停止组合。真实网络多 IP、RTSP 后端超时和断流恢复也不能只用本地文件测试替代验收。

若后续需要重新验证实现，可在既有依赖、模型与构建条件满足时手动执行以下命令；它们不属于本次仅文档修改的已执行检查：

```bash
cd yolo_onnx_cpp
ctest --preset vcpkg-gcc15-release
```

或对已完成 OpenVINO 构建的目录执行：

```bash
cd yolo_onnx_cpp
ctest --preset vcpkg-gcc15-openvino
```

真实摄像头并发新鲜度、CPU 占用和长时间稳定性需要独立验收；不能把 `max_streams=4`、`max_result_age_ms=300` 或检测频率配置当作已经达到的性能结论。

### 10.3 本次文档校验范围

- 对照当前源码检查初始化顺序、默认值和模板值、路由字段、状态码、数据归属与释放逻辑。
- 将每段原始代码与指定源码行段逐字核对，检查源文件链接、行号和文内目录锚点。
- 检查命令块的 Bash 语法、JSON 示例及 SSE 示例数据体，未执行上传、创建流、删除或构建命令。
- 人工检查 Mermaid 图的节点、调用方向和主要状态路径；未通过图形渲染器做视觉验收。
- 只新增本文和更新文档索引；未修改现有 API、业务源码、依赖、配置、模型或实验结果。
