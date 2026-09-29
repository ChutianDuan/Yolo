# 多路实时监测验收报告

- 开始时间：2026-09-14T02:03:38.851509+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.003/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_020338_851523 | running | 26.430 | 3.033 | 1.067 | 140.52 | 107 | 0 | 2 |
| acceptance-2-20260914_020338_851523 | running | 23.831 | 2.766 | 1.033 | 135.41 | 186 | 0 | 2 |
| acceptance-3-20260914_020338_851523 | running | 27.130 | 3.166 | 1.167 | 133.18 | 86 | 0 | 2 |
| acceptance-4-20260914_020338_851523 | running | 27.197 | 3.133 | 1.167 | 111.08 | 85 | 0 | 2 |
| acceptance-5-20260914_020338_851523 | running | 24.797 | 3.000 | 1.033 | 128.91 | 156 | 0 | 2 |
| acceptance-6-20260914_020338_851523 | running | 23.831 | 2.966 | 0.933 | 132.48 | 185 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2183 | 7e62bb8cc879ed105ca4da64bd30e93c5a5521b213afb70deb805e4e3a542b3d | 7e62bb8cc879ed105ca4da64bd30e93c5a5521b213afb70deb805e4e3a542b3d | True |
| model | best.onnx | 38399808 | 96ecf0117bc10499c611fbabc38dbf92457bfaec53b9e099fe67041cbb26fc59 | 96ecf0117bc10499c611fbabc38dbf92457bfaec53b9e099fe67041cbb26fc59 | True |
| model | best_640x384.onnx | 38114065 | 439a56ee159a52ffafdce6befa4e8486c8e311f8d6095ffb9cbbad0443fdfcbf | 439a56ee159a52ffafdce6befa4e8486c8e311f8d6095ffb9cbbad0443fdfcbf | True |

## 验收门槛

| gate | result | actual | target |
| --- | --- | --- | --- |
| collection and cleanup completed | PASS | [] | no errors |
| reproducibility evidence recorded | PASS | {'config_files': 1, 'model_files': 2, 'cpu_identity_recorded': True} | config and >=1 model fingerprint plus collector CPU identity |
| artifact fingerprints stable | PASS | [] | start/end fingerprints match after cleanup |
| lifecycle churn completed | PASS | disabled | disabled |
| valid sampling timeline | PASS | 31 | >= 2 ordered samples |
| requested duration covered | PASS | 30.003316976013593 | >= 30.0 s |
| sampling continuity | PASS | 1.006327565992251 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_020338_851523: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_020338_851523: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_020338_851523: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_020338_851523: processed FPS | PASS | 26.430411032019247 | >= 25.0 |
| acceptance-1-20260914_020338_851523: low detection FPS | FAIL | 3.032997987280897 | >= 4.0 |
| acceptance-1-20260914_020338_851523: high detection FPS | PASS | 1.0665487427800957 | >= 0.5 |
| acceptance-1-20260914_020338_851523: result age P95 | PASS | 140.524183 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_020338_851523: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_020338_851523: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_020338_851523: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_020338_851523: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_020338_851523: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_020338_851523: processed FPS | FAIL | 23.830698471492763 | >= 25.0 |
| acceptance-2-20260914_020338_851523: low detection FPS | FAIL | 2.766360801585873 | >= 4.0 |
| acceptance-2-20260914_020338_851523: high detection FPS | PASS | 1.0332190945682176 | >= 0.5 |
| acceptance-2-20260914_020338_851523: result age P95 | PASS | 135.41142 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_020338_851523: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_020338_851523: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_020338_851523: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_020338_851523: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_020338_851523: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_020338_851523: processed FPS | PASS | 27.130333644468685 | >= 25.0 |
| acceptance-3-20260914_020338_851523: low detection FPS | FAIL | 3.166316580128409 | >= 4.0 |
| acceptance-3-20260914_020338_851523: high detection FPS | PASS | 1.1665376874157296 | >= 0.5 |
| acceptance-3-20260914_020338_851523: result age P95 | PASS | 133.18352 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_020338_851523: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_020338_851523: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_020338_851523: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_020338_851523: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_020338_851523: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_020338_851523: processed FPS | PASS | 27.19699294089244 | >= 25.0 |
| acceptance-4-20260914_020338_851523: low detection FPS | FAIL | 3.132986931916531 | >= 4.0 |
| acceptance-4-20260914_020338_851523: high detection FPS | PASS | 1.1665376874157296 | >= 0.5 |
| acceptance-4-20260914_020338_851523: result age P95 | PASS | 111.076842 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_020338_851523: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_020338_851523: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_020338_851523: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_020338_851523: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_020338_851523: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_020338_851523: processed FPS | FAIL | 24.797258269637226 | >= 25.0 |
| acceptance-5-20260914_020338_851523: low detection FPS | FAIL | 2.9996683390690193 | >= 4.0 |
| acceptance-5-20260914_020338_851523: high detection FPS | PASS | 1.0332190945682176 | >= 0.5 |
| acceptance-5-20260914_020338_851523: result age P95 | PASS | 128.914084 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_020338_851523: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_020338_851523: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_020338_851523: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_020338_851523: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_020338_851523: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_020338_851523: processed FPS | FAIL | 23.830698471492763 | >= 25.0 |
| acceptance-6-20260914_020338_851523: low detection FPS | FAIL | 2.966338690857141 | >= 4.0 |
| acceptance-6-20260914_020338_851523: high detection FPS | PASS | 0.9332301499325837 | >= 0.5 |
| acceptance-6-20260914_020338_851523: result age P95 | PASS | 132.482339 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_020338_851523: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_020338_851523: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 12.377450980392155 | <= 10.0% |
| process CPU average | PASS | 49.93478545299991 | <= 85.0% of host |
| process RSS growth | PASS | 0.1336574981251663 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.002883787965402, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_020338_851523': {'decoded_frame_count': 300, 'processed_frame_count': 277, 'low_res_detection_count': 29, 'high_res_detection_count': 11, 'dropped_frame_count': 23, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 18, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_020338_851523': {'decoded_frame_count': 300, 'processed_frame_count': 243, 'low_res_detection_count': 27, 'high_res_detection_count': 10, 'dropped_frame_count': 55, 'decoder_queue_drop_count': 14, 'processor_coalesced_frame_count': 40, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_020338_851523': {'decoded_frame_count': 300, 'processed_frame_count': 253, 'low_res_detection_count': 28, 'high_res_detection_count': 11, 'dropped_frame_count': 46, 'decoder_queue_drop_count': 6, 'processor_coalesced_frame_count': 40, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_020338_851523': {'decoded_frame_count': 300, 'processed_frame_count': 269, 'low_res_detection_count': 29, 'high_res_detection_count': 10, 'dropped_frame_count': 30, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 25, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_020338_851523': {'decoded_frame_count': 300, 'processed_frame_count': 260, 'low_res_detection_count': 28, 'high_res_detection_count': 11, 'dropped_frame_count': 39, 'decoder_queue_drop_count': 6, 'processor_coalesced_frame_count': 33, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_020338_851523': {'decoded_frame_count': 300, 'processed_frame_count': 267, 'low_res_detection_count': 28, 'high_res_detection_count': 10, 'dropped_frame_count': 32, 'decoder_queue_drop_count': 15, 'processor_coalesced_frame_count': 17, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
