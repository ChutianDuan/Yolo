# 多路实时监测验收报告

- 开始时间：2026-09-14T02:25:15.195799+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：8
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.004/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_022515_195816 | running | 24.595 | 1.933 | 0.700 | 163.75 | 162 | 0 | 2 |
| acceptance-2-20260914_022515_195816 | running | 23.395 | 1.766 | 0.733 | 189.28 | 199 | 0 | 2 |
| acceptance-3-20260914_022515_195816 | running | 26.928 | 2.033 | 0.767 | 147.03 | 92 | 0 | 2 |
| acceptance-4-20260914_022515_195816 | running | 26.028 | 2.000 | 0.733 | 189.57 | 120 | 0 | 2 |
| acceptance-5-20260914_022515_195816 | running | 24.795 | 1.866 | 0.733 | 129.85 | 157 | 0 | 2 |
| acceptance-6-20260914_022515_195816 | running | 24.362 | 1.900 | 0.667 | 133.49 | 169 | 0 | 2 |
| acceptance-7-20260914_022515_195816 | running | 27.228 | 2.066 | 0.700 | 122.55 | 83 | 0 | 2 |
| acceptance-8-20260914_022515_195816 | running | 26.428 | 2.066 | 0.733 | 125.12 | 107 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2183 | 459d6ded42672c67b11d5a1603d66c71b3010cfea838ae92baadff3e64f4a69b | 459d6ded42672c67b11d5a1603d66c71b3010cfea838ae92baadff3e64f4a69b | True |
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
| requested duration covered | PASS | 30.00597189599648 | >= 30.0 s |
| sampling continuity | PASS | 1.0083248539594933 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 8/8 | 8/8 |
| acceptance-1-20260914_022515_195816: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_022515_195816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_022515_195816: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_022515_195816: processed FPS | FAIL | 24.595104019892354 | >= 25.0 |
| acceptance-1-20260914_022515_195816: low detection FPS | FAIL | 1.932948554408884 | >= 4.0 |
| acceptance-1-20260914_022515_195816: high detection FPS | PASS | 0.6998606834928718 | >= 0.5 |
| acceptance-1-20260914_022515_195816: result age P95 | PASS | 163.752327 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_022515_195816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_022515_195816: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_022515_195816: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_022515_195816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_022515_195816: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_022515_195816: processed FPS | FAIL | 23.395342848190285 | >= 25.0 |
| acceptance-2-20260914_022515_195816: low detection FPS | FAIL | 1.7663150583391527 | >= 4.0 |
| acceptance-2-20260914_022515_195816: high detection FPS | PASS | 0.733187382706818 | >= 0.5 |
| acceptance-2-20260914_022515_195816: result age P95 | PASS | 189.279864 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_022515_195816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_022515_195816: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_022515_195816: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_022515_195816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_022515_195816: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_022515_195816: processed FPS | PASS | 26.92797296486859 | >= 25.0 |
| acceptance-3-20260914_022515_195816: low detection FPS | FAIL | 2.032928652050723 | >= 4.0 |
| acceptance-3-20260914_022515_195816: high detection FPS | PASS | 0.7665140819207643 | >= 0.5 |
| acceptance-3-20260914_022515_195816: result age P95 | PASS | 147.029073 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_022515_195816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_022515_195816: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_022515_195816: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_022515_195816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_022515_195816: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_022515_195816: processed FPS | PASS | 26.028152086092042 | >= 25.0 |
| acceptance-4-20260914_022515_195816: low detection FPS | FAIL | 1.9996019528367766 | >= 4.0 |
| acceptance-4-20260914_022515_195816: high detection FPS | PASS | 0.733187382706818 | >= 0.5 |
| acceptance-4-20260914_022515_195816: result age P95 | PASS | 189.566261 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_022515_195816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_022515_195816: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_022515_195816: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_022515_195816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_022515_195816: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_022515_195816: processed FPS | FAIL | 24.79506421517603 | >= 25.0 |
| acceptance-5-20260914_022515_195816: low detection FPS | FAIL | 1.8662951559809915 | >= 4.0 |
| acceptance-5-20260914_022515_195816: high detection FPS | PASS | 0.733187382706818 | >= 0.5 |
| acceptance-5-20260914_022515_195816: result age P95 | PASS | 129.84765 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_022515_195816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_022515_195816: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_022515_195816: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_022515_195816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_022515_195816: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_022515_195816: processed FPS | FAIL | 24.36181712539473 | >= 25.0 |
| acceptance-6-20260914_022515_195816: low detection FPS | FAIL | 1.8996218551949378 | >= 4.0 |
| acceptance-6-20260914_022515_195816: high detection FPS | PASS | 0.6665339842789255 | >= 0.5 |
| acceptance-6-20260914_022515_195816: result age P95 | PASS | 133.487538 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_022515_195816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_022515_195816: queue bound | PASS | 2 | <= 2 |
| acceptance-7-20260914_022515_195816: every sample observed | PASS | 31 | 31 |
| acceptance-7-20260914_022515_195816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-7-20260914_022515_195816: counters never reset | PASS | True | monotonic |
| acceptance-7-20260914_022515_195816: processed FPS | PASS | 27.227913257794107 | >= 25.0 |
| acceptance-7-20260914_022515_195816: low detection FPS | FAIL | 2.0662553512646693 | >= 4.0 |
| acceptance-7-20260914_022515_195816: high detection FPS | PASS | 0.6998606834928718 | >= 0.5 |
| acceptance-7-20260914_022515_195816: result age P95 | PASS | 122.54949 | <= 300.0 ms (local decode to query) |
| acceptance-7-20260914_022515_195816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-7-20260914_022515_195816: queue bound | PASS | 2 | <= 2 |
| acceptance-8-20260914_022515_195816: every sample observed | PASS | 31 | 31 |
| acceptance-8-20260914_022515_195816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-8-20260914_022515_195816: counters never reset | PASS | True | monotonic |
| acceptance-8-20260914_022515_195816: processed FPS | PASS | 26.4280724766594 | >= 25.0 |
| acceptance-8-20260914_022515_195816: low detection FPS | FAIL | 2.0662553512646693 | >= 4.0 |
| acceptance-8-20260914_022515_195816: high detection FPS | PASS | 0.733187382706818 | >= 0.5 |
| acceptance-8-20260914_022515_195816: result age P95 | PASS | 125.115228 | <= 300.0 ms (local decode to query) |
| acceptance-8-20260914_022515_195816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-8-20260914_022515_195816: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 14.075887392900857 | <= 10.0% |
| process CPU average | PASS | 53.00524018507284 | <= 85.0% of host |
| process RSS growth | PASS | -3.2032702614021655 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003783939988352, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_022515_195816': {'decoded_frame_count': 300, 'processed_frame_count': 263, 'low_res_detection_count': 16, 'high_res_detection_count': 5, 'dropped_frame_count': 36, 'decoder_queue_drop_count': 6, 'processor_coalesced_frame_count': 30, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_022515_195816': {'decoded_frame_count': 300, 'processed_frame_count': 243, 'low_res_detection_count': 15, 'high_res_detection_count': 7, 'dropped_frame_count': 54, 'decoder_queue_drop_count': 16, 'processor_coalesced_frame_count': 38, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_022515_195816': {'decoded_frame_count': 300, 'processed_frame_count': 264, 'low_res_detection_count': 16, 'high_res_detection_count': 6, 'dropped_frame_count': 35, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 27, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_022515_195816': {'decoded_frame_count': 300, 'processed_frame_count': 260, 'low_res_detection_count': 17, 'high_res_detection_count': 6, 'dropped_frame_count': 38, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 33, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_022515_195816': {'decoded_frame_count': 300, 'processed_frame_count': 242, 'low_res_detection_count': 14, 'high_res_detection_count': 5, 'dropped_frame_count': 57, 'decoder_queue_drop_count': 18, 'processor_coalesced_frame_count': 39, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_022515_195816': {'decoded_frame_count': 300, 'processed_frame_count': 231, 'low_res_detection_count': 17, 'high_res_detection_count': 5, 'dropped_frame_count': 68, 'decoder_queue_drop_count': 20, 'processor_coalesced_frame_count': 48, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-7-20260914_022515_195816': {'decoded_frame_count': 300, 'processed_frame_count': 263, 'low_res_detection_count': 17, 'high_res_detection_count': 6, 'dropped_frame_count': 36, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 28, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-8-20260914_022515_195816': {'decoded_frame_count': 300, 'processed_frame_count': 264, 'low_res_detection_count': 17, 'high_res_detection_count': 5, 'dropped_frame_count': 35, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 30, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
