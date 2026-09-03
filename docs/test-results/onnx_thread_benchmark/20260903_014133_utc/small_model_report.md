# 小尺寸模型（640×384） ONNX Runtime 线程数性能测试报告

## 结论

本机测试中，SetIntraOpNumThreads(16) 最快：纯 ONNX 推理平均 44.21 ms，P95 52.61 ms，等效串行吞吐 22.62 FPS，相对 1 线程加速 5.21x。

当前配置 4 线程的均值为 82.21 ms；最优配置相对它降低 46.2% 的推理耗时。

该结论适用于本报告记录的双路 CPU 和单实例、batch=1、同步推理场景。若服务同时运行多个推理实例，应再做并发压测，因为单实例最优线程数不一定能带来最高总吞吐。

## 测试对象

- 模型：/home/ubuntu/YOLO/yolo_onnx_cpp/deploy/best_640x384.onnx
- 模型大小：36.35 MiB
- SHA-256：439a56ee159a52ffafdce6befa4e8486c8e311f8d6095ffb9cbbad0443fdfcbf
- 输入张量：[1, 3, 384, 640]
- 测试视频：/home/ubuntu/YOLO/Readme/dynamic_onnx_flow_detections.mp4（首帧固定复用）
- ONNX Runtime：1.23.2

## 测试方法

每个线程数使用独立进程创建 YoloEngine，先预热 10 次，再测量 50 次；共 3 轮，正式样本数为 150。各任务以固定随机种子 20260903 交错执行，以减弱运行顺序和 CPU 温度的系统性影响。未设置 CPU 亲和性或 NUMA 绑核。输入帧只预处理一次，不计入耗时。

纯 ONNX 是 Ort::Session::Run() 的耗时；完整 infer 还包括输入 Tensor 创建、输出 shape 读取、decode 和 NMS。模型加载及预热均不计入下表。CPU 利用率以单进程口径统计，100% 约等于占满一个逻辑 CPU。

## 结果

| Intra-op 线程 | 纯 ONNX 均值 (ms) | 中位数 (ms) | P95 (ms) | 标准差 (ms) | 等效 FPS | 相对基线 | 完整 infer 均值 (ms) | CPU 利用率 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 230.31 | 228.75 | 245.34 | 8.34 | 4.34 | 1.00x | 230.33 | 99.8% |
| 2 | 124.15 | 123.21 | 141.13 | 9.20 | 8.06 | 1.86x | 124.17 | 199.1% |
| 4 | 82.21 | 82.69 | 89.17 | 7.38 | 12.16 | 2.80x | 82.23 | 399.1% |
| 8 | 54.34 | 53.20 | 62.43 | 5.80 | 18.40 | 4.24x | 54.36 | 797.9% |
| 16 | 44.21 | 41.74 | 52.61 | 8.09 | 22.62 | 5.21x | 44.23 | 1596.2% |

## 测试环境

- 时间：2026-09-03T01:49:30.507800+00:00
- 主机：Linux-5.15.0-139-generic-x86_64-with-glibc2.31
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑 CPU：64；物理核心：32
- 仓库提交：f7bd994b7a2dfa1b564f183e453e0d4ea2c92c33
- 工作区状态：M model/mine_hard_samples.py;  M model/train.py;  M yolo_onnx_cpp/CMakeLists.txt; ?? yolo_onnx_cpp/tools/onnx_thread_benchmark.cpp; ?? yolo_onnx_cpp/tools/run_onnx_thread_benchmark.py

完整逐次样本见同目录 raw_results.json，汇总数据见 results.csv，环境元数据见 environment.json。
