# 五种视频推理模式对比

- 原始视频：`/home/ubuntu/YOLO/datasets/bdd100k_tracking_video/bdd100k_videos_train_00/bdd100k/videos/train/0000f77c-6257be58.mov`
- 输出目录：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807`
- 质量参照：`full_high` 全帧高分辨率结果（伪标签，不是真值）。
- 后端：ONNX Runtime CPU；4 线程；高/低分辨率置信度阈值均为 0.25。

## 运行摘要

| 模式 | endpoint | stride | async | 帧数 | 模型调用(推导) | 耗时(s) | FPS | CPU(%) | RSS(MB) | 唯一ID |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | `/infer_video` | full_model | False | 1217 | 1217 | 432.020 | 2.817 | 698.791 | 504.027 | 433 |
| high_dynamic_flow | `/infer_video` | async_dynamic | True | 1217 | 147 | 69.008 | 17.636 | 2268.579 | 486.770 | 349 |
| high_fixed_flow | `/infer_video` | async_fixed | True | 1217 | 151 | 80.430 | 15.131 | 2033.210 | 487.027 | 315 |
| low_dynamic_flow | `/infer_video` | async_dynamic | True | 1217 | 221 | 78.057 | 15.591 | 1835.209 | 224.758 | 230 |
| integrated_high_low_flow | `/infer_video_high_low` | async_high_low_dynamic | True | 1217 | 1010 | 159.934 | 7.609 | 1235.166 | 615.609 | 301 |

## 阶段耗时与延迟

| 模式 | infer总计(ms) | preprocess(ms) | postprocess(ms) | flow(ms) | tracker(ms) | E2E p50(ms) | E2E p95(ms) | E2E p99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 365206.110 | 6171.720 | 5.435 | 50688.194 | 85.875 | 345.484 | 395.857 | 422.503 |
| high_dynamic_flow | 57629.478 | 964.653 | 0.700 | 55038.720 | 59.584 | 41.997 | 69.862 | 100.101 |
| high_fixed_flow | 59662.729 | 914.392 | 0.797 | 63706.364 | 57.287 | 49.689 | 79.919 | 105.109 |
| low_dynamic_flow | 24705.683 | 677.606 | 1.170 | 60985.830 | 53.585 | 48.715 | 74.928 | 102.041 |
| integrated_high_low_flow | 127673.898 | 3025.328 | 5.605 | 56212.789 | 38.669 | 137.010 | 175.213 | 208.008 |

## 相对全帧基准的性能

| 模式 | 加速比 | 节省耗时(s) | 减少处理帧 | 处理帧减少比例 |
| --- | ---: | ---: | ---: | ---: |
| high_dynamic_flow | 6.260x | 363.012 | 1070 | 0.879 |
| high_fixed_flow | 5.371x | 351.590 | 1066 | 0.876 |
| low_dynamic_flow | 5.535x | 353.963 | 996 | 0.818 |
| integrated_high_low_flow | 2.701x | 272.086 | 309 | 0.254 |

## 质量一致性：high_dynamic_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.741314 | 0.628938 | 0.680518 | 0.696230 | 8385 | 13332 | 11311 | 2926 | 4947 |
| 0.500 | 0.599151 | 0.508326 | 0.550014 | 0.763402 | 6777 | 13332 | 11311 | 4534 | 6555 |
| 0.700 | 0.402617 | 0.341584 | 0.369598 | 0.840997 | 4554 | 13332 | 11311 | 6757 | 8778 |

## 质量一致性：high_fixed_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.780147 | 0.674992 | 0.723770 | 0.688551 | 8999 | 13332 | 11535 | 2536 | 4333 |
| 0.500 | 0.635371 | 0.549730 | 0.589456 | 0.752614 | 7329 | 13332 | 11535 | 4206 | 6003 |
| 0.700 | 0.402081 | 0.347885 | 0.373024 | 0.838111 | 4638 | 13332 | 11535 | 6897 | 8694 |

## 质量一致性：low_dynamic_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.913233 | 0.738149 | 0.816409 | 0.742517 | 9841 | 13332 | 10776 | 935 | 3491 |
| 0.500 | 0.817650 | 0.660891 | 0.730961 | 0.780741 | 8811 | 13332 | 10776 | 1965 | 4521 |
| 0.700 | 0.579529 | 0.468422 | 0.518085 | 0.849181 | 6245 | 13332 | 10776 | 4531 | 7087 |

## 质量一致性：integrated_high_low_flow

| IoU | precision | recall | F1 | mean IoU | matches | labels | predictions | FP | FN |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.300 | 0.783521 | 0.924392 | 0.848147 | 0.865287 | 12324 | 13332 | 15729 | 3405 | 1008 |
| 0.500 | 0.765211 | 0.902790 | 0.828327 | 0.875780 | 12036 | 13332 | 15729 | 3693 | 1296 |
| 0.700 | 0.702715 | 0.829058 | 0.760676 | 0.898879 | 11053 | 13332 | 15729 | 4676 | 2279 |

## 跟踪代理指标

| 模式 | observations | unique IDs | segments | reappearances | mean lifetime | median lifetime | p95 lifetime |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 13332 | 433 | 565 | 132 | 23.596 | 2.000 | 132 |
| high_dynamic_flow | 11311 | 349 | 447 | 98 | 25.304 | 7.000 | 133 |
| high_fixed_flow | 11535 | 315 | 413 | 98 | 27.930 | 8.000 | 130 |
| low_dynamic_flow | 10776 | 230 | 321 | 91 | 33.570 | 10.000 | 140 |
| integrated_high_low_flow | 15729 | 301 | 373 | 72 | 42.169 | 17.000 | 176 |

## IoU=0.5 最差时间点

### high_dynamic_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 401 | 13.301 | weak_tracked | 0.000000 | 10 | 14 | 14 | 10 |
| 262 | 8.690 | weak_tracked | 0.000000 | 11 | 13 | 13 | 11 |
| 583 | 19.338 | weak_tracked | 0.000000 | 6 | 12 | 12 | 6 |
| 495 | 16.419 | async_corrected | 0.000000 | 8 | 9 | 9 | 8 |
| 2 | 0.066 | empty | 0.000000 | 0 | 9 | 9 | 0 |

### high_fixed_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 419 | 13.898 | weak_tracked | 0.000000 | 11 | 11 | 11 | 11 |
| 493 | 16.352 | weak_tracked | 0.000000 | 7 | 10 | 10 | 7 |
| 567 | 18.807 | weak_tracked | 0.000000 | 3 | 9 | 9 | 3 |
| 2 | 0.066 | empty | 0.000000 | 0 | 9 | 9 | 0 |
| 166 | 5.506 | weak_tracked | 0.153846 | 5 | 17 | 19 | 7 |

### low_dynamic_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 479 | 15.888 | weak_tracked | 0.000000 | 3 | 7 | 7 | 3 |
| 1 | 0.033 | empty | 0.000000 | 0 | 7 | 7 | 0 |
| 221 | 7.330 | weak_tracked | 0.166667 | 9 | 11 | 13 | 11 |
| 595 | 19.736 | weak_tracked | 0.190476 | 5 | 12 | 14 | 7 |
| 121 | 4.013 | weak_tracked | 0.235294 | 2 | 11 | 13 | 4 |

### integrated_high_low_flow

| frame | time(s) | source | F1 | FP | FN | labels | predictions |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 5 | 0.166 | weak_tracked | 0.250000 | 6 | 6 | 8 | 8 |
| 358 | 11.875 | detected | 0.352941 | 18 | 4 | 10 | 24 |
| 565 | 18.740 | weak_tracked | 0.400000 | 6 | 3 | 6 | 9 |
| 1065 | 35.325 | weak_tracked | 0.428571 | 6 | 2 | 5 | 9 |
| 121 | 4.013 | weak_tracked | 0.444444 | 1 | 9 | 13 | 5 |

## 视频与数据产物

- `full_high_pseudo_labels`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/full_high_pseudo_labels.jsonl`
- `full_high_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/full_high_config.yaml`
- `full_high_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/full_high_infer_video_response.json`
- `high_dynamic_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/high_dynamic_flow_config.yaml`
- `high_dynamic_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/high_dynamic_flow_infer_video_response.json`
- `high_fixed_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/high_fixed_flow_config.yaml`
- `high_fixed_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/high_fixed_flow_infer_video_response.json`
- `low_dynamic_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/low_dynamic_flow_config.yaml`
- `low_dynamic_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/low_dynamic_flow_infer_video_response.json`
- `integrated_high_low_flow_config`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/integrated_high_low_flow_config.yaml`
- `integrated_high_low_flow_response`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/integrated_high_low_flow_infer_video_response.json`
- `comparison_json`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/comparison.json`
- `comparison_markdown`：`/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/video_algorithm_optimization_20260710/0000f77c-6257be58_20260710_110807/comparison.md`
