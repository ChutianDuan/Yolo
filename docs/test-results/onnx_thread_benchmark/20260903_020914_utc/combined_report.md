# C++ ONNX Runtime 与 OpenVINO 线程性能对比报告

## 结论

- 大尺寸模型（1280×736）：OpenVINO 16 线程最快，平均 91.98 ms，P95 146.99 ms，等效 10.87 FPS。相对另一后端各自最优配置，速度为 1.49x、延迟降低 33.0%。
- 小尺寸模型（640×384）：OpenVINO 16 线程最快，平均 22.90 ms，P95 34.48 ms，等效 43.66 FPS。相对另一后端各自最优配置，速度为 2.02x、延迟降低 50.5%。
- 大尺寸模型（1280×736）尾延迟：OpenVINO 8 线程 P95 114.28 ms，低于 16 线程的 146.99 ms；需要稳定尾延迟时，8 线程是更保守的候选。
- 小尺寸模型（640×384）尾延迟：OpenVINO 8 线程 P95 32.91 ms，低于 16 线程的 34.48 ms；需要稳定尾延迟时，8 线程是更保守的候选。

本结论针对单实例、batch=1、同步 CPU 推理。当前 High/Low 模式可能并行运行两个 Session，并发请求也会共享 CPU，因此生产配置还需结合并发吞吐测试，不能仅按单实例最低延迟选择线程数。

### 最优配置汇总

| 模型 | ONNX Runtime 最优 | OpenVINO 最优 | 全局最快 | 最快后端优势 |
|---|---:|---:|---:|---:|
| 大尺寸模型（1280×736） | 137.38 ms (16 线程) | 91.98 ms (16 线程) | OpenVINO 16 线程 | 1.49x |
| 小尺寸模型（640×384） | 46.23 ms (16 线程) | 22.90 ms (16 线程) | OpenVINO 16 线程 | 2.02x |

### 当前 thread_num=4 的同线程对比

| 模型 | ONNX Runtime (ms) | OpenVINO (ms) | 更快后端 | 加速比 |
|---|---:|---:|---:|---:|
| 大尺寸模型（1280×736） | 274.02 | 189.54 | OpenVINO | 1.45x |
| 小尺寸模型（640×384） | 81.99 | 47.52 | OpenVINO | 1.73x |

## 测试方法

被测路径全部是 C++：OpenCV 读取和预处理首帧，YoloEngine 创建后端并执行推理，decode 与 NMS 也使用项目 C++ 实现。Python 脚本只负责启动隔离进程、交错任务顺序和生成 JSON/CSV/Markdown，不加载模型，也不进入计时区间。

两个后端直接读取完全相同的 ONNX 文件，不使用 Python Runtime，也不使用预先转换的 OpenVINO IR。thread_num 对 ONNX Runtime 映射为 SetIntraOpNumThreads，对 OpenVINO 映射为 ov::inference_num_threads。

每个“模型 × 后端 × 线程数”组合都使用独立进程，先预热 10 次，再测量 50 次，共 3 轮；每个组合 150 个正式样本，总计 3000 个样本。任务使用固定随机种子 20260903 交错执行。未设置 CPU 亲和性或 NUMA 绑核，输入帧只预处理一次，模型加载与预热均不计入结果。

纯后端推理分别统计 Ort::Session::Run() 与 ov::InferRequest::infer()；完整 infer 还包含输入 Tensor 准备、输出 shape、decode 和 NMS。CPU 利用率为单进程口径，100% 约等于占满一个逻辑 CPU。

## 测试对象与环境

- 测试视频：/home/ubuntu/YOLO/Readme/dynamic_onnx_flow_detections.mp4（首帧固定复用）
- ONNX Runtime：1.23.2
- OpenVINO：2026.1.0-000--
- 大尺寸模型（1280×736）：/home/ubuntu/YOLO/yolo_onnx_cpp/deploy/best.onnx，36.62 MiB，输入 [1, 3, 736, 1280]，SHA-256 96ecf0117bc10499c611fbabc38dbf92457bfaec53b9e099fe67041cbb26fc59
- 小尺寸模型（640×384）：/home/ubuntu/YOLO/yolo_onnx_cpp/deploy/best_640x384.onnx，36.35 MiB，输入 [1, 3, 384, 640]，SHA-256 439a56ee159a52ffafdce6befa4e8486c8e311f8d6095ffb9cbbad0443fdfcbf
- 时间：2026-09-03T02:22:53.612811+00:00
- 主机：Linux-5.15.0-139-generic-x86_64-with-glibc2.31
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑 CPU：64；物理核心：32
- 仓库提交：f7bd994b7a2dfa1b564f183e453e0d4ea2c92c33
- 工作区状态：M model/mine_hard_samples.py;  M model/train.py;  M yolo_onnx_cpp/CMakeLists.txt;  M yolo_onnx_cpp/model/yolo_engine.cpp; ?? docs/test-results/onnx_thread_benchmark/; ?? yolo_onnx_cpp/tools/onnx_thread_benchmark.cpp; ?? yolo_onnx_cpp/tools/run_onnx_thread_benchmark.py

