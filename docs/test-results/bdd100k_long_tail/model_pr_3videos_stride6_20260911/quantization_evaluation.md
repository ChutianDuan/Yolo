# BDD100K PTQ runtime comparison

Generated: `2026-09-11T01:14:00.829039+00:00`

| Model | Runtime | Score | mAP50 | Precision | Recall | F1 | Mean infer ms | P95 infer ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| full_high_openvino | openvino | 0.001 | 0.320580 | 0.112917 | 0.644562 | 0.192169 | 128.876 | 161.524 |
| full_high_openvino | openvino | 0.010 | 0.316547 | 0.183617 | 0.614436 | 0.282740 | 128.876 | 161.524 |
| full_high_openvino | openvino | 0.050 | 0.304158 | 0.326965 | 0.566614 | 0.414654 | 128.876 | 161.524 |
| full_high_openvino | openvino | 0.100 | 0.290801 | 0.409060 | 0.537781 | 0.464671 | 128.876 | 161.524 |
| full_high_openvino | openvino | 0.150 | 0.276995 | 0.463163 | 0.516902 | 0.488559 | 128.876 | 161.524 |
| full_high_openvino | openvino | 0.200 | 0.265755 | 0.505798 | 0.498707 | 0.502228 | 128.876 | 161.524 |
| full_high_openvino | openvino | 0.250 | 0.256693 | 0.540299 | 0.481209 | 0.509045 | 128.876 | 161.524 |
| high_downscaled_fp32 | openvino | 0.001 | 0.236004 | 0.104650 | 0.545039 | 0.175587 | 40.806 | 59.278 |
| high_downscaled_fp32 | openvino | 0.010 | 0.226730 | 0.212996 | 0.486578 | 0.296292 | 40.806 | 59.278 |
| high_downscaled_fp32 | openvino | 0.050 | 0.206445 | 0.363341 | 0.422549 | 0.390715 | 40.806 | 59.278 |
| high_downscaled_fp32 | openvino | 0.100 | 0.195872 | 0.451028 | 0.390535 | 0.418607 | 40.806 | 59.278 |
| high_downscaled_fp32 | openvino | 0.150 | 0.185373 | 0.512117 | 0.363492 | 0.425190 | 40.806 | 59.278 |
| high_downscaled_fp32 | openvino | 0.200 | 0.177913 | 0.562449 | 0.345198 | 0.427823 | 40.806 | 59.278 |
| high_downscaled_fp32 | openvino | 0.250 | 0.170072 | 0.601715 | 0.327898 | 0.424480 | 40.806 | 59.278 |
| low_fp32 | openvino | 0.001 | 0.268734 | 0.093052 | 0.615331 | 0.161658 | 38.999 | 53.167 |
| low_fp32 | openvino | 0.010 | 0.265949 | 0.150528 | 0.588686 | 0.239751 | 38.999 | 53.167 |
| low_fp32 | openvino | 0.050 | 0.252423 | 0.285362 | 0.529728 | 0.370914 | 38.999 | 53.167 |
| low_fp32 | openvino | 0.100 | 0.238919 | 0.370351 | 0.492046 | 0.422612 | 38.999 | 53.167 |
| low_fp32 | openvino | 0.150 | 0.228618 | 0.432352 | 0.462915 | 0.447112 | 38.999 | 53.167 |
| low_fp32 | openvino | 0.200 | 0.218473 | 0.484233 | 0.438159 | 0.460045 | 38.999 | 53.167 |
| low_fp32 | openvino | 0.250 | 0.211111 | 0.530827 | 0.416882 | 0.467005 | 38.999 | 53.167 |
| u8s8_pc_openvino | openvino | 0.001 | 0.218567 | 0.099737 | 0.535892 | 0.168175 | 23.635 | 32.307 |
| u8s8_pc_openvino | openvino | 0.010 | 0.208844 | 0.210024 | 0.472857 | 0.290860 | 23.635 | 32.307 |
| u8s8_pc_openvino | openvino | 0.050 | 0.193739 | 0.330699 | 0.420262 | 0.370140 | 23.635 | 32.307 |
| u8s8_pc_openvino | openvino | 0.100 | 0.177733 | 0.423865 | 0.378604 | 0.399958 | 23.635 | 32.307 |
| u8s8_pc_openvino | openvino | 0.150 | 0.170508 | 0.479416 | 0.358918 | 0.410507 | 23.635 | 32.307 |
| u8s8_pc_openvino | openvino | 0.200 | 0.163232 | 0.477837 | 0.346192 | 0.401499 | 23.635 | 32.307 |
| u8s8_pc_openvino | openvino | 0.250 | 0.153576 | 0.533311 | 0.316763 | 0.397455 | 23.635 | 32.307 |

