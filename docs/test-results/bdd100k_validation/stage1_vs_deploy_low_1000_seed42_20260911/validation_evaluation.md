# BDD100K YOLO validation comparison

Generated: 2026-09-11T06:33:59.495865+00:00

Subset: 1000 validation images; method=sha256_rank_with_forced_class_positives; seed=42; forced=train.

| Model | Runtime | Score | mAP50 | Precision | Recall | F1 | Mean infer ms | P95 infer ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| deploy_low | openvino | 0.001 | 0.537823 | 0.119180 | 0.850856 | 0.209074 | 45.652 | 98.504 |
| deploy_low | openvino | 0.010 | 0.531264 | 0.285160 | 0.824680 | 0.423783 | 45.652 | 98.504 |
| deploy_low | openvino | 0.050 | 0.507570 | 0.514577 | 0.777693 | 0.619349 | 45.652 | 98.504 |
| deploy_low | openvino | 0.100 | 0.487165 | 0.636431 | 0.743009 | 0.685603 | 45.652 | 98.504 |
| deploy_low | openvino | 0.150 | 0.466965 | 0.711541 | 0.710384 | 0.710962 | 45.652 | 98.504 |
| deploy_low | openvino | 0.200 | 0.449465 | 0.766775 | 0.676891 | 0.719035 | 45.652 | 98.504 |
| deploy_low | openvino | 0.250 | 0.430385 | 0.809677 | 0.642098 | 0.716216 | 45.652 | 98.504 |
| optimized_stage1 | openvino | 0.001 | 0.533470 | 0.118420 | 0.849339 | 0.207860 | 43.429 | 74.562 |
| optimized_stage1 | openvino | 0.010 | 0.526043 | 0.280190 | 0.822241 | 0.417956 | 43.429 | 74.562 |
| optimized_stage1 | openvino | 0.050 | 0.506101 | 0.510665 | 0.775905 | 0.615944 | 43.429 | 74.562 |
| optimized_stage1 | openvino | 0.100 | 0.486303 | 0.628887 | 0.739757 | 0.679832 | 43.429 | 74.562 |
| optimized_stage1 | openvino | 0.150 | 0.467091 | 0.704586 | 0.708595 | 0.706585 | 43.429 | 74.562 |
| optimized_stage1 | openvino | 0.200 | 0.451435 | 0.760122 | 0.676620 | 0.715945 | 43.429 | 74.562 |
| optimized_stage1 | openvino | 0.250 | 0.429584 | 0.805614 | 0.642369 | 0.714790 | 43.429 | 74.562 |

## Per-class measured best F1

AP50 uses the minimum-score candidate floor. Best score is selected only from the measured grid.

| Model | Runtime | Class | GT | AP50 | Best score | Precision | Recall | F1 |
|---|---|---|---:|---:|---:|---:|---:|---:|
| deploy_low | openvino | bike | 95 | 0.533221 | 0.150 | 0.576471 | 0.515789 | 0.544444 |
| deploy_low | openvino | bus | 168 | 0.613430 | 0.250 | 0.712000 | 0.529762 | 0.607509 |
| deploy_low | openvino | car | 10174 | 0.779255 | 0.200 | 0.816481 | 0.720661 | 0.765584 |
| deploy_low | openvino | motor | 53 | 0.373453 | 0.150 | 0.538462 | 0.396226 | 0.456522 |
| deploy_low | openvino | person | 1354 | 0.599433 | 0.200 | 0.759298 | 0.542836 | 0.633075 |
| deploy_low | openvino | rider | 52 | 0.443736 | 0.250 | 0.600000 | 0.403846 | 0.482759 |
| deploy_low | openvino | traffic light | 2618 | 0.669793 | 0.200 | 0.696921 | 0.665775 | 0.680992 |
| deploy_low | openvino | traffic sign | 3508 | 0.683057 | 0.250 | 0.756424 | 0.612600 | 0.676957 |
| deploy_low | openvino | train | 15 | 0.061063 | 0.010 | 0.200000 | 0.200000 | 0.200000 |
| deploy_low | openvino | truck | 415 | 0.621788 | 0.200 | 0.639474 | 0.585542 | 0.611321 |
| optimized_stage1 | openvino | bike | 95 | 0.532317 | 0.250 | 0.714286 | 0.473684 | 0.569620 |
| optimized_stage1 | openvino | bus | 168 | 0.610351 | 0.200 | 0.701493 | 0.559524 | 0.622517 |
| optimized_stage1 | openvino | car | 10174 | 0.776065 | 0.200 | 0.809740 | 0.717417 | 0.760788 |
| optimized_stage1 | openvino | motor | 53 | 0.361075 | 0.100 | 0.511628 | 0.415094 | 0.458333 |
| optimized_stage1 | openvino | person | 1354 | 0.598828 | 0.200 | 0.750000 | 0.545052 | 0.631309 |
| optimized_stage1 | openvino | rider | 52 | 0.441630 | 0.200 | 0.589744 | 0.442308 | 0.505495 |
| optimized_stage1 | openvino | traffic light | 2618 | 0.660477 | 0.200 | 0.685236 | 0.671887 | 0.678496 |
| optimized_stage1 | openvino | traffic sign | 3508 | 0.682132 | 0.250 | 0.753482 | 0.616876 | 0.678370 |
| optimized_stage1 | openvino | train | 15 | 0.032728 | 0.010 | 0.071429 | 0.066667 | 0.068966 |
| optimized_stage1 | openvino | truck | 415 | 0.639094 | 0.200 | 0.686275 | 0.590361 | 0.634715 |

Notes: this subset is independent from train, but forced-class enrichment means pooled metrics are diagnostic rather than an unbiased full-validation estimate. YOLO detection labels have no crowd/distractor regions. Inference latency excludes image loading, preprocessing, decode and NMS.
