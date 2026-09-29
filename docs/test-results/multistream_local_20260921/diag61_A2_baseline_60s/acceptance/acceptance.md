# 多路实时监测验收报告

- 开始时间：2026-09-21T11:17:16.124194+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：PASS
- 同一批流采样前预热：预热 10.004/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260921_111716_124211 | running | 25.932 | 4.766 | 1.117 | 182.21 | 245 | 0 | 2 |
| acceptance-2-20260921_111716_124211 | running | 26.399 | 4.966 | 1.050 | 166.39 | 216 | 0 | 2 |
| acceptance-3-20260921_111716_124211 | running | 28.565 | 5.000 | 1.200 | 107.18 | 86 | 0 | 2 |
| acceptance-4-20260921_111716_124211 | running | 28.032 | 4.983 | 1.150 | 144.19 | 119 | 0 | 2 |
| acceptance-5-20260921_111716_124211 | running | 26.349 | 4.900 | 1.100 | 169.86 | 219 | 0 | 2 |
| acceptance-6-20260921_111716_124211 | running | 26.032 | 4.833 | 1.033 | 180.33 | 239 | 0 | 2 |

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
| valid sampling timeline | PASS | 61 | >= 2 ordered samples |
| requested duration covered | PASS | 60.003182396998454 | >= 60.0 s |
| sampling continuity | PASS | 1.0069613189989468 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_111716_124211: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_111716_124211: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_111716_124211: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_111716_124211: processed FPS | PASS | 25.931957903583392 | >= 25.0 |
| acceptance-1-20260921_111716_124211: low detection FPS | PASS | 4.766413856314172 | >= 4.0 |
| acceptance-1-20260921_111716_124211: high detection FPS | PASS | 1.1166074418638094 | >= 0.5 |
| acceptance-1-20260921_111716_124211: result age P95 | PASS | 182.213948 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_111716_124211: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_111716_124211: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_111716_124211: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_111716_124211: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_111716_124211: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_111716_124211: processed FPS | PASS | 26.398599819586178 | >= 25.0 |
| acceptance-2-20260921_111716_124211: low detection FPS | PASS | 4.9664032488867935 | >= 4.0 |
| acceptance-2-20260921_111716_124211: high detection FPS | PASS | 1.0499443110062685 | >= 0.5 |
| acceptance-2-20260921_111716_124211: result age P95 | PASS | 166.392838 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_111716_124211: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_111716_124211: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_111716_124211: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_111716_124211: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_111716_124211: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_111716_124211: processed FPS | PASS | 28.56515157245626 | >= 25.0 |
| acceptance-3-20260921_111716_124211: low detection FPS | PASS | 4.999734814315564 | >= 4.0 |
| acceptance-3-20260921_111716_124211: high detection FPS | PASS | 1.1999363554357354 | >= 0.5 |
| acceptance-3-20260921_111716_124211: result age P95 | PASS | 107.181372 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_111716_124211: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_111716_124211: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_111716_124211: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_111716_124211: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_111716_124211: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_111716_124211: processed FPS | PASS | 28.03184652559593 | >= 25.0 |
| acceptance-4-20260921_111716_124211: low detection FPS | PASS | 4.983069031601179 | >= 4.0 |
| acceptance-4-20260921_111716_124211: high detection FPS | PASS | 1.1499390072925797 | >= 0.5 |
| acceptance-4-20260921_111716_124211: result age P95 | PASS | 144.185251 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_111716_124211: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_111716_124211: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_111716_124211: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_111716_124211: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_111716_124211: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_111716_124211: processed FPS | PASS | 26.348602471443023 | >= 25.0 |
| acceptance-5-20260921_111716_124211: low detection FPS | PASS | 4.899740118029253 | >= 4.0 |
| acceptance-5-20260921_111716_124211: high detection FPS | PASS | 1.0999416591494242 | >= 0.5 |
| acceptance-5-20260921_111716_124211: result age P95 | PASS | 169.86148 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_111716_124211: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_111716_124211: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_111716_124211: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_111716_124211: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_111716_124211: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_111716_124211: processed FPS | PASS | 26.031952599869705 | >= 25.0 |
| acceptance-6-20260921_111716_124211: low detection FPS | PASS | 4.8330769871717125 | >= 4.0 |
| acceptance-6-20260921_111716_124211: high detection FPS | PASS | 1.0332785282918833 | >= 0.5 |
| acceptance-6-20260921_111716_124211: result age P95 | PASS | 180.330508 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_111716_124211: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_111716_124211: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 9.218203033838982 | <= 10.0% |
| process CPU average | PASS | 59.25952039081953 | <= 85.0% of host |
| process RSS growth | PASS | -9.65364270510632 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003930412000045, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_111716_124211': {'decoded_frame_count': 300, 'processed_frame_count': 252, 'low_res_detection_count': 43, 'high_res_detection_count': 8, 'dropped_frame_count': 47, 'decoder_queue_drop_count': 7, 'processor_coalesced_frame_count': 40, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_111716_124211': {'decoded_frame_count': 300, 'processed_frame_count': 214, 'low_res_detection_count': 37, 'high_res_detection_count': 9, 'dropped_frame_count': 85, 'decoder_queue_drop_count': 37, 'processor_coalesced_frame_count': 47, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_111716_124211': {'decoded_frame_count': 300, 'processed_frame_count': 274, 'low_res_detection_count': 45, 'high_res_detection_count': 7, 'dropped_frame_count': 26, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 21, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_111716_124211': {'decoded_frame_count': 300, 'processed_frame_count': 255, 'low_res_detection_count': 43, 'high_res_detection_count': 7, 'dropped_frame_count': 44, 'decoder_queue_drop_count': 12, 'processor_coalesced_frame_count': 32, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_111716_124211': {'decoded_frame_count': 300, 'processed_frame_count': 244, 'low_res_detection_count': 43, 'high_res_detection_count': 7, 'dropped_frame_count': 56, 'decoder_queue_drop_count': 13, 'processor_coalesced_frame_count': 43, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_111716_124211': {'decoded_frame_count': 300, 'processed_frame_count': 244, 'low_res_detection_count': 40, 'high_res_detection_count': 6, 'dropped_frame_count': 55, 'decoder_queue_drop_count': 20, 'processor_coalesced_frame_count': 35, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
