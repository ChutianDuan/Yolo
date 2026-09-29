# BDD100K PTQ runtime comparison

Generated: `2026-09-10T23:32:16.915379+00:00`

| Model | Runtime | Score | mAP50 | Precision | Recall | F1 | Mean infer ms | P95 infer ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fp32_openvino | openvino | 0.001 | 0.471531 | 0.134867 | 0.854111 | 0.232950 | 39.171 | 53.237 |
| fp32_openvino | openvino | 0.010 | 0.453775 | 0.350641 | 0.800683 | 0.487703 | 39.171 | 53.237 |
| fp32_openvino | openvino | 0.050 | 0.435630 | 0.576057 | 0.767748 | 0.658230 | 39.171 | 53.237 |
| fp32_openvino | openvino | 0.100 | 0.427333 | 0.669791 | 0.749695 | 0.707494 | 39.171 | 53.237 |
| fp32_openvino | openvino | 0.150 | 0.414892 | 0.729500 | 0.733594 | 0.731541 | 39.171 | 53.237 |
| fp32_openvino | openvino | 0.200 | 0.408286 | 0.771331 | 0.716760 | 0.743045 | 39.171 | 53.237 |
| fp32_openvino | openvino | 0.250 | 0.393914 | 0.809109 | 0.702122 | 0.751829 | 39.171 | 53.237 |
| u8s8_pc_openvino | openvino | 0.001 | 0.450545 | 0.142693 | 0.848012 | 0.244281 | 23.674 | 32.499 |
| u8s8_pc_openvino | openvino | 0.010 | 0.433431 | 0.360777 | 0.797512 | 0.496809 | 23.674 | 32.499 |
| u8s8_pc_openvino | openvino | 0.050 | 0.408458 | 0.553420 | 0.765797 | 0.642514 | 23.674 | 32.499 |
| u8s8_pc_openvino | openvino | 0.100 | 0.383771 | 0.666887 | 0.738961 | 0.701076 | 23.674 | 32.499 |
| u8s8_pc_openvino | openvino | 0.150 | 0.380261 | 0.715381 | 0.728470 | 0.721866 | 23.674 | 32.499 |
| u8s8_pc_openvino | openvino | 0.200 | 0.362228 | 0.720696 | 0.717004 | 0.718846 | 23.674 | 32.499 |
| u8s8_pc_openvino | openvino | 0.250 | 0.339869 | 0.762343 | 0.666748 | 0.711348 | 23.674 | 32.499 |
| u8s8_pc_onnxruntime | onnxruntime | 0.001 | 0.441146 | 0.142296 | 0.848988 | 0.243740 | 49.235 | 59.222 |
| u8s8_pc_onnxruntime | onnxruntime | 0.010 | 0.425003 | 0.362296 | 0.797512 | 0.498247 | 49.235 | 59.222 |
| u8s8_pc_onnxruntime | onnxruntime | 0.050 | 0.401539 | 0.554533 | 0.764089 | 0.642659 | 49.235 | 59.222 |
| u8s8_pc_onnxruntime | onnxruntime | 0.100 | 0.379730 | 0.668940 | 0.741888 | 0.703528 | 49.235 | 59.222 |
| u8s8_pc_onnxruntime | onnxruntime | 0.150 | 0.376202 | 0.716346 | 0.727007 | 0.721637 | 49.235 | 59.222 |
| u8s8_pc_onnxruntime | onnxruntime | 0.200 | 0.359432 | 0.723278 | 0.714808 | 0.719018 | 49.235 | 59.222 |
| u8s8_pc_onnxruntime | onnxruntime | 0.250 | 0.336415 | 0.762064 | 0.666504 | 0.711088 | 49.235 | 59.222 |

Notes: inference latency excludes video decoding, preprocessing, decode and NMS. The minimum-score row is the AP candidate floor; higher rows are operating points. Crowd-overlapping same-class predictions are ignored for both models.
