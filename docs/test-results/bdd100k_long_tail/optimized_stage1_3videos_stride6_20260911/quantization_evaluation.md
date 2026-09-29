# BDD100K PTQ runtime comparison

Generated: `2026-09-11T01:48:10.922910+00:00`

| Model | Runtime | Score | mAP50 | Precision | Recall | F1 | Mean infer ms | P95 infer ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| optimized_stage1_fp32 | openvino | 0.001 | 0.284649 | 0.095367 | 0.620501 | 0.165325 | 46.846 | 80.456 |
| optimized_stage1_fp32 | openvino | 0.010 | 0.282058 | 0.146284 | 0.594750 | 0.234814 | 46.846 | 80.456 |
| optimized_stage1_fp32 | openvino | 0.050 | 0.269302 | 0.281248 | 0.533108 | 0.368231 | 46.846 | 80.456 |
| optimized_stage1_fp32 | openvino | 0.100 | 0.254246 | 0.367437 | 0.494532 | 0.421615 | 46.846 | 80.456 |
| optimized_stage1_fp32 | openvino | 0.150 | 0.243806 | 0.434102 | 0.465997 | 0.449485 | 46.846 | 80.456 |
| optimized_stage1_fp32 | openvino | 0.200 | 0.233375 | 0.488490 | 0.440943 | 0.463500 | 46.846 | 80.456 |
| optimized_stage1_fp32 | openvino | 0.250 | 0.225619 | 0.538501 | 0.419964 | 0.471903 | 46.846 | 80.456 |

Notes: inference latency excludes video decoding, preprocessing, decode and NMS. The minimum-score row is the AP candidate floor; higher rows are operating points. Unmatched predictions overlapping same-class crowd or any-class BDD distractor regions are ignored for every model.

## Per-class measured best F1

AP50 uses the minimum-score candidate floor. Best score is selected only from the reported operating-point grid; F1 ties prefer higher precision, then the lower score.

| Model | Runtime | Class | GT | AP50 | Best score | Precision | Recall | F1 |
|---|---|---|---:|---:|---:|---:|---:|---:|
| optimized_stage1_fp32 | openvino | bike | 301 | 0.115734 | 0.250 | 0.243446 | 0.215947 | 0.228873 |
| optimized_stage1_fp32 | openvino | bus | 319 | 0.436815 | 0.100 | 0.449405 | 0.473354 | 0.461069 |
| optimized_stage1_fp32 | openvino | car | 3697 | 0.424396 | 0.250 | 0.542463 | 0.499324 | 0.520000 |
| optimized_stage1_fp32 | openvino | motor | 338 | 0.175144 | 0.250 | 0.318681 | 0.257396 | 0.284779 |
| optimized_stage1_fp32 | openvino | person | 3666 | 0.339917 | 0.200 | 0.475079 | 0.410802 | 0.440609 |
| optimized_stage1_fp32 | openvino | rider | 374 | 0.095918 | 0.150 | 0.250000 | 0.229947 | 0.239554 |
| optimized_stage1_fp32 | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| optimized_stage1_fp32 | openvino | truck | 1190 | 0.689270 | 0.150 | 0.823040 | 0.582353 | 0.682087 |
