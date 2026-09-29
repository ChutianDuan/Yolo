# 多路实时监测验收报告

- 开始时间：2026-09-21T05:39:22.254515+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.003/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260921_053922_254535 | running | 22.298 | 3.783 | 0.450 | 203.07 | 463 | 0 | 2 |
| acceptance-2-20260921_053922_254535 | running | 20.232 | 3.416 | 0.433 | 230.45 | 587 | 0 | 2 |
| acceptance-3-20260921_053922_254535 | running | 25.164 | 4.133 | 0.617 | 117.70 | 290 | 0 | 2 |
| acceptance-4-20260921_053922_254535 | running | 23.948 | 4.066 | 0.483 | 199.96 | 362 | 0 | 2 |
| acceptance-5-20260921_053922_254535 | running | 22.381 | 3.800 | 0.517 | 192.36 | 457 | 0 | 2 |
| acceptance-6-20260921_053922_254535 | running | 20.648 | 3.350 | 0.483 | 230.17 | 561 | 0 | 2 |

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
| requested duration covered | PASS | 60.00526859099955 | >= 60.0 s |
| sampling continuity | PASS | 1.01005536699995 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_053922_254535: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_053922_254535: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_053922_254535: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_053922_254535: processed FPS | FAIL | 22.298042012275776 | >= 25.0 |
| acceptance-1-20260921_053922_254535: low detection FPS | FAIL | 3.783001148569956 | >= 4.0 |
| acceptance-1-20260921_053922_254535: high detection FPS | FAIL | 0.4499604890369551 | >= 0.5 |
| acceptance-1-20260921_053922_254535: result age P95 | PASS | 203.074702 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_053922_254535: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_053922_254535: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_053922_254535: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_053922_254535: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_053922_254535: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_053922_254535: processed FPS | FAIL | 20.231556803365315 | >= 25.0 |
| acceptance-2-20260921_053922_254535: low detection FPS | FAIL | 3.416366676021326 | >= 4.0 |
| acceptance-2-20260921_053922_254535: high detection FPS | FAIL | 0.4332952857392901 | >= 0.5 |
| acceptance-2-20260921_053922_254535: result age P95 | PASS | 230.449159 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_053922_254535: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_053922_254535: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_053922_254535: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_053922_254535: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_053922_254535: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_053922_254535: processed FPS | PASS | 25.164456979474156 | >= 25.0 |
| acceptance-3-20260921_053922_254535: low detection FPS | PASS | 4.132970417820921 | >= 4.0 |
| acceptance-3-20260921_053922_254535: high detection FPS | PASS | 0.6166125220136052 | >= 0.5 |
| acceptance-3-20260921_053922_254535: result age P95 | PASS | 117.700796 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_053922_254535: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_053922_254535: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_053922_254535: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_053922_254535: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_053922_254535: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_053922_254535: processed FPS | FAIL | 23.94789713874461 | >= 25.0 |
| acceptance-4-20260921_053922_254535: low detection FPS | PASS | 4.066309604630261 | >= 4.0 |
| acceptance-4-20260921_053922_254535: high detection FPS | FAIL | 0.4832908956322851 | >= 0.5 |
| acceptance-4-20260921_053922_254535: result age P95 | PASS | 199.959015 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_053922_254535: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_053922_254535: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_053922_254535: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_053922_254535: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_053922_254535: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_053922_254535: processed FPS | FAIL | 22.3813680287641 | >= 25.0 |
| acceptance-5-20260921_053922_254535: low detection FPS | FAIL | 3.799666351867621 | >= 4.0 |
| acceptance-5-20260921_053922_254535: high detection FPS | PASS | 0.5166213022276152 | >= 0.5 |
| acceptance-5-20260921_053922_254535: result age P95 | PASS | 192.358189 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_053922_254535: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_053922_254535: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_053922_254535: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_053922_254535: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_053922_254535: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_053922_254535: processed FPS | FAIL | 20.64818688580694 | >= 25.0 |
| acceptance-6-20260921_053922_254535: low detection FPS | FAIL | 3.3497058628306657 | >= 4.0 |
| acceptance-6-20260921_053922_254535: high detection FPS | FAIL | 0.4832908956322851 | >= 0.5 |
| acceptance-6-20260921_053922_254535: result age P95 | PASS | 230.165718 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_053922_254535: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_053922_254535: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 19.602649006622517 | <= 10.0% |
| process CPU average | PASS | 57.51099278146601 | <= 85.0% of host |
| process RSS growth | PASS | 0.3452090032154341 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003120483999737, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_053922_254535': {'decoded_frame_count': 300, 'processed_frame_count': 222, 'low_res_detection_count': 32, 'high_res_detection_count': 2, 'dropped_frame_count': 77, 'decoder_queue_drop_count': 15, 'processor_coalesced_frame_count': 62, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_053922_254535': {'decoded_frame_count': 300, 'processed_frame_count': 213, 'low_res_detection_count': 26, 'high_res_detection_count': 2, 'dropped_frame_count': 85, 'decoder_queue_drop_count': 42, 'processor_coalesced_frame_count': 43, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_053922_254535': {'decoded_frame_count': 300, 'processed_frame_count': 243, 'low_res_detection_count': 33, 'high_res_detection_count': 2, 'dropped_frame_count': 55, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 46, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_053922_254535': {'decoded_frame_count': 300, 'processed_frame_count': 237, 'low_res_detection_count': 32, 'high_res_detection_count': 1, 'dropped_frame_count': 63, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 53, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_053922_254535': {'decoded_frame_count': 300, 'processed_frame_count': 217, 'low_res_detection_count': 32, 'high_res_detection_count': 3, 'dropped_frame_count': 82, 'decoder_queue_drop_count': 16, 'processor_coalesced_frame_count': 66, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_053922_254535': {'decoded_frame_count': 300, 'processed_frame_count': 195, 'low_res_detection_count': 23, 'high_res_detection_count': 4, 'dropped_frame_count': 104, 'decoder_queue_drop_count': 47, 'processor_coalesced_frame_count': 57, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