Notes: inference latency excludes video decoding, preprocessing, decode and NMS. The minimum-score row is the AP candidate floor; higher rows are operating points. Crowd-overlapping same-class predictions are ignored for both models.

## Per-class measured best F1

AP50 uses the minimum-score candidate floor. Best score is selected only from the reported operating-point grid; F1 ties prefer higher precision, then the lower score.

| Model | Runtime | Class | GT | AP50 | Best score | Precision | Recall | F1 |
|---|---|---|---:|---:|---:|---:|---:|---:|
| full_high_openvino | openvino | bike | 301 | 0.129983 | 0.250 | 0.213333 | 0.212625 | 0.212978 |
| full_high_openvino | openvino | bus | 319 | 0.573221 | 0.100 | 0.544944 | 0.608150 | 0.574815 |
| full_high_openvino | openvino | car | 3697 | 0.467542 | 0.250 | 0.576675 | 0.556397 | 0.566355 |
| full_high_openvino | openvino | motor | 338 | 0.216114 | 0.250 | 0.353909 | 0.254438 | 0.296041 |
| full_high_openvino | openvino | person | 3666 | 0.369660 | 0.250 | 0.482356 | 0.473541 | 0.477908 |
| full_high_openvino | openvino | rider | 374 | 0.082612 | 0.100 | 0.235988 | 0.213904 | 0.224404 |
| full_high_openvino | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| full_high_openvino | openvino | truck | 1190 | 0.725504 | 0.100 | 0.742397 | 0.697479 | 0.719237 |
| high_downscaled_fp32 | openvino | bike | 301 | 0.106866 | 0.100 | 0.303030 | 0.199336 | 0.240481 |
| high_downscaled_fp32 | openvino | bus | 319 | 0.337594 | 0.150 | 0.745763 | 0.275862 | 0.402746 |
| high_downscaled_fp32 | openvino | car | 3697 | 0.363756 | 0.250 | 0.581275 | 0.394644 | 0.470114 |
| high_downscaled_fp32 | openvino | motor | 338 | 0.104889 | 0.050 | 0.277551 | 0.201183 | 0.233276 |
| high_downscaled_fp32 | openvino | person | 3666 | 0.313851 | 0.150 | 0.503949 | 0.348063 | 0.411746 |
| high_downscaled_fp32 | openvino | rider | 374 | 0.047094 | 0.150 | 0.272727 | 0.096257 | 0.142292 |
| high_downscaled_fp32 | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| high_downscaled_fp32 | openvino | truck | 1190 | 0.613979 | 0.050 | 0.731293 | 0.542017 | 0.622587 |
| low_fp32 | openvino | bike | 301 | 0.066749 | 0.250 | 0.197133 | 0.182724 | 0.189655 |
| low_fp32 | openvino | bus | 319 | 0.409990 | 0.100 | 0.401070 | 0.470219 | 0.432900 |
| low_fp32 | openvino | car | 3697 | 0.420746 | 0.250 | 0.538802 | 0.493914 | 0.515382 |
| low_fp32 | openvino | motor | 338 | 0.171946 | 0.250 | 0.305842 | 0.263314 | 0.282989 |
| low_fp32 | openvino | person | 3666 | 0.342183 | 0.250 | 0.515835 | 0.386525 | 0.441915 |
| low_fp32 | openvino | rider | 374 | 0.063812 | 0.100 | 0.169643 | 0.203209 | 0.184915 |
| low_fp32 | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| low_fp32 | openvino | truck | 1190 | 0.674449 | 0.150 | 0.788313 | 0.600840 | 0.681927 |
| u8s8_pc_openvino | openvino | bike | 301 | 0.094966 | 0.150 | 0.300000 | 0.169435 | 0.216561 |
| u8s8_pc_openvino | openvino | bus | 319 | 0.333894 | 0.150 | 0.558442 | 0.269592 | 0.363636 |
| u8s8_pc_openvino | openvino | car | 3697 | 0.324619 | 0.150 | 0.427610 | 0.429808 | 0.428706 |
| u8s8_pc_openvino | openvino | motor | 338 | 0.075274 | 0.010 | 0.167401 | 0.224852 | 0.191919 |
| u8s8_pc_openvino | openvino | person | 3666 | 0.298318 | 0.150 | 0.494365 | 0.346972 | 0.407758 |
| u8s8_pc_openvino | openvino | rider | 374 | 0.030692 | 0.050 | 0.111406 | 0.112299 | 0.111851 |
| u8s8_pc_openvino | openvino | train | 173 | 0.000000 | 0.010 | 0.000000 | 0.000000 | 0.000000 |
| u8s8_pc_openvino | openvino | truck | 1190 | 0.590772 | 0.050 | 0.693856 | 0.550420 | 0.613871 |