## 大尺寸模型（1280×736）详细结果

| 后端 | 线程 | 纯后端均值 (ms) | 中位数 (ms) | P95 (ms) | 标准差 (ms) | 等效 FPS | 相对本后端 1 线程 | 完整 infer (ms) | CPU |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ONNX Runtime | 1 | 935.66 | 941.88 | 982.22 | 26.37 | 1.07 | 1.00x | 935.68 | 99.9% |
| ONNX Runtime | 2 | 475.13 | 465.47 | 511.04 | 19.80 | 2.10 | 1.97x | 475.15 | 199.6% |
| ONNX Runtime | 4 | 274.02 | 271.18 | 297.84 | 13.92 | 3.65 | 3.41x | 274.04 | 399.2% |
| ONNX Runtime | 8 | 192.89 | 191.11 | 222.16 | 14.12 | 5.18 | 4.85x | 192.91 | 797.6% |
| ONNX Runtime | 16 | 137.38 | 129.65 | 179.88 | 18.86 | 7.28 | 6.81x | 137.40 | 1593.8% |
| OpenVINO | 1 | 687.16 | 693.78 | 718.04 | 22.42 | 1.46 | 1.00x | 690.24 | 99.7% |
| OpenVINO | 2 | 352.12 | 351.69 | 374.66 | 12.79 | 2.84 | 1.95x | 355.01 | 199.0% |
| OpenVINO | 4 | 189.54 | 189.02 | 199.38 | 5.57 | 5.28 | 3.63x | 192.15 | 396.1% |
| OpenVINO | 8 | 108.15 | 107.14 | 114.28 | 3.65 | 9.25 | 6.35x | 110.85 | 786.5% |
| OpenVINO | 16 | 91.98 | 77.12 | 146.99 | 29.76 | 10.87 | 7.47x | 94.71 | 1402.8% |

## 小尺寸模型（640×384）详细结果

| 后端 | 线程 | 纯后端均值 (ms) | 中位数 (ms) | P95 (ms) | 标准差 (ms) | 等效 FPS | 相对本后端 1 线程 | 完整 infer (ms) | CPU |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ONNX Runtime | 1 | 233.04 | 234.62 | 250.10 | 10.65 | 4.29 | 1.00x | 233.06 | 99.8% |
| ONNX Runtime | 2 | 129.03 | 122.98 | 151.07 | 14.30 | 7.75 | 1.81x | 129.05 | 199.4% |
| ONNX Runtime | 4 | 81.99 | 81.46 | 89.79 | 5.57 | 12.20 | 2.84x | 82.01 | 399.0% |
| ONNX Runtime | 8 | 56.13 | 55.06 | 65.43 | 6.79 | 17.82 | 4.15x | 56.15 | 797.4% |
| ONNX Runtime | 16 | 46.23 | 42.66 | 59.62 | 8.49 | 21.63 | 5.04x | 46.25 | 1592.6% |
| OpenVINO | 1 | 171.21 | 168.03 | 187.63 | 10.74 | 5.84 | 1.00x | 172.01 | 99.1% |
| OpenVINO | 2 | 88.81 | 87.57 | 94.82 | 3.84 | 11.26 | 1.93x | 89.53 | 199.2% |
| OpenVINO | 4 | 47.52 | 46.96 | 52.25 | 2.04 | 21.04 | 3.60x | 48.23 | 397.9% |
| OpenVINO | 8 | 28.65 | 28.20 | 32.91 | 3.12 | 34.90 | 5.98x | 29.36 | 790.5% |
| OpenVINO | 16 | 22.90 | 18.81 | 34.48 | 7.09 | 43.66 | 7.48x | 23.63 | 1522.6% |

完整逐次样本见同目录 raw_results.json，汇总数据见 results.csv，环境元数据见 environment.json。
