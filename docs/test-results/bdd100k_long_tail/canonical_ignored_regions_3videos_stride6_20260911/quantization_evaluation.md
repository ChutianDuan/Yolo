# BDD100K PTQ runtime comparison

Generated: `2026-09-11T01:26:40.698751+00:00`

| Model | Runtime | Score | mAP50 | Precision | Recall | F1 | Mean infer ms | P95 infer ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| full_high_openvino | openvino | 0.001 | 0.320636 | 0.112962 | 0.644562 | 0.192234 | 129.105 | 164.195 |
| full_high_openvino | openvino | 0.010 | 0.316602 | 0.183726 | 0.614436 | 0.282870 | 129.105 | 164.195 |
| full_high_openvino | openvino | 0.050 | 0.304207 | 0.327134 | 0.566614 | 0.414789 | 129.105 | 164.195 |
| full_high_openvino | openvino | 0.100 | 0.290848 | 0.409339 | 0.537781 | 0.464850 | 129.105 | 164.195 |
| full_high_openvino | openvino | 0.150 | 0.277040 | 0.463493 | 0.516902 | 0.488743 | 129.105 | 164.195 |
| full_high_openvino | openvino | 0.200 | 0.265800 | 0.506206 | 0.498707 | 0.502429 | 129.105 | 164.195 |
| full_high_openvino | openvino | 0.250 | 0.256735 | 0.540782 | 0.481209 | 0.509259 | 129.105 | 164.195 |
| high_downscaled_fp32 | openvino | 0.001 | 0.236028 | 0.104706 | 0.545039 | 0.175666 | 40.536 | 55.892 |
| high_downscaled_fp32 | openvino | 0.010 | 0.226748 | 0.213125 | 0.486578 | 0.296417 | 40.536 | 55.892 |
| high_downscaled_fp32 | openvino | 0.050 | 0.206458 | 0.363496 | 0.422549 | 0.390805 | 40.536 | 55.892 |
| high_downscaled_fp32 | openvino | 0.100 | 0.195883 | 0.451183 | 0.390535 | 0.418674 | 40.536 | 55.892 |
| high_downscaled_fp32 | openvino | 0.150 | 0.185382 | 0.512332 | 0.363492 | 0.425265 | 40.536 | 55.892 |
| high_downscaled_fp32 | openvino | 0.200 | 0.177921 | 0.562723 | 0.345198 | 0.427902 | 40.536 | 55.892 |
| high_downscaled_fp32 | openvino | 0.250 | 0.170080 | 0.601935 | 0.327898 | 0.424535 | 40.536 | 55.892 |
| low_fp32 | openvino | 0.001 | 0.268789 | 0.093101 | 0.615331 | 0.161732 | 38.735 | 50.974 |
| low_fp32 | openvino | 0.010 | 0.266002 | 0.150642 | 0.588686 | 0.239896 | 38.735 | 50.974 |
| low_fp32 | openvino | 0.050 | 0.252471 | 0.285592 | 0.529728 | 0.371108 | 38.735 | 50.974 |
| low_fp32 | openvino | 0.100 | 0.238959 | 0.370656 | 0.492046 | 0.422811 | 38.735 | 50.974 |
| low_fp32 | openvino | 0.150 | 0.228652 | 0.432633 | 0.462915 | 0.447262 | 38.735 | 50.974 |
| low_fp32 | openvino | 0.200 | 0.218504 | 0.484552 | 0.438159 | 0.460189 | 38.735 | 50.974 |
| low_fp32 | openvino | 0.250 | 0.211139 | 0.531163 | 0.416882 | 0.467135 | 38.735 | 50.974 |
| u8s8_pc_openvino | openvino | 0.001 | 0.218629 | 0.099798 | 0.535892 | 0.168261 | 23.416 | 30.209 |
| u8s8_pc_openvino | openvino | 0.010 | 0.208900 | 0.210163 | 0.472857 | 0.290994 | 23.416 | 30.209 |
| u8s8_pc_openvino | openvino | 0.050 | 0.193789 | 0.330881 | 0.420262 | 0.370254 | 23.416 | 30.209 |
| u8s8_pc_openvino | openvino | 0.100 | 0.177780 | 0.424101 | 0.378604 | 0.400063 | 23.416 | 30.209 |
| u8s8_pc_openvino | openvino | 0.150 | 0.170552 | 0.479734 | 0.358918 | 0.410624 | 23.416 | 30.209 |
| u8s8_pc_openvino | openvino | 0.200 | 0.163275 | 0.478165 | 0.346192 | 0.401615 | 23.416 | 30.209 |
| u8s8_pc_openvino | openvino | 0.250 | 0.153616 | 0.533758 | 0.316763 | 0.397579 | 23.416 | 30.209 |

