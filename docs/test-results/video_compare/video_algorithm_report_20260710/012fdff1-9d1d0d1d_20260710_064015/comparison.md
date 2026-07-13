# 五种视频推理模式对比

- 原始视频：`/home/ubuntu/YOLO/datasets/bdd100k_tracking_video/bdd100k_videos_train_00/bdd100k/videos/train/012fdff1-9d1d0d1d.mov`
- 输出目录：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015`
- 质量参照：`full_high` 全帧高分辨率结果（伪标签，不是真值）。
- 后端：ONNX Runtime CPU；4 线程；高/低分辨率置信度阈值均为 0.25。

## 运行摘要

| 模式 | endpoint | stride | async | 帧数 | 模型调用(推导) | 耗时(s) | FPS | CPU(%) | RSS(MB) | 唯一ID |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | `/infer_video` | full_model | False | 1207 | 1207 | 384.925 | 3.136 | 582.338 | 501.965 | 157 |
| high_dynamic_flow | `/infer_video` | async_dynamic | True | 1207 | 48 | 21.802 | 55.362 | 3060.477 | 501.793 | 89 |
| high_fixed_flow | `/infer_video` | async_fixed | True | 1207 | 46 | 22.014 | 54.829 | 3103.583 | 501.016 | 94 |
| low_dynamic_flow | `/infer_video` | async_dynamic | True | 1207 | 136 | 21.105 | 57.190 | 3082.634 | 244.008 | 114 |
| integrated_high_low_flow | `/infer_video_high_low` | async_high_low_dynamic | True | 1207 | 185 | 37.099 | 32.535 | 1991.685 | 641.379 | 97 |

## 阶段耗时与延迟

| 模式 | infer总计(ms) | preprocess(ms) | postprocess(ms) | flow(ms) | tracker(ms) | E2E p50(ms) | E2E p95(ms) | E2E p99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 356834.844 | 6668.367 | 4.290 | 15161.740 | 62.528 | 310.071 | 361.980 | 385.627 |
| high_dynamic_flow | 20667.719 | 350.499 | 0.209 | 13763.579 | 36.189 | 9.924 | 22.471 | 44.374 |
| high_fixed_flow | 20980.976 | 371.435 | 0.298 | 13612.691 | 41.072 | 10.399 | 21.315 | 33.892 |
| low_dynamic_flow | 15922.967 | 396.478 | 0.706 | 13441.846 | 39.359 | 10.144 | 21.685 | 41.151 |
| integrated_high_low_flow | 30189.607 | 670.316 | 0.920 | 14411.654 | 18.084 | 11.408 | 118.314 | 139.945 |

## 相对全帧基准的性能

| 模式 | 加速比 | 节省耗时(s) | 减少处理帧 | 处理帧减少比例 |
| --- | ---: | ---: | ---: | ---: |
| high_dynamic_flow | 17.655x | 363.123 | 1159 | 0.960 |
| high_fixed_flow | 17.485x | 362.911 | 1161 | 0.962 |
| low_dynamic_flow | 18.239x | 363.820 | 1071 | 0.887 |
| integrated_high_low_flow | 10.376x | 347.826 | 1059 | 0.877 |

## 质量一致性：high_dynamic_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.684870 | 0.660141 | 0.672278 | 0.660225 | 5631 | 8530 | 8222 | 2591 | 2899 |
| 0.500 | 0.531136 | 0.511958 | 0.521371 | 0.734777 | 4367 | 8530 | 8222 | 3855 | 4163 |
| 0.700 | 0.319022 | 0.307503 | 0.313157 | 0.820141 | 2623 | 8530 | 8222 | 5599 | 5907 |

## 质量一致性：high_fixed_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.660047 | 0.627315 | 0.643265 | 0.666664 | 5351 | 8530 | 8107 | 2756 | 3179 |
| 0.500 | 0.515727 | 0.490152 | 0.502615 | 0.738778 | 4181 | 8530 | 8107 | 3926 | 4349 |
| 0.700 | 0.317997 | 0.302227 | 0.309912 | 0.822103 | 2578 | 8530 | 8107 | 5529 | 5952 |

## 质量一致性：low_dynamic_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.807618 | 0.783001 | 0.795119 | 0.751401 | 6679 | 8530 | 8270 | 1591 | 1851 |
| 0.500 | 0.725030 | 0.702931 | 0.713810 | 0.789751 | 5996 | 8530 | 8270 | 2274 | 2534 |
| 0.700 | 0.565659 | 0.548417 | 0.556905 | 0.840555 | 4678 | 8530 | 8270 | 3592 | 3852 |

## 质量一致性：integrated_high_low_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.530331 | 0.923447 | 0.673737 | 0.841626 | 7877 | 8530 | 14853 | 6976 | 653 |
| 0.500 | 0.508517 | 0.885463 | 0.646025 | 0.860181 | 7553 | 8530 | 14853 | 7300 | 977 |
| 0.700 | 0.451289 | 0.785815 | 0.573322 | 0.891129 | 6703 | 8530 | 14853 | 8150 | 1827 |

## 跟踪代理指标

| 模式 | observations | unique IDs | segments | reappearances | mean lifetime | median lifetime | p95 lifetime |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 8530 | 157 | 211 | 54 | 40.427 | 10.000 | 203 |
| high_dynamic_flow | 8222 | 89 | 92 | 3 | 89.370 | 33.000 | 228 |
| high_fixed_flow | 8107 | 94 | 97 | 3 | 83.577 | 33.000 | 189 |
| low_dynamic_flow | 8270 | 114 | 130 | 16 | 63.615 | 15.500 | 319 |
| integrated_high_low_flow | 14853 | 97 | 98 | 1 | 151.561 | 91.000 | 448 |

## IoU=0.5 最差时间点

### high_dynamic_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 6 | 0.200 | empty | 0.000000 | 0 | 16 | 16 | 0 |
| 670 | 22.316 | weak_tracked | 0.000000 | 6 | 5 | 5 | 6 |
| 454 | 15.121 | weak_tracked | 0.125000 | 7 | 7 | 8 | 8 |
| 141 | 4.696 | weak_tracked | 0.125000 | 9 | 5 | 6 | 10 |
| 585 | 19.485 | weak_tracked | 0.166667 | 5 | 5 | 6 | 6 |

### high_fixed_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 6 | 0.200 | empty | 0.000000 | 0 | 16 | 16 | 0 |
| 671 | 22.349 | weak_tracked | 0.000000 | 6 | 5 | 5 | 6 |
| 505 | 16.820 | weak_tracked | 0.117647 | 5 | 10 | 11 | 6 |
| 141 | 4.696 | weak_tracked | 0.133333 | 8 | 5 | 6 | 9 |
| 443 | 14.755 | weak_tracked | 0.142857 | 7 | 5 | 6 | 8 |

### low_dynamic_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.033 | empty | 0.000000 | 0 | 15 | 15 | 0 |
| 454 | 15.121 | weak_tracked | 0.428571 | 3 | 5 | 8 | 6 |
| 140 | 4.663 | async_corrected | 0.444444 | 2 | 3 | 5 | 4 |
| 346 | 11.524 | weak_tracked | 0.500000 | 3 | 5 | 9 | 7 |
| 641 | 21.350 | weak_tracked | 0.500000 | 2 | 4 | 7 | 5 |

### integrated_high_low_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 127 | 4.230 | weak_tracked | 0.200000 | 16 | 0 | 2 | 18 |
| 638 | 21.250 | weak_tracked | 0.375000 | 8 | 2 | 5 | 11 |
| 756 | 25.180 | weak_tracked | 0.400000 | 9 | 3 | 7 | 13 |
| 239 | 7.960 | weak_tracked | 0.421053 | 7 | 4 | 8 | 11 |
| 443 | 14.755 | detected | 0.421053 | 9 | 2 | 6 | 13 |

## 视频与数据产物

- `full_high_pseudo_labels`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/full_high_pseudo_labels.jsonl`
- `full_high_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/full_high_detections.mp4`
- `high_dynamic_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/high_dynamic_flow_detections.mp4`
- `high_fixed_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/high_fixed_flow_detections.mp4`
- `low_dynamic_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/low_dynamic_flow_detections.mp4`
- `integrated_high_low_flow_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/integrated_high_low_flow_detections.mp4`
- `comparison_grid_video`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/all_modes_comparison_grid.mp4`
- `full_high_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/full_high_config.yaml`
- `full_high_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/full_high_infer_video_response.json`
- `high_dynamic_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/high_dynamic_flow_config.yaml`
- `high_dynamic_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/high_dynamic_flow_infer_video_response.json`
- `high_fixed_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/high_fixed_flow_config.yaml`
- `high_fixed_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/high_fixed_flow_infer_video_response.json`
- `low_dynamic_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/low_dynamic_flow_config.yaml`
- `low_dynamic_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/low_dynamic_flow_infer_video_response.json`
- `integrated_high_low_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/integrated_high_low_flow_config.yaml`
- `integrated_high_low_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/integrated_high_low_flow_infer_video_response.json`
- `comparison_json`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/comparison.json`
- `comparison_markdown`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_report_20260710/012fdff1-9d1d0d1d_20260710_064015/comparison.md`
