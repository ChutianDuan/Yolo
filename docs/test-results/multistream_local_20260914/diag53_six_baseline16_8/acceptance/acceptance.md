# 多路实时监测验收报告

- 开始时间：2026-09-14T07:29:29.366681+00:00
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
| acceptance-1-20260914_072929_366707 | running | 23.063 | 2.033 | 0.700 | 210.93 | 209 | 0 | 2 |
| acceptance-2-20260914_072929_366707 | running | 21.764 | 1.766 | 0.633 | 226.05 | 249 | 0 | 2 |
| acceptance-3-20260914_072929_366707 | running | 24.896 | 2.133 | 0.600 | 146.72 | 154 | 0 | 2 |
| acceptance-4-20260914_072929_366707 | running | 24.363 | 2.100 | 0.433 | 156.69 | 170 | 0 | 2 |
| acceptance-5-20260914_072929_366707 | running | 23.297 | 2.033 | 0.567 | 124.84 | 202 | 0 | 2 |
| acceptance-6-20260914_072929_366707 | running | 21.797 | 1.766 | 0.567 | 176.82 | 248 | 0 | 2 |

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
| requested duration covered | PASS | 30.004303507041186 | >= 30.0 s |
| sampling continuity | PASS | 1.0071451419498771 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_072929_366707: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_072929_366707: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_072929_366707: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_072929_366707: processed FPS | FAIL | 23.063358222516534 | >= 25.0 |
| acceptance-1-20260914_072929_366707: low detection FPS | FAIL | 2.0330416930253015 | >= 4.0 |
| acceptance-1-20260914_072929_366707: high detection FPS | PASS | 0.6998995992382185 | >= 0.5 |
| acceptance-1-20260914_072929_366707: result age P95 | PASS | 210.92934 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_072929_366707: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_072929_366707: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_072929_366707: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_072929_366707: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_072929_366707: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_072929_366707: processed FPS | FAIL | 21.763544681074126 | >= 25.0 |
| acceptance-2-20260914_072929_366707: low detection FPS | FAIL | 1.7664132742678849 | >= 4.0 |
| acceptance-2-20260914_072929_366707: high detection FPS | PASS | 0.6332424945488644 | >= 0.5 |
| acceptance-2-20260914_072929_366707: result age P95 | PASS | 226.046204 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_072929_366707: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_072929_366707: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_072929_366707: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_072929_366707: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_072929_366707: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_072929_366707: processed FPS | FAIL | 24.896428601473772 | >= 25.0 |
| acceptance-3-20260914_072929_366707: low detection FPS | FAIL | 2.1330273500593324 | >= 4.0 |
| acceptance-3-20260914_072929_366707: high detection FPS | PASS | 0.5999139422041873 | >= 0.5 |
| acceptance-3-20260914_072929_366707: result age P95 | PASS | 146.720667 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_072929_366707: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_072929_366707: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_072929_366707: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_072929_366707: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_072929_366707: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_072929_366707: processed FPS | FAIL | 24.36317176395894 | >= 25.0 |
| acceptance-4-20260914_072929_366707: low detection FPS | FAIL | 2.0996987977146553 | >= 4.0 |
| acceptance-4-20260914_072929_366707: high detection FPS | FAIL | 0.43327118048080193 | >= 0.5 |
| acceptance-4-20260914_072929_366707: result age P95 | PASS | 156.690123 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_072929_366707: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_072929_366707: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_072929_366707: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_072929_366707: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_072929_366707: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_072929_366707: processed FPS | FAIL | 23.296658088929274 | >= 25.0 |
| acceptance-5-20260914_072929_366707: low detection FPS | FAIL | 2.0330416930253015 | >= 4.0 |
| acceptance-5-20260914_072929_366707: high detection FPS | PASS | 0.5665853898595102 | >= 0.5 |
| acceptance-5-20260914_072929_366707: result age P95 | PASS | 124.835967 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_072929_366707: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_072929_366707: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_072929_366707: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_072929_366707: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_072929_366707: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_072929_366707: processed FPS | FAIL | 21.796873233418804 | >= 25.0 |
| acceptance-6-20260914_072929_366707: low detection FPS | FAIL | 1.7664132742678849 | >= 4.0 |
| acceptance-6-20260914_072929_366707: high detection FPS | PASS | 0.5665853898595102 | >= 0.5 |
| acceptance-6-20260914_072929_366707: result age P95 | PASS | 176.820681 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_072929_366707: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_072929_366707: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 12.583668005354756 | <= 10.0% |
| process CPU average | PASS | 49.214271155106076 | <= 85.0% of host |
| process RSS growth | FAIL | 5.517915454161761 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00358143192716, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_072929_366707': {'decoded_frame_count': 300, 'processed_frame_count': 260, 'low_res_detection_count': 19, 'high_res_detection_count': 4, 'dropped_frame_count': 38, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 33, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_072929_366707': {'decoded_frame_count': 300, 'processed_frame_count': 242, 'low_res_detection_count': 16, 'high_res_detection_count': 6, 'dropped_frame_count': 56, 'decoder_queue_drop_count': 28, 'processor_coalesced_frame_count': 28, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_072929_366707': {'decoded_frame_count': 300, 'processed_frame_count': 245, 'low_res_detection_count': 19, 'high_res_detection_count': 4, 'dropped_frame_count': 53, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 45, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_072929_366707': {'decoded_frame_count': 300, 'processed_frame_count': 231, 'low_res_detection_count': 17, 'high_res_detection_count': 4, 'dropped_frame_count': 68, 'decoder_queue_drop_count': 20, 'processor_coalesced_frame_count': 48, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_072929_366707': {'decoded_frame_count': 300, 'processed_frame_count': 262, 'low_res_detection_count': 20, 'high_res_detection_count': 2, 'dropped_frame_count': 37, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 32, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_072929_366707': {'decoded_frame_count': 300, 'processed_frame_count': 242, 'low_res_detection_count': 17, 'high_res_detection_count': 4, 'dropped_frame_count': 56, 'decoder_queue_drop_count': 17, 'processor_coalesced_frame_count': 39, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
