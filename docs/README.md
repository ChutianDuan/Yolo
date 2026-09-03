# VisionTrack 文档索引

`docs/` 只保存适合阅读和版本管理的文档资产。测试源码仍位于 `yolo_onnx_cpp/test/` 和 `yolo_onnx_cpp/test_cpp/`，新的运行产物仍写入 `yolo_onnx_cpp/test_outputs/`。

## 正式报告

- [代码审核报告（2026-07-10）](reports/代码审核报告_20260710.md)
- [High/Low 重复框与光流漂移优化计划](reports/优化计划.md)
- [视频算法对比报告（2026-07-10）](reports/video_algorithm_comparison_20260710.md)
- [C++ ONNX Runtime / OpenVINO 双模型线程性能对比（2026-09-03）](test-results/onnx_thread_benchmark/20260903_020914_utc/combined_report.md)
- [ONNX Runtime / OpenVINO CPU 对比（历史，2026-07-09）](reports/onnx_openvino_cpu_benchmark_20260709.md)

## 测试结果

- [构建与 CTest 验证快照](test-results/build_verification_20260710/)
- [High/Low 初期对比](test-results/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/comparison.md)
- [三场景算法报告](test-results/video_compare/video_algorithm_report_20260710/)
- [High/Low 优化回归](test-results/video_compare/video_algorithm_optimization_20260710/)

这里的 JSON、日志和 CTest 文件是历史快照，不是当前服务运行时读取的数据。大型视频、原始响应和临时实验输出不进入 `docs/`。

## 资源

- [Web 工作台截图](assets/visiontrack-workbench-overview.png)
