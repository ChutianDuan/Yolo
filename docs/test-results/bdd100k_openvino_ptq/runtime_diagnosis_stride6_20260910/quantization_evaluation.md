# BDD100K PTQ runtime comparison

Generated: `2026-09-10T23:23:38.325208+00:00`

| Model | Runtime | Score | mAP50 | Precision | Recall | F1 | Mean infer ms | P95 infer ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fp32_openvino | openvino | 0.001 | 0.471531 | 0.134867 | 0.854111 | 0.232950 | 39.304 | 57.756 |
| fp32_openvino | openvino | 0.010 | 0.453775 | 0.350641 | 0.800683 | 0.487703 | 39.304 | 57.756 |
| fp32_openvino | openvino | 0.050 | 0.435630 | 0.576057 | 0.767748 | 0.658230 | 39.304 | 57.756 |
| fp32_openvino | openvino | 0.100 | 0.427333 | 0.669791 | 0.749695 | 0.707494 | 39.304 | 57.756 |
| fp32_openvino | openvino | 0.150 | 0.414892 | 0.729500 | 0.733594 | 0.731541 | 39.304 | 57.756 |
| fp32_openvino | openvino | 0.200 | 0.408286 | 0.771331 | 0.716760 | 0.743045 | 39.304 | 57.756 |
| fp32_openvino | openvino | 0.250 | 0.393914 | 0.809109 | 0.702122 | 0.751829 | 39.304 | 57.756 |
| int8_openvino | openvino | 0.001 | 0.020518 | 0.139276 | 0.109783 | 0.122783 | 23.863 | 34.135 |
| int8_openvino | openvino | 0.010 | 0.002475 | 0.439394 | 0.007075 | 0.013926 | 23.863 | 34.135 |
| int8_openvino | openvino | 0.050 | 0.001650 | 0.333333 | 0.000488 | 0.000974 | 23.863 | 34.135 |
| int8_openvino | openvino | 0.100 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 23.863 | 34.135 |
| int8_openvino | openvino | 0.150 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 23.863 | 34.135 |
| int8_openvino | openvino | 0.200 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 23.863 | 34.135 |
| int8_openvino | openvino | 0.250 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 23.863 | 34.135 |
| int8_onnxruntime | onnxruntime | 0.001 | 0.437558 | 0.239942 | 0.807514 | 0.369956 | 98.305 | 122.251 |
| int8_onnxruntime | onnxruntime | 0.010 | 0.411329 | 0.507560 | 0.761649 | 0.609171 | 98.305 | 122.251 |
| int8_onnxruntime | onnxruntime | 0.050 | 0.382319 | 0.686320 | 0.718468 | 0.702026 | 98.305 | 122.251 |
| int8_onnxruntime | onnxruntime | 0.100 | 0.349623 | 0.783243 | 0.668212 | 0.721169 | 98.305 | 122.251 |
| int8_onnxruntime | onnxruntime | 0.150 | 0.341609 | 0.830335 | 0.653086 | 0.731121 | 98.305 | 122.251 |
| int8_onnxruntime | onnxruntime | 0.200 | 0.314001 | 0.833768 | 0.624055 | 0.713827 | 98.305 | 122.251 |
| int8_onnxruntime | onnxruntime | 0.250 | 0.279379 | 0.864719 | 0.548914 | 0.671542 | 98.305 | 122.251 |

Notes: inference latency excludes video decoding, preprocessing, decode and NMS. The minimum-score row is the AP candidate floor; higher rows are operating points. Crowd-overlapping same-class predictions are ignored for both models.
