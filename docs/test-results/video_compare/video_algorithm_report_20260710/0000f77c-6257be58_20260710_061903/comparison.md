# 五种视频推理模式对比

- 原始视频：`/home/ubuntu/YOLO/datasets/bdd100k_tracking_video/bdd100k_videos_train_00/bdd100k/videos/train/0000f77c-6257be58.mov`
- 输出目录：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903`
- 质量参照：`full_high` 全帧高分辨率结果（伪标签，不是真值）。
- 后端：ONNX Runtime CPU；4 线程；高/低分辨率置信度阈值均为 0.25。

## 运行摘要

| 模式 | endpoint | stride | async | 帧数 | 模型调用(推导) | 耗时(s) | FPS | CPU(%) | RSS(MB) | 唯一ID |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | `/infer_video` | full_model | False | 1217 | 1217 | 400.903 | 3.036 | 586.761 | 504.457 | 276 |
| high_dynamic_flow | `/infer_video` | async_dynamic | True | 1217 | 65 | 28.028 | 43.421 | 2757.405 | 503.344 | 238 |
| high_fixed_flow | `/infer_video` | async_fixed | True | 1217 | 65 | 28.515 | 42.679 | 2665.441 | 504.273 | 226 |
| low_dynamic_flow | `/infer_video` | async_dynamic | True | 1217 | 158 | 26.148 | 46.543 | 2808.700 | 246.160 | 256 |
| integrated_high_low_flow | `/infer_video_high_low` | async_high_low_dynamic | True | 1217 | 188 | 42.820 | 28.421 | 1896.997 | 646.027 | 139 |

## 阶段耗时与延迟

| 模式 | infer总计(ms) | preprocess(ms) | postprocess(ms) | flow(ms) | tracker(ms) | E2E p50(ms) | E2E p95(ms) | E2E p99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 368045.901 | 7151.490 | 4.878 | 15779.582 | 92.907 | 320.794 | 367.877 | 389.490 |
| high_dynamic_flow | 26278.273 | 510.434 | 0.377 | 14216.012 | 52.715 | 10.520 | 23.732 | 57.625 |
| high_fixed_flow | 26824.793 | 476.070 | 0.316 | 14684.473 | 50.181 | 10.871 | 23.995 | 51.756 |
| low_dynamic_flow | 18821.576 | 508.128 | 0.946 | 13362.132 | 51.421 | 10.073 | 22.500 | 42.701 |
| integrated_high_low_flow | 29766.757 | 701.073 | 0.909 | 14294.905 | 27.276 | 10.867 | 121.850 | 140.890 |

## 相对全帧基准的性能

| 模式 | 加速比 | 节省耗时(s) | 减少处理帧 | 处理帧减少比例 |
| --- | ---: | ---: | ---: | ---: |
| high_dynamic_flow | 14.304x | 372.875 | 1152 | 0.947 |
| high_fixed_flow | 14.059x | 372.388 | 1152 | 0.947 |
| low_dynamic_flow | 15.332x | 374.755 | 1059 | 0.870 |
| integrated_high_low_flow | 9.363x | 358.083 | 1067 | 0.877 |

## 质量一致性：high_dynamic_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.604620 | 0.504143 | 0.549829 | 0.628062 | 6753 | 13395 | 11169 | 4416 | 6642 |
| 0.500 | 0.434775 | 0.362523 | 0.395375 | 0.716129 | 4856 | 13395 | 11169 | 6313 | 8539 |
| 0.700 | 0.221775 | 0.184920 | 0.201677 | 0.831172 | 2477 | 13395 | 11169 | 8692 | 10918 |

## 质量一致性：high_fixed_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.593547 | 0.483389 | 0.532834 | 0.625086 | 6475 | 13395 | 10909 | 4434 | 6920 |
| 0.500 | 0.415712 | 0.338559 | 0.373190 | 0.719722 | 4535 | 13395 | 10909 | 6374 | 8860 |
| 0.700 | 0.223302 | 0.181859 | 0.200461 | 0.828828 | 2436 | 13395 | 10909 | 8473 | 10959 |

## 质量一致性：low_dynamic_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.818743 | 0.663979 | 0.733284 | 0.699694 | 8894 | 13395 | 10863 | 1969 | 4501 |
| 0.500 | 0.695296 | 0.563867 | 0.622722 | 0.749789 | 7553 | 13395 | 10863 | 3310 | 5842 |
| 0.700 | 0.434318 | 0.352221 | 0.388985 | 0.837791 | 4718 | 13395 | 10863 | 6145 | 8677 |

## 质量一致性：integrated_high_low_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.514202 | 0.851437 | 0.641181 | 0.821183 | 11405 | 13395 | 22180 | 10775 | 1990 |
| 0.500 | 0.485888 | 0.804554 | 0.605875 | 0.845135 | 10777 | 13395 | 22180 | 11403 | 2618 |
| 0.700 | 0.416186 | 0.689138 | 0.518960 | 0.883541 | 9231 | 13395 | 22180 | 12949 | 4164 |

## 跟踪代理指标

| 模式 | observations | unique IDs | segments | reappearances | mean lifetime | median lifetime | p95 lifetime |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 13395 | 276 | 408 | 132 | 32.831 | 6.000 | 153 |
| high_dynamic_flow | 11169 | 238 | 257 | 19 | 43.459 | 19.000 | 193 |
| high_fixed_flow | 10909 | 226 | 244 | 18 | 44.709 | 20.000 | 189 |
| low_dynamic_flow | 10863 | 256 | 290 | 34 | 37.459 | 9.000 | 165 |
| integrated_high_low_flow | 22180 | 139 | 139 | 0 | 159.568 | 123.000 | 435 |

## IoU=0.5 最差时间点

### high_dynamic_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 263 | 8.723 | async_corrected | 0.000000 | 13 | 13 | 13 | 13 |
| 416 | 13.798 | weak_tracked | 0.000000 | 9 | 15 | 15 | 9 |
| 595 | 19.736 | weak_tracked | 0.000000 | 2 | 14 | 14 | 2 |
| 506 | 16.784 | weak_tracked | 0.000000 | 4 | 9 | 9 | 4 |
| 332 | 11.012 | weak_tracked | 0.000000 | 3 | 9 | 9 | 3 |

### high_fixed_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 628 | 20.830 | async_corrected | 0.000000 | 12 | 14 | 14 | 12 |
| 255 | 8.458 | async_corrected | 0.000000 | 11 | 14 | 14 | 11 |
| 416 | 13.798 | weak_tracked | 0.000000 | 9 | 15 | 15 | 9 |
| 567 | 18.807 | weak_tracked | 0.000000 | 4 | 9 | 9 | 4 |
| 327 | 10.846 | weak_tracked | 0.000000 | 3 | 9 | 9 | 3 |

### low_dynamic_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 427 | 14.163 | weak_tracked | 0.000000 | 10 | 12 | 12 | 10 |
| 314 | 10.415 | async_corrected | 0.000000 | 5 | 5 | 5 | 5 |
| 2 | 0.066 | empty | 0.000000 | 0 | 9 | 9 | 0 |
| 490 | 16.253 | weak_tracked | 0.142857 | 4 | 8 | 9 | 5 |
| 624 | 20.697 | weak_tracked | 0.166667 | 9 | 11 | 13 | 11 |

### integrated_high_low_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 500 | 16.584 | weak_tracked | 0.142857 | 18 | 6 | 8 | 20 |
| 327 | 10.846 | weak_tracked | 0.250000 | 12 | 6 | 9 | 15 |
| 567 | 18.807 | weak_tracked | 0.272727 | 10 | 6 | 9 | 13 |
| 1070 | 35.491 | weak_tracked | 0.272727 | 14 | 2 | 5 | 17 |
| 423 | 14.030 | weak_tracked | 0.285714 | 19 | 6 | 11 | 24 |

## 视频与数据产物

- `full_high_pseudo_labels`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/full_high_pseudo_labels.jsonl`
- `full_high_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/full_high_detections.mp4`
- `high_dynamic_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/high_dynamic_flow_detections.mp4`
- `high_fixed_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/high_fixed_flow_detections.mp4`
- `low_dynamic_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/low_dynamic_flow_detections.mp4`
- `integrated_high_low_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/integrated_high_low_flow_detections.mp4`
- `comparison_grid_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/all_modes_comparison_grid.mp4`
- `full_high_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/full_high_config.yaml`
- `full_high_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/full_high_infer_video_response.json`
- `high_dynamic_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/high_dynamic_flow_config.yaml`
- `high_dynamic_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/high_dynamic_flow_infer_video_response.json`
- `high_fixed_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/high_fixed_flow_config.yaml`
- `high_fixed_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/high_fixed_flow_infer_video_response.json`
- `low_dynamic_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/low_dynamic_flow_config.yaml`
- `low_dynamic_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/low_dynamic_flow_infer_video_response.json`
- `integrated_high_low_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/integrated_high_low_flow_config.yaml`
- `integrated_high_low_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/integrated_high_low_flow_infer_video_response.json`
- `comparison_json`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/comparison.json`
- `comparison_markdown`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/0000f77c-6257be58_20260710_061903/comparison.md`
