# 多路实时监测验收报告

- 开始时间：2026-09-21T23:39:24.197783+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.004/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260921_233924_197800 | running | 26.598 | 4.833 | 1.033 | 200.01 | 204 | 0 | 2 |
| acceptance-2-20260921_233924_197800 | running | 25.148 | 4.800 | 1.050 | 190.30 | 294 | 0 | 2 |
| acceptance-3-20260921_233924_197800 | running | 27.964 | 5.000 | 1.200 | 107.01 | 125 | 0 | 2 |
| acceptance-4-20260921_233924_197800 | running | 27.281 | 4.933 | 1.183 | 161.23 | 163 | 0 | 2 |
| acceptance-5-20260921_233924_197800 | running | 26.098 | 4.733 | 1.083 | 189.86 | 233 | 0 | 2 |
| acceptance-6-20260921_233924_197800 | running | 25.631 | 4.866 | 1.067 | 190.02 | 262 | 0 | 2 |

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
| requested duration covered | PASS | 60.00496385899896 | >= 60.0 s |
| sampling continuity | PASS | 1.0075289889937267 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_233924_197800: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_233924_197800: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_233924_197800: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_233924_197800: processed FPS | PASS | 26.597799537890186 | >= 25.0 |
| acceptance-1-20260921_233924_197800: low detection FPS | PASS | 4.832933499992578 | >= 4.0 |
| acceptance-1-20260921_233924_197800: high detection FPS | PASS | 1.0332478517225512 | >= 0.5 |
| acceptance-1-20260921_233924_197800: result age P95 | PASS | 200.011273 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_233924_197800: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_233924_197800: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_233924_197800: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_233924_197800: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_233924_197800: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_233924_197800: processed FPS | PASS | 25.147919487892413 | >= 25.0 |
| acceptance-2-20260921_233924_197800: low detection FPS | PASS | 4.79960292413056 | >= 4.0 |
| acceptance-2-20260921_233924_197800: high detection FPS | PASS | 1.04991313965356 | >= 0.5 |
| acceptance-2-20260921_233924_197800: result age P95 | PASS | 190.301414 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_233924_197800: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_233924_197800: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_233924_197800: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_233924_197800: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_233924_197800: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_233924_197800: processed FPS | PASS | 27.964353148232917 | >= 25.0 |
| acceptance-3-20260921_233924_197800: low detection FPS | PASS | 4.9995863793026665 | >= 4.0 |
| acceptance-3-20260921_233924_197800: high detection FPS | PASS | 1.19990073103264 | >= 0.5 |
| acceptance-3-20260921_233924_197800: result age P95 | PASS | 107.011687 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_233924_197800: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_233924_197800: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_233924_197800: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_233924_197800: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_233924_197800: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_233924_197800: processed FPS | PASS | 27.281076343061553 | >= 25.0 |
| acceptance-4-20260921_233924_197800: low detection FPS | PASS | 4.9329252275786315 | >= 4.0 |
| acceptance-4-20260921_233924_197800: high detection FPS | PASS | 1.1832354431016312 | >= 0.5 |
| acceptance-4-20260921_233924_197800: result age P95 | PASS | 161.226918 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_233924_197800: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_233924_197800: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_233924_197800: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_233924_197800: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_233924_197800: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_233924_197800: processed FPS | PASS | 26.09784089995992 | >= 25.0 |
| acceptance-5-20260921_233924_197800: low detection FPS | PASS | 4.732941772406525 | >= 4.0 |
| acceptance-5-20260921_233924_197800: high detection FPS | PASS | 1.0832437155155779 | >= 0.5 |
| acceptance-5-20260921_233924_197800: result age P95 | PASS | 189.858172 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_233924_197800: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_233924_197800: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_233924_197800: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_233924_197800: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_233924_197800: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_233924_197800: processed FPS | PASS | 25.63121283789167 | >= 25.0 |
| acceptance-6-20260921_233924_197800: low detection FPS | PASS | 4.866264075854596 | >= 4.0 |
| acceptance-6-20260921_233924_197800: high detection FPS | PASS | 1.0665784275845689 | >= 0.5 |
| acceptance-6-20260921_233924_197800: result age P95 | PASS | 190.015501 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_233924_197800: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_233924_197800: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 10.071513706793809 | <= 10.0% |
| process CPU average | PASS | 59.477270428952544 | <= 85.0% of host |
| process RSS growth | PASS | -1.99609257900123 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003745574009372, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_233924_197800': {'decoded_frame_count': 300, 'processed_frame_count': 229, 'low_res_detection_count': 41, 'high_res_detection_count': 7, 'dropped_frame_count': 70, 'decoder_queue_drop_count': 16, 'processor_coalesced_frame_count': 54, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_233924_197800': {'decoded_frame_count': 300, 'processed_frame_count': 204, 'low_res_detection_count': 31, 'high_res_detection_count': 8, 'dropped_frame_count': 93, 'decoder_queue_drop_count': 54, 'processor_coalesced_frame_count': 38, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_233924_197800': {'decoded_frame_count': 300, 'processed_frame_count': 262, 'low_res_detection_count': 44, 'high_res_detection_count': 8, 'dropped_frame_count': 35, 'decoder_queue_drop_count': 7, 'processor_coalesced_frame_count': 28, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_233924_197800': {'decoded_frame_count': 300, 'processed_frame_count': 266, 'low_res_detection_count': 44, 'high_res_detection_count': 9, 'dropped_frame_count': 34, 'decoder_queue_drop_count': 6, 'processor_coalesced_frame_count': 28, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_233924_197800': {'decoded_frame_count': 300, 'processed_frame_count': 264, 'low_res_detection_count': 45, 'high_res_detection_count': 9, 'dropped_frame_count': 36, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 31, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_233924_197800': {'decoded_frame_count': 300, 'processed_frame_count': 242, 'low_res_detection_count': 42, 'high_res_detection_count': 9, 'dropped_frame_count': 57, 'decoder_queue_drop_count': 16, 'processor_coalesced_frame_count': 41, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
