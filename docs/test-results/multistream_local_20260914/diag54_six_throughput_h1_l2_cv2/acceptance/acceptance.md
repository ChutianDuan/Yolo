# 多路实时监测验收报告

- 开始时间：2026-09-14T07:59:37.483334+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.005/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_075937_483362 | running | 21.964 | 4.433 | 0.767 | 155.27 | 241 | 0 | 2 |
| acceptance-2-20260914_075937_483362 | running | 19.164 | 3.633 | 0.700 | 237.42 | 324 | 0 | 2 |
| acceptance-3-20260914_075937_483362 | running | 23.364 | 4.533 | 0.833 | 155.63 | 200 | 0 | 2 |
| acceptance-4-20260914_075937_483362 | running | 20.797 | 4.199 | 0.700 | 139.45 | 277 | 0 | 2 |
| acceptance-5-20260914_075937_483362 | running | 21.997 | 4.266 | 0.700 | 223.53 | 241 | 0 | 2 |
| acceptance-6-20260914_075937_483362 | running | 19.064 | 3.900 | 0.600 | 253.86 | 329 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2187 | 19be74da9c34c91f305a99098c3a08bcb8744bcfbac7dc895bed10f80e927c5c | 19be74da9c34c91f305a99098c3a08bcb8744bcfbac7dc895bed10f80e927c5c | True |
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
| requested duration covered | PASS | 30.003783493069932 | >= 30.0 s |
| sampling continuity | PASS | 1.0136841859202832 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_075937_483362: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_075937_483362: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_075937_483362: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_075937_483362: processed FPS | FAIL | 21.96389665830682 | >= 25.0 |
| acceptance-1-20260914_075937_483362: low detection FPS | PASS | 4.432774287640071 | >= 4.0 |
| acceptance-1-20260914_075937_483362: high detection FPS | PASS | 0.766569989591892 | >= 0.5 |
| acceptance-1-20260914_075937_483362: result age P95 | PASS | 155.269841 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_075937_483362: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_075937_483362: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_075937_483362: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_075937_483362: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_075937_483362: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_075937_483362: processed FPS | FAIL | 19.164249739797302 | >= 25.0 |
| acceptance-2-20260914_075937_483362: low detection FPS | FAIL | 3.6328751680659233 | >= 4.0 |
| acceptance-2-20260914_075937_483362: high detection FPS | PASS | 0.6999117296273797 | >= 0.5 |
| acceptance-2-20260914_075937_483362: result age P95 | PASS | 237.417085 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_075937_483362: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_075937_483362: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_075937_483362: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_075937_483362: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_075937_483362: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_075937_483362: processed FPS | FAIL | 23.36372011756158 | >= 25.0 |
| acceptance-3-20260914_075937_483362: low detection FPS | PASS | 4.53276167758684 | >= 4.0 |
| acceptance-3-20260914_075937_483362: high detection FPS | PASS | 0.8332282495564044 | >= 0.5 |
| acceptance-3-20260914_075937_483362: result age P95 | PASS | 155.632083 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_075937_483362: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_075937_483362: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_075937_483362: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_075937_483362: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_075937_483362: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_075937_483362: processed FPS | FAIL | 20.797377108927854 | >= 25.0 |
| acceptance-4-20260914_075937_483362: low detection FPS | PASS | 4.199470377764278 | >= 4.0 |
| acceptance-4-20260914_075937_483362: high detection FPS | PASS | 0.6999117296273797 | >= 0.5 |
| acceptance-4-20260914_075937_483362: result age P95 | PASS | 139.446006 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_075937_483362: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_075937_483362: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_075937_483362: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_075937_483362: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_075937_483362: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_075937_483362: processed FPS | FAIL | 21.997225788289075 | >= 25.0 |
| acceptance-5-20260914_075937_483362: low detection FPS | PASS | 4.266128637728791 | >= 4.0 |
| acceptance-5-20260914_075937_483362: high detection FPS | PASS | 0.6999117296273797 | >= 0.5 |
| acceptance-5-20260914_075937_483362: result age P95 | PASS | 223.525853 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_075937_483362: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_075937_483362: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_075937_483362: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_075937_483362: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_075937_483362: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_075937_483362: processed FPS | FAIL | 19.064262349850534 | >= 25.0 |
| acceptance-6-20260914_075937_483362: low detection FPS | FAIL | 3.8995082079239727 | >= 4.0 |
| acceptance-6-20260914_075937_483362: high detection FPS | PASS | 0.5999243396806112 | >= 0.5 |
| acceptance-6-20260914_075937_483362: result age P95 | PASS | 253.858844 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_075937_483362: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_075937_483362: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 18.40228245363766 | <= 10.0% |
| process CPU average | PASS | 57.862110605366254 | <= 85.0% of host |
| process RSS growth | PASS | 0.38650138737463247 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.004740281961858, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_075937_483362': {'decoded_frame_count': 301, 'processed_frame_count': 222, 'low_res_detection_count': 38, 'high_res_detection_count': 6, 'dropped_frame_count': 77, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 67, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_075937_483362': {'decoded_frame_count': 300, 'processed_frame_count': 194, 'low_res_detection_count': 35, 'high_res_detection_count': 3, 'dropped_frame_count': 105, 'decoder_queue_drop_count': 43, 'processor_coalesced_frame_count': 62, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_075937_483362': {'decoded_frame_count': 300, 'processed_frame_count': 199, 'low_res_detection_count': 35, 'high_res_detection_count': 3, 'dropped_frame_count': 100, 'decoder_queue_drop_count': 28, 'processor_coalesced_frame_count': 72, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_075937_483362': {'decoded_frame_count': 300, 'processed_frame_count': 220, 'low_res_detection_count': 38, 'high_res_detection_count': 5, 'dropped_frame_count': 78, 'decoder_queue_drop_count': 14, 'processor_coalesced_frame_count': 64, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_075937_483362': {'decoded_frame_count': 300, 'processed_frame_count': 204, 'low_res_detection_count': 35, 'high_res_detection_count': 4, 'dropped_frame_count': 95, 'decoder_queue_drop_count': 27, 'processor_coalesced_frame_count': 68, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_075937_483362': {'decoded_frame_count': 300, 'processed_frame_count': 185, 'low_res_detection_count': 26, 'high_res_detection_count': 3, 'dropped_frame_count': 113, 'decoder_queue_drop_count': 62, 'processor_coalesced_frame_count': 51, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
