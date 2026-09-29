# 多路实时监测验收报告

- 开始时间：2026-09-14T07:33:33.619056+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.004/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_073333_619073 | running | 24.297 | 3.900 | 0.067 | 134.04 | 171 | 0 | 2 |
| acceptance-2-20260914_073333_619073 | running | 23.064 | 3.600 | 0.000 | 178.22 | 208 | 0 | 2 |
| acceptance-3-20260914_073333_619073 | running | 26.964 | 4.000 | 0.100 | 120.54 | 92 | 0 | 2 |
| acceptance-4-20260914_073333_619073 | running | 25.697 | 4.066 | 0.000 | 138.83 | 129 | 0 | 2 |
| acceptance-5-20260914_073333_619073 | running | 24.364 | 3.933 | 0.033 | 124.02 | 169 | 0 | 2 |
| acceptance-6-20260914_073333_619073 | running | 22.731 | 3.366 | 0.067 | 172.44 | 219 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2183 | e3265877db4a27c66ee71202dc9eac403a4b2b086a9fc5a465e89e40d2e9b06b | e3265877db4a27c66ee71202dc9eac403a4b2b086a9fc5a465e89e40d2e9b06b | True |
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
| requested duration covered | PASS | 30.003512317081913 | >= 30.0 s |
| sampling continuity | PASS | 1.023484901059419 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_073333_619073: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_073333_619073: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_073333_619073: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_073333_619073: processed FPS | FAIL | 24.297155356206684 | >= 25.0 |
| acceptance-1-20260914_073333_619073: low detection FPS | FAIL | 3.899543452230702 | >= 4.0 |
| acceptance-1-20260914_073333_619073: high detection FPS | FAIL | 0.06665886243129406 | >= 0.5 |
| acceptance-1-20260914_073333_619073: result age P95 | PASS | 134.03537 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_073333_619073: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_073333_619073: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_073333_619073: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_073333_619073: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_073333_619073: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_073333_619073: processed FPS | FAIL | 23.06396640122774 | >= 25.0 |
| acceptance-2-20260914_073333_619073: low detection FPS | FAIL | 3.599578571289879 | >= 4.0 |
| acceptance-2-20260914_073333_619073: high detection FPS | FAIL | 0.0 | >= 0.5 |
| acceptance-2-20260914_073333_619073: result age P95 | PASS | 178.220258 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_073333_619073: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_073333_619073: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_073333_619073: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_073333_619073: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_073333_619073: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_073333_619073: processed FPS | PASS | 26.963509853458444 | >= 25.0 |
| acceptance-3-20260914_073333_619073: low detection FPS | FAIL | 3.9995317458776434 | >= 4.0 |
| acceptance-3-20260914_073333_619073: high detection FPS | FAIL | 0.09998829364694108 | >= 0.5 |
| acceptance-3-20260914_073333_619073: result age P95 | PASS | 120.544427 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_073333_619073: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_073333_619073: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_073333_619073: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_073333_619073: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_073333_619073: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_073333_619073: processed FPS | PASS | 25.696991467263857 | >= 25.0 |
| acceptance-4-20260914_073333_619073: low detection FPS | PASS | 4.066190608308937 | >= 4.0 |
| acceptance-4-20260914_073333_619073: high detection FPS | FAIL | 0.0 | >= 0.5 |
| acceptance-4-20260914_073333_619073: result age P95 | PASS | 138.825679 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_073333_619073: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_073333_619073: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_073333_619073: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_073333_619073: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_073333_619073: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_073333_619073: processed FPS | FAIL | 24.363814218637977 | >= 25.0 |
| acceptance-5-20260914_073333_619073: low detection FPS | FAIL | 3.932872883446349 | >= 4.0 |
| acceptance-5-20260914_073333_619073: high detection FPS | FAIL | 0.03332943121564703 | >= 0.5 |
| acceptance-5-20260914_073333_619073: result age P95 | PASS | 124.022457 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_073333_619073: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_073333_619073: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_073333_619073: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_073333_619073: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_073333_619073: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_073333_619073: processed FPS | FAIL | 22.730672089071273 | >= 25.0 |
| acceptance-6-20260914_073333_619073: low detection FPS | FAIL | 3.36627255278035 | >= 4.0 |
| acceptance-6-20260914_073333_619073: high detection FPS | FAIL | 0.06665886243129406 | >= 0.5 |
| acceptance-6-20260914_073333_619073: result age P95 | PASS | 172.444766 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_073333_619073: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_073333_619073: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 15.698393077873913 | <= 10.0% |
| process CPU average | PASS | 49.962335546464494 | <= 85.0% of host |
| process RSS growth | PASS | 1.9186758209590717 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00367511389777, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_073333_619073': {'decoded_frame_count': 300, 'processed_frame_count': 244, 'low_res_detection_count': 34, 'high_res_detection_count': 0, 'dropped_frame_count': 55, 'decoder_queue_drop_count': 19, 'processor_coalesced_frame_count': 36, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_073333_619073': {'decoded_frame_count': 300, 'processed_frame_count': 245, 'low_res_detection_count': 36, 'high_res_detection_count': 0, 'dropped_frame_count': 54, 'decoder_queue_drop_count': 13, 'processor_coalesced_frame_count': 41, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_073333_619073': {'decoded_frame_count': 300, 'processed_frame_count': 270, 'low_res_detection_count': 39, 'high_res_detection_count': 0, 'dropped_frame_count': 28, 'decoder_queue_drop_count': 3, 'processor_coalesced_frame_count': 25, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_073333_619073': {'decoded_frame_count': 300, 'processed_frame_count': 259, 'low_res_detection_count': 38, 'high_res_detection_count': 1, 'dropped_frame_count': 40, 'decoder_queue_drop_count': 6, 'processor_coalesced_frame_count': 34, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_073333_619073': {'decoded_frame_count': 300, 'processed_frame_count': 225, 'low_res_detection_count': 34, 'high_res_detection_count': 0, 'dropped_frame_count': 74, 'decoder_queue_drop_count': 19, 'processor_coalesced_frame_count': 55, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_073333_619073': {'decoded_frame_count': 300, 'processed_frame_count': 237, 'low_res_detection_count': 31, 'high_res_detection_count': 1, 'dropped_frame_count': 61, 'decoder_queue_drop_count': 29, 'processor_coalesced_frame_count': 31, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
