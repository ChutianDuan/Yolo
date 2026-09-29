# 多路实时监测验收报告

- 开始时间：2026-09-14T07:56:45.727392+00:00
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
| acceptance-1-20260914_075645_727411 | running | 21.796 | 3.866 | 0.733 | 221.00 | 246 | 0 | 2 |
| acceptance-2-20260914_075645_727411 | running | 21.230 | 3.633 | 0.667 | 214.69 | 264 | 0 | 2 |
| acceptance-3-20260914_075645_727411 | running | 25.862 | 4.499 | 0.700 | 196.42 | 126 | 0 | 2 |
| acceptance-4-20260914_075645_727411 | running | 25.096 | 4.466 | 0.767 | 178.05 | 147 | 0 | 2 |
| acceptance-5-20260914_075645_727411 | running | 22.896 | 4.199 | 0.767 | 232.34 | 214 | 0 | 2 |
| acceptance-6-20260914_075645_727411 | running | 21.396 | 3.733 | 0.667 | 207.41 | 258 | 0 | 2 |

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
| requested duration covered | PASS | 30.005016194889322 | >= 30.0 s |
| sampling continuity | PASS | 1.0376340220682323 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_075645_727411: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_075645_727411: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_075645_727411: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_075645_727411: processed FPS | FAIL | 21.796355507763202 | >= 25.0 |
| acceptance-1-20260914_075645_727411: low detection FPS | FAIL | 3.8660202429671737 | >= 4.0 |
| acceptance-1-20260914_075645_727411: high detection FPS | PASS | 0.7332107357351536 | >= 0.5 |
| acceptance-1-20260914_075645_727411: result age P95 | PASS | 221.000253 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_075645_727411: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_075645_727411: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_075645_727411: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_075645_727411: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_075645_727411: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_075645_727411: processed FPS | FAIL | 21.22978357560422 | >= 25.0 |
| acceptance-2-20260914_075645_727411: low detection FPS | FAIL | 3.632725917960534 | >= 4.0 |
| acceptance-2-20260914_075645_727411: high detection FPS | PASS | 0.6665552143046851 | >= 0.5 |
| acceptance-2-20260914_075645_727411: result age P95 | PASS | 214.68673 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_075645_727411: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_075645_727411: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_075645_727411: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_075645_727411: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_075645_727411: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_075645_727411: processed FPS | PASS | 25.862342315021785 | >= 25.0 |
| acceptance-3-20260914_075645_727411: low detection FPS | PASS | 4.499247696556624 | >= 4.0 |
| acceptance-3-20260914_075645_727411: high detection FPS | PASS | 0.6998829750199194 | >= 0.5 |
| acceptance-3-20260914_075645_727411: result age P95 | PASS | 196.418067 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_075645_727411: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_075645_727411: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_075645_727411: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_075645_727411: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_075645_727411: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_075645_727411: processed FPS | PASS | 25.095803818571394 | >= 25.0 |
| acceptance-4-20260914_075645_727411: low detection FPS | PASS | 4.465919935841391 | >= 4.0 |
| acceptance-4-20260914_075645_727411: high detection FPS | PASS | 0.7665384964503879 | >= 0.5 |
| acceptance-4-20260914_075645_727411: result age P95 | PASS | 178.05252 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_075645_727411: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_075645_727411: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_075645_727411: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_075645_727411: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_075645_727411: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_075645_727411: processed FPS | FAIL | 22.896171611365933 | >= 25.0 |
| acceptance-5-20260914_075645_727411: low detection FPS | PASS | 4.199297850119517 | >= 4.0 |
| acceptance-5-20260914_075645_727411: high detection FPS | PASS | 0.7665384964503879 | >= 0.5 |
| acceptance-5-20260914_075645_727411: result age P95 | PASS | 232.340077 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_075645_727411: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_075645_727411: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_075645_727411: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_075645_727411: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_075645_727411: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_075645_727411: processed FPS | FAIL | 21.396422379180393 | >= 25.0 |
| acceptance-6-20260914_075645_727411: low detection FPS | FAIL | 3.7327092001062367 | >= 4.0 |
| acceptance-6-20260914_075645_727411: high detection FPS | PASS | 0.6665552143046851 | >= 0.5 |
| acceptance-6-20260914_075645_727411: result age P95 | PASS | 207.411225 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_075645_727411: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_075645_727411: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 17.91237113402063 | <= 10.0% |
| process CPU average | PASS | 60.2849145583116 | <= 85.0% of host |
| process RSS growth | PASS | -1.6780575888783178 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00364865316078, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_075645_727411': {'decoded_frame_count': 300, 'processed_frame_count': 211, 'low_res_detection_count': 35, 'high_res_detection_count': 4, 'dropped_frame_count': 88, 'decoder_queue_drop_count': 17, 'processor_coalesced_frame_count': 71, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_075645_727411': {'decoded_frame_count': 300, 'processed_frame_count': 219, 'low_res_detection_count': 32, 'high_res_detection_count': 4, 'dropped_frame_count': 78, 'decoder_queue_drop_count': 28, 'processor_coalesced_frame_count': 50, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_075645_727411': {'decoded_frame_count': 300, 'processed_frame_count': 213, 'low_res_detection_count': 37, 'high_res_detection_count': 3, 'dropped_frame_count': 85, 'decoder_queue_drop_count': 15, 'processor_coalesced_frame_count': 70, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_075645_727411': {'decoded_frame_count': 300, 'processed_frame_count': 227, 'low_res_detection_count': 35, 'high_res_detection_count': 5, 'dropped_frame_count': 72, 'decoder_queue_drop_count': 14, 'processor_coalesced_frame_count': 58, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_075645_727411': {'decoded_frame_count': 300, 'processed_frame_count': 201, 'low_res_detection_count': 31, 'high_res_detection_count': 4, 'dropped_frame_count': 97, 'decoder_queue_drop_count': 31, 'processor_coalesced_frame_count': 66, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_075645_727411': {'decoded_frame_count': 300, 'processed_frame_count': 209, 'low_res_detection_count': 27, 'high_res_detection_count': 5, 'dropped_frame_count': 90, 'decoder_queue_drop_count': 39, 'processor_coalesced_frame_count': 51, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