Notes: inference latency excludes video decoding, preprocessing, decode and NMS. The minimum-score row is the AP candidate floor; higher rows are operating points. Unmatched predictions overlapping same-class crowd or any-class BDD distractor regions are ignored for every model.

## Per-class measured best F1

AP50 uses the minimum-score candidate floor. Best score is selected only from the reported operating-point grid; F1 ties prefer higher precision, then the lower score.

| Model | Runtime | Class | GT | AP50 | Best score | Precision | Recall | F1 |
|---|---|---|---:|---:|---:|---:|---:|---:|
| full_high_openvino | openvino | bike | 301 | 0.129987 | 0.250 | 0.213333 | 0.212625 | 0.212978 |
| full_high_openvino | openvino | bus | 319 | 0.573221 | 0.100 | 0.544944 | 0.608150 | 0.574815 |
| full_high_openvino | openvino | car | 3697 | 0.467542 | 0.250 | 0.576675 | 0.556397 | 0.566355 |
| full_high_openvino | openvino | motor | 338 | 0.216125 | 0.250 | 0.353909 | 0.254438 | 0.296041 |
| full_high_openvino | openvino | person | 3666 | 0.370062 | 0.250 | 0.483431 | 0.473541 | 0.478435 |
| full_high_openvino | openvino | rider | 374 | 0.082646 | 0.100 | 0.235988 | 0.213904 | 0.224404 |
| full_high_openvino | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| full_high_openvino | openvino | truck | 1190 | 0.725504 | 0.100 | 0.742397 | 0.697479 | 0.719237 |
| high_downscaled_fp32 | openvino | bike | 301 | 0.106895 | 0.100 | 0.303030 | 0.199336 | 0.240481 |
| high_downscaled_fp32 | openvino | bus | 319 | 0.337594 | 0.150 | 0.745763 | 0.275862 | 0.402746 |
| high_downscaled_fp32 | openvino | car | 3697 | 0.363766 | 0.250 | 0.581275 | 0.394644 | 0.470114 |
| high_downscaled_fp32 | openvino | motor | 338 | 0.104928 | 0.050 | 0.277551 | 0.201183 | 0.233276 |
| high_downscaled_fp32 | openvino | person | 3666 | 0.313961 | 0.150 | 0.504348 | 0.348063 | 0.411879 |
| high_downscaled_fp32 | openvino | rider | 374 | 0.047102 | 0.150 | 0.272727 | 0.096257 | 0.142292 |
| high_downscaled_fp32 | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| high_downscaled_fp32 | openvino | truck | 1190 | 0.613979 | 0.050 | 0.731293 | 0.542017 | 0.622587 |
| low_fp32 | openvino | bike | 301 | 0.066781 | 0.250 | 0.197842 | 0.182724 | 0.189983 |
| low_fp32 | openvino | bus | 319 | 0.409990 | 0.100 | 0.401070 | 0.470219 | 0.432900 |
| low_fp32 | openvino | car | 3697 | 0.420747 | 0.250 | 0.538802 | 0.493914 | 0.515382 |
| low_fp32 | openvino | motor | 338 | 0.171957 | 0.250 | 0.305842 | 0.263314 | 0.282989 |
| low_fp32 | openvino | person | 3666 | 0.342480 | 0.250 | 0.516588 | 0.386525 | 0.442191 |
| low_fp32 | openvino | rider | 374 | 0.063907 | 0.100 | 0.170404 | 0.203209 | 0.185366 |
| low_fp32 | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| low_fp32 | openvino | truck | 1190 | 0.674449 | 0.150 | 0.788313 | 0.600840 | 0.681927 |
| u8s8_pc_openvino | openvino | bike | 301 | 0.095000 | 0.150 | 0.300000 | 0.169435 | 0.216561 |
| u8s8_pc_openvino | openvino | bus | 319 | 0.333894 | 0.150 | 0.558442 | 0.269592 | 0.363636 |
| u8s8_pc_openvino | openvino | car | 3697 | 0.324633 | 0.150 | 0.427725 | 0.429808 | 0.428764 |
| u8s8_pc_openvino | openvino | motor | 338 | 0.075294 | 0.010 | 0.167401 | 0.224852 | 0.191919 |
| u8s8_pc_openvino | openvino | person | 3666 | 0.298735 | 0.150 | 0.495134 | 0.346972 | 0.408019 |
| u8s8_pc_openvino | openvino | rider | 374 | 0.030704 | 0.050 | 0.111406 | 0.112299 | 0.111851 |
| u8s8_pc_openvino | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| u8s8_pc_openvino | openvino | truck | 1190 | 0.590772 | 0.050 | 0.693856 | 0.550420 | 0.613871 |
