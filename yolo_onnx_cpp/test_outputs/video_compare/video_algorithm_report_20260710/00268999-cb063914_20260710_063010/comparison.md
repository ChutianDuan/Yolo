# 五种视频推理模式对比

- 原始视频：`/home/ubuntu/YOLO/datasets/bdd100k_tracking_video/bdd100k_videos_train_00/bdd100k/videos/train/00268999-cb063914.mov`
- 输出目录：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010`
- 质量参照：`full_high` 全帧高分辨率结果（伪标签，不是真值）。
- 后端：ONNX Runtime CPU；4 线程；高/低分辨率置信度阈值均为 0.25。

## 运行摘要

| 模式 | endpoint | stride | async | 帧数 | 模型调用(推导) | 耗时(s) | FPS | CPU(%) | RSS(MB) | 唯一ID |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | `/infer_video` | full_model | False | 1194 | 1194 | 384.609 | 3.104 | 580.514 | 503.258 | 153 |
| high_dynamic_flow | `/infer_video` | async_dynamic | True | 1194 | 40 | 19.430 | 61.451 | 3358.458 | 501.078 | 127 |
| high_fixed_flow | `/infer_video` | async_fixed | True | 1194 | 33 | 17.132 | 69.694 | 3525.547 | 501.859 | 122 |
| low_dynamic_flow | `/infer_video` | async_dynamic | True | 1194 | 112 | 16.971 | 70.355 | 3476.294 | 245.211 | 163 |
| integrated_high_low_flow | `/infer_video_high_low` | async_high_low_dynamic | True | 1194 | 188 | 35.510 | 33.624 | 2066.026 | 631.609 | 133 |

## 阶段耗时与延迟

| 模式 | infer总计(ms) | preprocess(ms) | postprocess(ms) | flow(ms) | tracker(ms) | E2E p50(ms) | E2E p95(ms) | E2E p99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 358571.345 | 7101.094 | 4.584 | 12811.746 | 78.352 | 316.122 | 362.449 | 390.171 |
| high_dynamic_flow | 18561.412 | 288.561 | 0.256 | 11107.906 | 46.678 | 8.527 | 19.520 | 34.137 |
| high_fixed_flow | 16351.399 | 228.507 | 0.257 | 10198.434 | 40.525 | 7.490 | 18.938 | 33.904 |
| low_dynamic_flow | 14659.865 | 315.457 | 0.657 | 9675.178 | 47.652 | 7.171 | 18.294 | 32.234 |
| integrated_high_low_flow | 30870.127 | 691.452 | 0.927 | 12299.872 | 25.404 | 9.517 | 121.017 | 138.935 |

## 相对全帧基准的性能

| 模式 | 加速比 | 节省耗时(s) | 减少处理帧 | 处理帧减少比例 |
| --- | ---: | ---: | ---: | ---: |
| high_dynamic_flow | 19.795x | 365.179 | 1154 | 0.966 |
| high_fixed_flow | 22.450x | 367.477 | 1161 | 0.972 |
| low_dynamic_flow | 22.663x | 367.638 | 1082 | 0.906 |
| integrated_high_low_flow | 10.831x | 349.099 | 1044 | 0.874 |

## 质量一致性：high_dynamic_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.656674 | 0.558397 | 0.603561 | 0.738024 | 6966 | 12475 | 10608 | 3642 | 5509 |
| 0.500 | 0.542515 | 0.461323 | 0.498635 | 0.809510 | 5755 | 12475 | 10608 | 4853 | 6720 |
| 0.700 | 0.394608 | 0.335551 | 0.362691 | 0.882188 | 4186 | 12475 | 10608 | 6422 | 8289 |

## 质量一致性：high_fixed_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.639633 | 0.526012 | 0.577285 | 0.724359 | 6562 | 12475 | 10259 | 3697 | 5913 |
| 0.500 | 0.522176 | 0.429419 | 0.471277 | 0.797156 | 5357 | 12475 | 10259 | 4902 | 7118 |
| 0.700 | 0.353641 | 0.290822 | 0.319170 | 0.886114 | 3628 | 12475 | 10259 | 6631 | 8847 |

## 质量一致性：low_dynamic_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.702448 | 0.680882 | 0.691497 | 0.768603 | 8494 | 12475 | 12092 | 3598 | 3981 |
| 0.500 | 0.614704 | 0.595832 | 0.605121 | 0.820883 | 7433 | 12475 | 12092 | 4659 | 5042 |
| 0.700 | 0.505954 | 0.490421 | 0.498067 | 0.868195 | 6118 | 12475 | 12092 | 5974 | 6357 |

## 质量一致性：integrated_high_low_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.535537 | 0.950701 | 0.685133 | 0.866698 | 11860 | 12475 | 22146 | 10286 | 615 |
| 0.500 | 0.521178 | 0.925210 | 0.666763 | 0.879460 | 11542 | 12475 | 22146 | 10604 | 933 |
| 0.700 | 0.478551 | 0.849539 | 0.612230 | 0.902794 | 10598 | 12475 | 22146 | 11548 | 1877 |

## 跟踪代理指标

| 模式 | observations | unique IDs | segments | reappearances | mean lifetime | median lifetime | p95 lifetime |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 12475 | 153 | 226 | 73 | 55.199 | 10.000 | 250 |
| high_dynamic_flow | 10608 | 127 | 134 | 7 | 79.164 | 29.000 | 473 |
| high_fixed_flow | 10259 | 122 | 126 | 4 | 81.421 | 34.500 | 482 |
| low_dynamic_flow | 12092 | 163 | 199 | 36 | 60.764 | 15.000 | 239 |
| integrated_high_low_flow | 22146 | 133 | 134 | 1 | 165.269 | 123.000 | 507 |

## IoU=0.5 最差时间点

### high_dynamic_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.033 | empty | 0.000000 | 0 | 9 | 9 | 0 |
| 458 | 15.237 | weak_tracked | 0.250000 | 4 | 8 | 10 | 6 |
| 519 | 17.267 | weak_tracked | 0.260870 | 5 | 12 | 15 | 8 |
| 633 | 21.059 | weak_tracked | 0.260870 | 8 | 9 | 12 | 11 |
| 384 | 12.775 | weak_tracked | 0.285714 | 7 | 8 | 11 | 10 |

### high_fixed_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.033 | empty | 0.000000 | 0 | 9 | 9 | 0 |
| 659 | 21.924 | weak_tracked | 0.100000 | 9 | 9 | 10 | 10 |
| 531 | 17.666 | weak_tracked | 0.173913 | 5 | 14 | 16 | 7 |
| 161 | 5.356 | weak_tracked | 0.250000 | 9 | 15 | 19 | 13 |
| 458 | 15.237 | weak_tracked | 0.250000 | 4 | 8 | 10 | 6 |

### low_dynamic_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.033 | empty | 0.000000 | 0 | 9 | 9 | 0 |
| 112 | 3.726 | async_corrected | 0.258065 | 12 | 11 | 15 | 16 |
| 522 | 17.366 | weak_tracked | 0.307692 | 7 | 11 | 15 | 11 |
| 389 | 12.942 | weak_tracked | 0.333333 | 5 | 7 | 10 | 8 |
| 879 | 29.244 | weak_tracked | 0.375000 | 4 | 6 | 9 | 7 |

### integrated_high_low_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 5 | 0.166 | weak_tracked | 0.260870 | 11 | 6 | 9 | 14 |
| 941 | 31.306 | weak_tracked | 0.400000 | 8 | 4 | 8 | 12 |
| 250 | 8.317 | weak_tracked | 0.410256 | 21 | 2 | 10 | 29 |
| 335 | 11.145 | weak_tracked | 0.482759 | 12 | 3 | 10 | 19 |
| 111 | 3.693 | weak_tracked | 0.512821 | 14 | 5 | 15 | 24 |

## 视频与数据产物

- `full_high_pseudo_labels`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/full_high_pseudo_labels.jsonl`
- `full_high_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/full_high_detections.mp4`
- `high_dynamic_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/high_dynamic_flow_detections.mp4`
- `high_fixed_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/high_fixed_flow_detections.mp4`
- `low_dynamic_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/low_dynamic_flow_detections.mp4`
- `integrated_high_low_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/integrated_high_low_flow_detections.mp4`
- `comparison_grid_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/all_modes_comparison_grid.mp4`
- `full_high_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/full_high_config.yaml`
- `full_high_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/full_high_infer_video_response.json`
- `high_dynamic_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/high_dynamic_flow_config.yaml`
- `high_dynamic_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/high_dynamic_flow_infer_video_response.json`
- `high_fixed_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/high_fixed_flow_config.yaml`
- `high_fixed_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/high_fixed_flow_infer_video_response.json`
- `low_dynamic_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/low_dynamic_flow_config.yaml`
- `low_dynamic_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/low_dynamic_flow_infer_video_response.json`
- `integrated_high_low_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/integrated_high_low_flow_config.yaml`
- `integrated_high_low_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/integrated_high_low_flow_infer_video_response.json`
- `comparison_json`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/comparison.json`
- `comparison_markdown`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/00268999-cb063914_20260710_063010/comparison.md`
