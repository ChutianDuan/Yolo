# 视频算法详细对比报告

测试日期：2026-07-10

## 1. 结论摘要

本次在 3 段原始 BDD100K 视频、共 3618 帧上，实测了 5 种当前 C++ 视频推理模式。所有质量指标均以全帧高分辨率 YOLO 输出作为伪标签，只表示算法间一致性，不代表真实准确率。

- **当前综合最优是 `low_dynamic_flow`**：整体 56.334 FPS，IoU=0.5 pooled F1=0.639451，在速度、precision 和 recall 之间最均衡。
- **集成 `integrated_high_low_flow` 召回最高但输出过量**：recall=0.868372，precision=0.504774；预测 59179 个框，而伪标签为 34400 个，预测/标签比为 1.720。
- **高分辨率动态/固定光流没有达到配置的 4 FPS 强检测目标**：实际模型调用率仅 1.272/1.197 FPS，导致 F1 分别只有 0.465163/0.442026。
- **全帧高分辨率 CPU 推理不具备实时性**：整体仅 3.091 FPS，三段总耗时 1170.437 秒。

推荐结论：当前若优先综合质量与实时性，应使用低分辨率动态光流；集成 High/Low 在修复重复/过量轨迹输出和补足可观测性前，不建议作为默认生产模式。

## 2. 测试条件与口径

| 项目 | 配置 |
| --- | --- |
| CPU | 2× Intel Xeon Gold 5218，64 逻辑核 |
| 后端 | ONNX Runtime CPU，`thread_num: 4` |
| 高分辨率模型 | `deploy/best.onnx`，输入 1280×736 |
| 低分辨率模型 | `deploy/best_640x384.onnx`，输入 640×384 |
| 阈值 | confidence 0.25，NMS IoU 0.45，letterbox |
| 光流模式 | 目标检测频率 4 FPS；dynamic/fixed 按模式设置；异步开启 |
| 质量匹配 | 按帧、按类别的一对一贪心 IoU 匹配，阈值 0.3/0.5/0.7 |
| 速度汇总 | 总帧数 ÷ 三段 HTTP 总耗时；不包含服务启动和视频渲染 |
| 资源/延迟汇总 | 三段视频相应指标的中位数 |

测试视频：

- `0000f77c-6257be58`：白天高速/常规车流，1217 帧，约 40.367 秒。
- `00268999-cb063914`：夜间城市/低照度，1194 帧，约 39.723 秒。
- `012fdff1-9d1d0d1d`：雨天/挡风玻璃干扰，1207 帧，约 40.202 秒。

## 3. VS Code 可直接播放的视频

全部视频均经 FFprobe 验证为 H.264、`yuv420p`、`faststart` MP4。六宫格布局依次为：原视频、全帧高分辨率、高分辨率动态、高分辨率固定、低分辨率动态、集成 High/Low。

| 场景 | 六宫格 | full_high | high_dynamic | high_fixed | low_dynamic | High/Low |
| --- | --- | --- | --- | --- | --- | --- |
| 白天高速/常规车流 | [六宫格](test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/all_modes_comparison_grid.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/full_high_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/high_dynamic_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/high_fixed_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/low_dynamic_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/integrated_high_low_flow_detections.mp4) |
| 夜间城市/低照度 | [六宫格](test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/all_modes_comparison_grid.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/full_high_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/high_dynamic_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/high_fixed_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/low_dynamic_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/integrated_high_low_flow_detections.mp4) |
| 雨天/挡风玻璃干扰 | [六宫格](test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/all_modes_comparison_grid.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/full_high_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/high_dynamic_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/high_fixed_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/low_dynamic_flow_detections.mp4) | [视频](test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/integrated_high_low_flow_detections.mp4) |

## 4. 总体性能

