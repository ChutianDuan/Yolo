# 大尺寸模型（1280×736） ONNX Runtime 线程数性能测试报告

## 结论

本机测试中，SetIntraOpNumThreads(16) 最快：纯 ONNX 推理平均 137.14 ms，P95 166.61 ms，等效串行吞吐 7.29 FPS，相对 1 线程加速 6.87x。

当前配置 4 线程的均值为 297.58 ms；最优配置相对它降低 53.9% 的推理耗时。

该结论适用于本报告记录的双路 CPU 和单实例、batch=1、同步推理场景。若服务同时运行多个推理实例，应再做并发压测，因为单实例最优线程数不一定能带来最高总吞吐。

## 测试对象

- 模型：/home/ubuntu/YOLO/yolo_onnx_cpp/deploy/best.onnx
- 模型大小：36.62 MiB
- SHA-256：96ecf0117bc10499c611fbabc38dbf92457bfaec53b9e099fe67041cbb26fc59
- 输入张量：[1, 3, 736, 1280]
- 测试视频：/home/ubuntu/YOLO/Readme/dynamic_onnx_flow_detections.mp4（首帧固定复用）
- ONNX Runtime：1.23.2

## 测试方法

每个线程数使用独立进程创建 YoloEngine，先预热 10 次，再测量 50 次；共 3 轮，正式样本数为 150。各任务以固定随机种子 20260903 交错执行，以减弱运行顺序和 CPU 温度的系统性影响。未设置 CPU 亲和性或 NUMA 绑核。输入帧只预处理一次，不计入耗时。

纯 ONNX 是 Ort::Session::Run() 的耗时；完整 infer 还包括输入 Tensor 创建、输出 shape 读取、decode 和 NMS。模型加载及预热均不计入下表。CPU 利用率以单进程口径统计，100% 约等于占满一个逻辑 CPU。

## 结果

| Intra-op 线程 | 纯 ONNX 均值 (ms) | 中位数 (ms) | P95 (ms) | 标准差 (ms) | 等效 FPS | 相对基线 | 完整 infer 均值 (ms) | CPU 利用率 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 941.55 | 946.69 | 981.66 | 25.59 | 1.06 | 1.00x | 941.57 | 99.9% |
| 2 | 481.54 | 472.23 | 539.93 | 24.30 | 2.08 | 1.96x | 481.56 | 199.6% |
| 4 | 297.58 | 292.09 | 332.83 | 19.37 | 3.36 | 3.16x | 297.60 | 398.8% |
| 8 | 186.42 | 183.34 | 215.42 | 14.43 | 5.36 | 5.05x | 186.44 | 797.8% |
| 16 | 137.14 | 130.94 | 166.61 | 15.89 | 7.29 | 6.87x | 137.16 | 1593.4% |

## 测试环境

- 时间：2026-09-03T01:49:30.507800+00:00
- 主机：Linux-5.15.0-139-generic-x86_64-with-glibc2.31
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑 CPU：64；物理核心：32
- 仓库提交：f7bd994b7a2dfa1b564f183e453e0d4ea2c92c33
- 工作区状态：M model/mine_hard_samples.py;  M model/train.py;  M yolo_onnx_cpp/CMakeLists.txt; ?? yolo_onnx_cpp/tools/onnx_thread_benchmark.cpp; ?? yolo_onnx_cpp/tools/run_onnx_thread_benchmark.py

完整逐次样本见同目录 raw_results.json，汇总数据见 results.csv，环境元数据见 environment.json。
