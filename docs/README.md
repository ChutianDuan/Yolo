# VisionTrack 文档索引

`docs/` 只保存适合阅读和版本管理的文档资产。测试源码位于 `yolo_onnx_cpp/test/`、`yolo_onnx_cpp/test_cpp/` 及 `model/test_training_quality_pipeline.py`，新的运行产物仍写入 `yolo_onnx_cpp/test_outputs/`。

运行说明与历史报告应区分阅读：根目录 [README](../readme.md)、速读记录及完整流程已于 2026-09-29 对照源码同步；优化计划保留历史分析并注明当前实现，带日期的实验数据不代表重新验收。

## 正式报告

- [多路实时监测代码速读（2026-09-29 同步）](reports/多路实时监测代码速读记录_20260908.md)
- [初始化到多 IP 任务提交完整流程（源码精读）](reports/初始化到多IP任务提交完整流程.md)
- [代码审核报告（2026-07-10）](reports/代码审核报告_20260710.md)
- [High/Low 重复框与光流漂移优化计划](reports/优化计划.md)
- [CPU 多路实时监测实施记录（2026-09-05）](reports/多路实时监测实施记录_20260905.md)
- [视频算法对比报告（2026-07-10）](reports/video_algorithm_comparison_20260710.md)
- [C++ ONNX Runtime / OpenVINO 双模型线程性能对比（2026-09-03）](test-results/onnx_thread_benchmark/20260903_020914_utc/combined_report.md)
- [ONNX Runtime / OpenVINO CPU 对比（历史，2026-07-09）](reports/onnx_openvino_cpu_benchmark_20260709.md)

## 测试结果

- [构建与 CTest 验证快照](test-results/build_verification_20260710/)
- [High/Low 初期对比](test-results/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/comparison.md)
- [三场景算法报告](test-results/video_compare/video_algorithm_report_20260710/)
- [High/Low 优化回归](test-results/video_compare/video_algorithm_optimization_20260710/)

这里的 JSON、日志和 CTest 文件是历史快照，不是当前服务运行时读取的数据。新增大型视频、原始响应和临时实验输出应放在 `yolo_onnx_cpp/test_outputs/`。本地可能存在未纳入 Git 的实验目录；实施记录引用这些目录时，不代表干净检出后也能取得全部证据。

## 资源

- [Web 工作台截图](assets/visiontrack-workbench-overview.png)
