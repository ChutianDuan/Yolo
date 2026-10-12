# VisionTrack 文档索引

`docs/` 保存适合阅读和版本管理的文档资产。测试源码位于 `yolo_onnx_cpp/test/`、`yolo_onnx_cpp/test_cpp/`、`model/test_training_quality_pipeline.py` 及 `model/test_quantization.py`，新的运行产物仍写入明确的新实验目录。

当前项目效果和运行入口见根目录 [README](../readme.md)。实现细节统一在综合报告中说明，历史实验保留当时的条件和结论，不代表当前版本重新验收。

## 当前实现与阅读入口

- [系统实现与验证综合报告](reports/系统实现与验证综合报告.md)：训练量化、视频算法、跟踪时钟、ROI、调度回放、指标及证据边界。
- [报告导航](reports/README.md)：按问题检索，历史报告与实施记录的主题映射。
- [多路实时监测代码速读](reports/多路实时监测代码速读记录_20260908.md)：从每路处理循环定位源码。
- [初始化到多 IP 任务提交完整流程（源码精读）](reports/初始化到多IP任务提交完整流程.md)
- [PTQ/QAT INT8 使用说明](../model/quantization.md)：脚本、输出与公平对比条件。

## 历史决策与审核

- [优化计划与历史决策](reports/优化计划.md)
- [CPU 多路实施记录](reports/多路实时监测实施记录_20260905.md)：时间线、控制变量、失败与终态证据。
- [代码审核快照（2026-07-10）](reports/代码审核报告_20260710.md)

算法和后端历史报告的条件、用途与读取边界见[报告导航](reports/README.md)。

## 测试结果

- [C++ ONNX Runtime / OpenVINO 双模型线程对比（2026-09-03）](test-results/onnx_thread_benchmark/20260903_020914_utc/combined_report.md)
- [High FP32 / INT8 + LK 对比（2026-10-08）](test-results/high_fp32_int8_lk_20261008/report.md)
- [High + LK 分阶段耗时与 CPU 实验（2026-10-10）](test-results/high_stage_cpu_20261010/README.md)
- [本地多路线程与请求池对照（2026-09-28）](test-results/multistream_local_20260928/README.md)
- [构建与 CTest 验证快照](test-results/build_verification_20260710/)
- [High/Low 初期对比](test-results/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/comparison.md)
- [三场景算法报告](test-results/video_compare/video_algorithm_report_20260710/)
- [High/Low 优化回归](test-results/video_compare/video_algorithm_optimization_20260710/)

这里的 JSON、日志和 CTest 文件是历史快照，不是当前服务运行时读取的数据。新增大型视频、原始响应和临时实验输出应放在 `yolo_onnx_cpp/test_outputs/`。本地可能存在未纳入 Git 的实验目录；实施记录引用这些目录时，不代表干净检出后也能取得全部证据。

10 月 High + LK 实验使用跟踪时钟修复前的代码，当前输出可能不同；复现前请阅读实验说明中的源码与本地资产要求。

## 性能分析

- [High/Low 调用栈采样与耗时分析（2026-10-08）](profiling/high_low_20261008/README.md)

## 资源

- [Web 工作台截图](assets/visiontrack-workbench-overview.png)
- [预测视频第 360 帧预览](assets/visiontrack-video-frame-360.jpg)：截自仓库已有演示视频约第 12 秒，包含轨迹 ID、类别与分数。