| 模式 | 总耗时(s) | pooled FPS | 相对全帧加速 | 模型调用 | 实际调用 FPS | 调用减少 | CPU中位数(%) | RSS中位数(MB) | E2E p95中位数(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 1170.437 | 3.091 | 1.000x | 3618 | 30.077 | 0.000 | 582.338 | 503.258 | 362.449 |
| high_dynamic_flow | 69.260 | 52.238 | 16.899x | 153 | 1.272 | 0.958 | 3060.477 | 501.793 | 22.471 |
| high_fixed_flow | 67.661 | 53.472 | 17.299x | 144 | 1.197 | 0.960 | 3103.583 | 501.859 | 21.315 |
| low_dynamic_flow | 64.224 | 56.334 | 18.224x | 406 | 3.375 | 0.888 | 3082.634 | 245.211 | 21.685 |
| integrated_high_low_flow | 115.429 | 31.344 | 10.140x | 561 | 4.664 | 0.845 | 1991.685 | 641.379 | 121.017 |

说明：进程 CPU 可超过 100%，表示使用多个逻辑核。异步模式的阶段耗时存在并发重叠，不能把各阶段总和直接当作墙钟耗时。

### 三段累计阶段耗时

| 模式 | preprocess(s) | infer(s) | postprocess(s) | optical flow(s) | tracker(s) | queue wait(s) | profiled(s) | wall(s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 20.921 | 1083.452 | 0.014 | 43.753 | 0.234 | 0.000 | 1148.405 | 1169.918 |
| high_dynamic_flow | 1.149 | 65.507 | 0.001 | 39.087 | 0.136 | 0.018 | 105.901 | 68.737 |
| high_fixed_flow | 1.076 | 64.157 | 0.001 | 38.496 | 0.132 | 0.005 | 103.868 | 67.110 |
| low_dynamic_flow | 1.220 | 49.404 | 0.002 | 36.479 | 0.138 | 0.022 | 87.271 | 63.681 |
| integrated_high_low_flow | 2.063 | 90.826 | 0.003 | 41.006 | 0.071 | 0.003 | 133.980 | 114.415 |

### 延迟分位数（三段相应分位数的中位数）

| 模式 | infer p50(ms) | infer p95(ms) | infer p99(ms) | E2E p50(ms) | E2E p95(ms) | E2E p99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 299.806 | 344.573 | 368.035 | 316.122 | 362.449 | 389.490 |
| high_dynamic_flow | 423.541 | 540.309 | 569.598 | 9.924 | 22.471 | 44.374 |
| high_fixed_flow | 440.113 | 553.021 | 575.979 | 10.399 | 21.315 | 33.904 |
| low_dynamic_flow | 113.874 | 152.759 | 176.461 | 10.073 | 21.685 | 41.151 |
| integrated_high_low_flow | 108.122 | 430.396 | 469.943 | 10.867 | 121.017 | 139.945 |

## 5. 总体质量一致性

### IoU=0.5 pooled 结果

| 模式 | precision | recall | F1 | mean matched IoU | matches | predictions | FP | FN |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| high_dynamic_flow | 0.499283 | 0.435407 | 0.465163 | 0.757446 | 14978 | 29999 | 15021 | 19422 |
| high_fixed_flow | 0.480717 | 0.409099 | 0.442026 | 0.754859 | 14073 | 29275 | 15202 | 20327 |
| low_dynamic_flow | 0.671962 | 0.609942 | 0.639451 | 0.786394 | 20982 | 31225 | 10243 | 13418 |
| integrated_high_low_flow | 0.504774 | 0.868372 | 0.638434 | 0.862202 | 29872 | 59179 | 29307 | 4528 |

### 不同 IoU 阈值

- `high_dynamic_flow`：IoU 0.3: P=0.645, R=0.562, F1=0.601；IoU 0.5: P=0.499, R=0.435, F1=0.465；IoU 0.7: P=0.310, R=0.270, F1=0.288
- `high_fixed_flow`：IoU 0.3: P=0.628, R=0.535, F1=0.578；IoU 0.5: P=0.481, R=0.409, F1=0.442；IoU 0.7: P=0.295, R=0.251, F1=0.271
- `low_dynamic_flow`：IoU 0.3: P=0.771, R=0.700, F1=0.733；IoU 0.5: P=0.672, R=0.610, F1=0.639；IoU 0.7: P=0.497, R=0.451, F1=0.473
- `integrated_high_low_flow`：IoU 0.3: P=0.526, R=0.905, F1=0.666；IoU 0.5: P=0.505, R=0.868, F1=0.638；IoU 0.7: P=0.448, R=0.771, F1=0.567

### 分场景 IoU=0.5

| 场景 | 模式 | precision | recall | F1 | predictions/labels |
| --- | --- | ---: | ---: | ---: | ---: |
| 白天高速/常规车流 | high_dynamic_flow | 0.434775 | 0.362523 | 0.395375 | 0.834 |
| 白天高速/常规车流 | high_fixed_flow | 0.415712 | 0.338559 | 0.373190 | 0.814 |
| 白天高速/常规车流 | low_dynamic_flow | 0.695296 | 0.563867 | 0.622722 | 0.811 |
| 白天高速/常规车流 | integrated_high_low_flow | 0.485888 | 0.804554 | 0.605875 | 1.656 |
| 夜间城市/低照度 | high_dynamic_flow | 0.542515 | 0.461323 | 0.498635 | 0.850 |
| 夜间城市/低照度 | high_fixed_flow | 0.522176 | 0.429419 | 0.471277 | 0.822 |
| 夜间城市/低照度 | low_dynamic_flow | 0.614704 | 0.595832 | 0.605121 | 0.969 |
| 夜间城市/低照度 | integrated_high_low_flow | 0.521178 | 0.925210 | 0.666763 | 1.775 |
| 雨天/挡风玻璃干扰 | high_dynamic_flow | 0.531136 | 0.511958 | 0.521371 | 0.964 |
| 雨天/挡风玻璃干扰 | high_fixed_flow | 0.515727 | 0.490152 | 0.502615 | 0.950 |
| 雨天/挡风玻璃干扰 | low_dynamic_flow | 0.725030 | 0.702931 | 0.713810 | 0.970 |
| 雨天/挡风玻璃干扰 | integrated_high_low_flow | 0.508517 | 0.885463 | 0.646025 | 1.741 |

### 分类别 F1（IoU=0.5）

| 类别 | high_dynamic | high_fixed | low_dynamic | High/Low |
| --- | ---: | ---: | ---: | ---: |
| 0:person | 0.000000 | 0.000000 | 0.000000 | 0.020833 |
| 2:car | 0.481612 | 0.459958 | 0.699798 | 0.664552 |
| 3:truck | 0.000000 | 0.000000 | 0.107692 | 0.168478 |
| 4:bus | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| 8:traffic light | 0.339964 | 0.336259 | 0.402767 | 0.482629 |
| 9:traffic sign | 0.454257 | 0.420629 | 0.555838 | 0.635217 |

## 6. 调度与跟踪代理指标

| 模式 | processed | async requests | corrections | skipped | dropped | empty frames | weak tracked | unique IDs | reappearances | mean lifetime(frames) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 3618 | 0 | 0 | 0 | 0 | 0 | 0 | 586 | 259 | 40.710 |
| high_dynamic_flow | 153 | 153 | 153 | 2309 | 2309 | 24 | 3442 | 454 | 29 | 62.110 |
| high_fixed_flow | 144 | 144 | 144 | 2469 | 2469 | 29 | 3445 | 442 | 25 | 62.687 |
| low_dynamic_flow | 406 | 406 | 406 | 610 | 610 | 10 | 3202 | 533 | 86 | 50.444 |
| integrated_high_low_flow | 448 | 113 | 113 | 0 | 0 | 0 | 3170 | 369 | 2 | 159.512 |

### 最终帧来源分布

| 模式 | detected | async_corrected | weak_tracked | interpolated | empty |
| --- | ---: | ---: | ---: | ---: | ---: |
| full_high | 3618 | 0 | 0 | 0 | 0 |
| high_dynamic_flow | 0 | 152 | 3442 | 0 | 24 |
| high_fixed_flow | 0 | 144 | 3445 | 0 | 29 |
| low_dynamic_flow | 0 | 406 | 3202 | 0 | 10 |
| integrated_high_low_flow | 448 | 0 | 3170 | 0 | 0 |

集成 High/Low 的 561 次模型调用由 113 次高分辨率异步请求和 448 次低分辨率处理构成；这是根据当前实现字段推导，服务没有直接返回拆分计数或拆分耗时。

## 7. 最差时间点（IoU=0.5）

以下列出每段视频、每个候选模式自动筛选的最差时间点之一；完整前 5 项保存在各场景 `comparison.json`。

| 场景 | 模式 | frame | time(s) | source | F1 | FP | FN |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: |
| 白天高速/常规车流 | high_dynamic_flow | 263 | 8.723 | async_corrected | 0.000000 | 13 | 13 |
| 白天高速/常规车流 | high_fixed_flow | 628 | 20.830 | async_corrected | 0.000000 | 12 | 14 |
| 白天高速/常规车流 | low_dynamic_flow | 427 | 14.163 | weak_tracked | 0.000000 | 10 | 12 |
| 白天高速/常规车流 | integrated_high_low_flow | 500 | 16.584 | weak_tracked | 0.142857 | 18 | 6 |
| 夜间城市/低照度 | high_dynamic_flow | 1 | 0.033 | empty | 0.000000 | 0 | 9 |
| 夜间城市/低照度 | high_fixed_flow | 1 | 0.033 | empty | 0.000000 | 0 | 9 |
| 夜间城市/低照度 | low_dynamic_flow | 1 | 0.033 | empty | 0.000000 | 0 | 9 |
| 夜间城市/低照度 | integrated_high_low_flow | 5 | 0.166 | weak_tracked | 0.260870 | 11 | 6 |
| 雨天/挡风玻璃干扰 | high_dynamic_flow | 6 | 0.200 | empty | 0.000000 | 0 | 16 |
| 雨天/挡风玻璃干扰 | high_fixed_flow | 6 | 0.200 | empty | 0.000000 | 0 | 16 |
| 雨天/挡风玻璃干扰 | low_dynamic_flow | 1 | 0.033 | empty | 0.000000 | 0 | 15 |
| 雨天/挡风玻璃干扰 | integrated_high_low_flow | 127 | 4.230 | weak_tracked | 0.200000 | 16 | 0 |

## 8. 当前问题与缺陷

### P0：旧 High/Low 对比产物不能作为当前结论

旧报告使用已画框的 `Readme/dynamic_onnx_flow_detections.mp4` 作为输入，并采用测试端后融合；旧视频还是 MPEG-4 Part 2。输入内容、算法实现和播放编码均不符合本次要求。本报告只使用原始 `.mov` 与集成 C++ endpoint。

### P1：高分辨率异步模式严重低于 4 FPS 调度目标

动态/固定模式三段合计只接受 153/144 次模型调用，实际为 1.272/1.197 FPS。对应 skipped=2309/2469、dropped=2309/2469。异步 worker 只有有限 pending 能力，模型速度低于调度频率时，大量请求被跳过，配置的 `video_detect_fps: 4` 并未兑现。

### P1：集成 High/Low 存在明显过量/重复轨迹输出

IoU=0.5 下，集成模式产生 59179 个预测，而基准只有 34400 个；FP=29307，precision=0.505。其 recall=0.868 很高，但高召回是以大量额外框为代价。结合 AuthorityTracker 同时保留稳定轨迹和低分辨率 provisional 轨迹的策略，当前需要优先检查重复抑制、provisional 晋升和轨迹过期规则。后一句属于基于代码与结果的定位推断，不是真值证明。

六宫格抽查提供了直接画面证据：白天 16.584 秒 `full_high` 为 7 条轨迹、High/Low 为 20 条；夜间 15.237 秒分别为 9/18 条，并出现跨画面边缘的大框；雨天 21.250 秒分别为 5/11 条。

### P1：弱跟踪帧存在框漂移、尺度膨胀与目标丢失

白天 16.584 秒高分辨率动态/固定模式分别只剩 2/1 条弱跟踪轨迹，而基准为 7 条；夜间 15.237 秒可以看到右侧车辆框和画面边缘框明显膨胀；雨天 21.250 秒在雨滴与模糊干扰下，多种光流模式的框位置和尺度偏离目标。该现象与 LK 光流只适合短间隔、当前强检测请求率不足相互叠加。

### P1：全帧高分辨率与集成模式均未稳定达到源视频实时帧率

全帧模式只有 3.091 FPS；集成模式整体 31.344 FPS，虽略高于三段约 30 FPS 的源帧率，但分场景中白天视频只有 28.421 FPS，仍可能在复杂场景积压。

### P2：High/Low 可观测性不足

服务响应只有合并推理耗时，`tracks_source` 也统一为 `detected`/`weak_tracked`，没有明确标识高分辨率检测、低分辨率检测或两者融合。当前无法可靠回答每类模型各自耗时、各自质量贡献和高/低检测帧来源。

### P2：异步阶段 timing 不能解释墙钟占比

集成模式三段 `profiled_stage_ms` 合计 133980.2 ms，而 `total_elapsed_ms` 合计 114414.5 ms；阶段耗时因并发重叠可大于墙钟时间。当前 `timing_ratio` 适合看累计计算量，不适合解释严格的端到端时间占比。

### P2：缺少视频检测/跟踪真值

precision、recall、F1 都是相对 `full_high` 的一致性指标，继承了高分辨率模型本身的漏检、误检和 ID 碎片。没有 MOT 真值时不能报告真实 mAP、MOTA、IDF1 或 HOTA，也不能把 unique ID 减少直接解释为身份保持更好。

## 9. 建议优先级

1. 先修复异步检测调度：区分“目标检测频率”和“worker 实际可承载频率”，至少暴露拒绝原因与实际请求率。
2. 对集成 High/Low 增加稳定轨迹与 provisional 轨迹之间的重复抑制，并对重叠输出做逐帧诊断。
3. 增加高/低分辨率独立调用次数、独立推理耗时和明确的 `tracks_source`。
4. 补充对应 BDD100K 检测/MOT 真值后，再决定生产默认算法；当前仅按伪标签结果，低分辨率动态光流是更稳妥的默认选择。

## 10. 产物与复现

- 聚合 JSON：[aggregate_comparison.json](test_outputs/video_compare/video_algorithm_report_20260710/aggregate_comparison.json)
- 白天高速/常规车流：[comparison.md](test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/comparison.md) / [comparison.json](test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/comparison.json)
- 夜间城市/低照度：[comparison.md](test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/comparison.md) / [comparison.json](test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/comparison.json)
- 雨天/挡风玻璃干扰：[comparison.md](test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/comparison.md) / [comparison.json](test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/comparison.json)
- 构建：`cd yolo_onnx_cpp && cmake --preset vcpkg-gcc15-release && cmake --build --preset vcpkg-gcc15-release`
- 测试：`ctest --test-dir build --output-on-failure`
- 对比脚本：`conda run -n yolo python yolo_onnx_cpp/test/compare_yolo_high_low_flow.py --video <raw.mov>`

限制：每段视频每种模式只做一次正式运行，结果用于当前机器和当前工作区版本的工程对比，不作为跨机器 benchmark。
