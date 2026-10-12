# 技术报告导航

本目录整合当前实现说明、专项调用链和历史工程证据。当前实现以源码为准；带日期的报告保留当时的数据、失败记录及尚未完成项，不视为当前版本重新验收。

## 推荐阅读顺序

1. [系统实现与验证综合报告](系统实现与验证综合报告.md)：项目能力、训练与量化、视频算法、多路资源、指标口径和验证边界。
2. [多路实时监测代码速读](多路实时监测代码速读记录_20260908.md)：从问题定位源码入口，快速阅读每路处理循环。
3. [初始化到多 IP 任务提交完整流程](初始化到多IP任务提交完整流程.md)：逐个查看初始化、HTTP 编排、线程、队列、SSE 与停止过程。
4. 需要复核性能或质量结论时，再进入下方对应的历史报告及 `test-results`。

日常启动与命令入口见[项目 README](../../readme.md)，模型量化命令见[PTQ/QAT 使用说明](../../model/quantization.md)。

## 按问题查阅

| 问题 | 当前实现说明 | 证据 / 补充材料 |
| --- | --- | --- |
| 模型怎样训练、蒸馏和量化？ | [综合报告：模型流程](系统实现与验证综合报告.md#model-pipeline) | [量化脚本说明](../../model/quantization.md)、[训练脚本](../../model/train.py) |
| High/Low、光流和跟踪器怎样分工？ | [综合报告：视频算法](系统实现与验证综合报告.md#tracking) | [初期算法对比](video_algorithm_comparison_20260710.md) |
| 检测和光流在同一帧是否重复老化？ | [综合报告：跟踪时钟](系统实现与验证综合报告.md#tracking-clock) | [StreamProcessor 回归](../../yolo_onnx_cpp/test_cpp/stream_processor_test.cpp) |
| ROI 和分类别阈值具体怎样生效？ | [综合报告：质量控制](系统实现与验证综合报告.md#quality-controls) | [AuthorityTracker 回归](../../yolo_onnx_cpp/test_cpp/authority_tracker_policy_test.cpp) |
| 多路推理为什么完成了却没有输出？ | [综合报告：回放与指标](系统实现与验证综合报告.md#replay) | [2026-09-28 逐流核算](../test-results/multistream_local_20260928/README.md) |
| 一路最多有几个任务？队列满时怎样处理？ | [综合报告：资源与调度](系统实现与验证综合报告.md#scheduling) | [完整流程第 6–7 节](初始化到多IP任务提交完整流程.md#camera-ips) |
| SSE 断线、DELETE、重连怎样工作？ | [完整流程：生命周期](初始化到多IP任务提交完整流程.md#lifecycle) | [实时流管理器回归](../../yolo_onnx_cpp/test_cpp/realtime_stream_manager_test.cpp) |
| 耗时、FPS、CPU、RSS 应怎样解读？ | [综合报告：测量口径](系统实现与验证综合报告.md#measurement) | [分阶段实验](../test-results/high_stage_cpu_20261010/README.md) |
| 四路、六路、八路是否已经验收？ | [综合报告：已有证据](系统实现与验证综合报告.md#evidence) | [实施记录定位表](#history-map) |

## 历史报告与专项证据

| 材料 | 保留用途 | 阅读边界 |
| --- | --- | --- |
| [代码审核报告（2026-07-10）](代码审核报告_20260710.md) | 当时修复项、构建及测试快照 | 单个 OpenVINO 请求加锁、Qt 依赖缺失等属于当时状态 |
| [视频算法对比（2026-07-10）](video_algorithm_comparison_20260710.md) | 五种视频策略的初期性能与伪标签一致性 | 不能把初期 High/Low 与后续优化回归混为同一次实验 |
| [优化计划](优化计划.md) | 改造原因、原始验收目标与历次决策 | 早期“尚未实现”和“下一项”按历史时点理解 |
| [多路实施记录](多路实时监测实施记录_20260905.md) | 实现演进、控制变量、失败与收尾证据 | 截止第 75.1 节只记录候选启动，不证明目前仍有任务运行 |
| [CPU 后端对比（2026-07-09）](onnx_openvino_cpu_benchmark_20260709.md) | 较早的后端与 Python 基准 | C++ 路径优先参考后续同链路线程基准 |
| [C++ 后端线程基准（2026-09-03）](../test-results/onnx_thread_benchmark/20260903_020914_utc/combined_report.md) | 单实例延迟与尾延迟对照 | 不直接决定多路线程、请求池或容量 |
| [High FP32/INT8 + LK（2026-10-08）](../test-results/high_fp32_int8_lk_20261008/report.md) | 文件大小、内存、性能与真值精度对照 | 同步单模型，使用跟踪时钟修复前代码 |
| [分阶段计时（2026-10-10）](../test-results/high_stage_cpu_20261010/README.md) | 模型、LK、解码等成本拆分 | 同步固定 stride，原始资产部分仅存本地 |
| [High/Low 调用栈分析](../profiling/high_low_20261008/README.md) | 阅读模型和光流的调用热点 | GDB 栈快照占比不是 perf CPU 时间占比 |

<a id="history-map"></a>
## 实施记录定位表

| 主题 | 历史章节 | 版本化入口 |
| --- | --- | --- |
| 六路八小时与采集缺口 | 第 63–64 节 | [2026-09-22 记录](../test-results/multistream_local_20260922/README.md) |
| 中断窗口与独立会话收尾 | 第 65–66 节 | [2026-09-23](../test-results/multistream_local_20260923/README.md)、[2026-09-24](../test-results/multistream_local_20260924/README.md) |
| 四路八小时与八路短窗 | [第 67 节](多路实时监测实施记录_20260905.md#local-capacity) | [2026-09-27 终态](../test-results/multistream_local_20260927/README.md) |
| CPU 亲和性、高低线程与请求池 | 第 68–72 节 | [节点与线程对照](../test-results/multistream_local_20260928/README.md) |
| 完成、应用、过期与发布差额 | [第 73–74 节](多路实时监测实施记录_20260905.md#result-losses) | [逐流损失与低模线程对照](../test-results/multistream_local_20260928/README.md) |
| 最新已记录候选启动 | [第 75.1 节](多路实时监测实施记录_20260905.md#last-recorded-step) | 未归档完整终态，不能补写成通过 |

历史记录中的本地 `test_outputs` 路径可能不在干净检出中。复核时先使用版本管理中的汇总和审计文件，需要逐帧数据、模型或完整日志时再确认本地资产是否存在。
