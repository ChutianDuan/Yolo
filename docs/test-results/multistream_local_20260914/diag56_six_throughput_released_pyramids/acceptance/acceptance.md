# 多路实时监测验收报告

- 开始时间：2026-09-14T08:48:18.189008+00:00
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
| acceptance-1-20260914_084818_189027 | running | 23.097 | 4.566 | 0.800 | 160.41 | 208 | 0 | 2 |
| acceptance-2-20260914_084818_189027 | running | 21.697 | 4.266 | 0.867 | 233.01 | 247 | 0 | 2 |
| acceptance-3-20260914_084818_189027 | running | 26.063 | 4.766 | 0.800 | 110.91 | 118 | 0 | 2 |
| acceptance-4-20260914_084818_189027 | running | 25.963 | 4.799 | 1.067 | 113.29 | 121 | 0 | 2 |
| acceptance-5-20260914_084818_189027 | running | 24.163 | 4.699 | 0.833 | 139.50 | 174 | 0 | 2 |
| acceptance-6-20260914_084818_189027 | running | 20.964 | 4.199 | 0.700 | 168.96 | 270 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2187 | c44e4bc259a97f07e59b0d18230ab0e49892769d826e6d21b9774f8dde7ae57e | c44e4bc259a97f07e59b0d18230ab0e49892769d826e6d21b9774f8dde7ae57e | True |
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
| requested duration covered | PASS | 30.004191437968984 | >= 30.0 s |
| sampling continuity | PASS | 1.0059365250635892 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_084818_189027: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_084818_189027: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_084818_189027: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_084818_189027: processed FPS | FAIL | 23.0967730436168 | >= 25.0 |
| acceptance-1-20260914_084818_189027: low detection FPS | PASS | 4.566028725794374 | >= 4.0 |
| acceptance-1-20260914_084818_189027: high detection FPS | PASS | 0.799888243934781 | >= 0.5 |
| acceptance-1-20260914_084818_189027: result age P95 | PASS | 160.406488 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_084818_189027: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_084818_189027: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_084818_189027: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_084818_189027: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_084818_189027: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_084818_189027: processed FPS | FAIL | 21.69696861673093 | >= 25.0 |
| acceptance-2-20260914_084818_189027: low detection FPS | PASS | 4.266070634318831 | >= 4.0 |
| acceptance-2-20260914_084818_189027: high detection FPS | PASS | 0.8665455975960127 | >= 0.5 |
| acceptance-2-20260914_084818_189027: result age P95 | PASS | 233.009623 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_084818_189027: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_084818_189027: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_084818_189027: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_084818_189027: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_084818_189027: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_084818_189027: processed FPS | PASS | 26.063025281541613 | >= 25.0 |
| acceptance-3-20260914_084818_189027: low detection FPS | PASS | 4.766000786778069 | >= 4.0 |
| acceptance-3-20260914_084818_189027: high detection FPS | PASS | 0.799888243934781 | >= 0.5 |
| acceptance-3-20260914_084818_189027: result age P95 | PASS | 110.906153 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_084818_189027: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_084818_189027: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_084818_189027: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_084818_189027: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_084818_189027: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_084818_189027: processed FPS | PASS | 25.963039251049764 | >= 25.0 |
| acceptance-4-20260914_084818_189027: low detection FPS | PASS | 4.799329463608686 | >= 4.0 |
| acceptance-4-20260914_084818_189027: high detection FPS | PASS | 1.0665176585797078 | >= 0.5 |
| acceptance-4-20260914_084818_189027: result age P95 | PASS | 113.2899 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_084818_189027: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_084818_189027: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_084818_189027: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_084818_189027: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_084818_189027: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_084818_189027: processed FPS | FAIL | 24.163290702196505 | >= 25.0 |
| acceptance-5-20260914_084818_189027: low detection FPS | PASS | 4.699343433116838 | >= 4.0 |
| acceptance-5-20260914_084818_189027: high detection FPS | PASS | 0.8332169207653968 | >= 0.5 |
| acceptance-5-20260914_084818_189027: result age P95 | PASS | 139.501578 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_084818_189027: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_084818_189027: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_084818_189027: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_084818_189027: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_084818_189027: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_084818_189027: processed FPS | FAIL | 20.963737726457385 | >= 25.0 |
| acceptance-6-20260914_084818_189027: low detection FPS | PASS | 4.1994132806576 | >= 4.0 |
| acceptance-6-20260914_084818_189027: high detection FPS | PASS | 0.6999022134429334 | >= 0.5 |
| acceptance-6-20260914_084818_189027: result age P95 | PASS | 168.961686 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_084818_189027: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_084818_189027: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 19.565217391304344 | <= 10.0% |
| process CPU average | PASS | 61.07180427907426 | <= 85.0% of host |
| process RSS growth | FAIL | 11.101713412637784 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003111576894298, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_084818_189027': {'decoded_frame_count': 300, 'processed_frame_count': 236, 'low_res_detection_count': 44, 'high_res_detection_count': 10, 'dropped_frame_count': 63, 'decoder_queue_drop_count': 13, 'processor_coalesced_frame_count': 50, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_084818_189027': {'decoded_frame_count': 300, 'processed_frame_count': 239, 'low_res_detection_count': 43, 'high_res_detection_count': 9, 'dropped_frame_count': 60, 'decoder_queue_drop_count': 16, 'processor_coalesced_frame_count': 44, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_084818_189027': {'decoded_frame_count': 300, 'processed_frame_count': 246, 'low_res_detection_count': 44, 'high_res_detection_count': 10, 'dropped_frame_count': 53, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 43, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_084818_189027': {'decoded_frame_count': 300, 'processed_frame_count': 265, 'low_res_detection_count': 47, 'high_res_detection_count': 8, 'dropped_frame_count': 34, 'decoder_queue_drop_count': 4, 'processor_coalesced_frame_count': 30, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_084818_189027': {'decoded_frame_count': 300, 'processed_frame_count': 257, 'low_res_detection_count': 44, 'high_res_detection_count': 10, 'dropped_frame_count': 42, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 32, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_084818_189027': {'decoded_frame_count': 300, 'processed_frame_count': 246, 'low_res_detection_count': 44, 'high_res_detection_count': 6, 'dropped_frame_count': 53, 'decoder_queue_drop_count': 16, 'processor_coalesced_frame_count': 37, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
