# 多路实时监测验收报告

- 开始时间：2026-09-22T01:21:32.266414+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.007/10.000 s，观察 3 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260922_012132_266429 | running | 27.147 | 4.883 | 1.117 | 147.20 | 171 | 0 | 2 |
| acceptance-2-20260922_012132_266429 | running | 26.981 | 4.916 | 1.033 | 106.54 | 181 | 0 | 2 |
| acceptance-3-20260922_012132_266429 | running | 28.864 | 5.000 | 1.167 | 99.38 | 67 | 0 | 2 |
| acceptance-4-20260922_012132_266429 | running | 28.181 | 4.950 | 1.167 | 104.34 | 110 | 0 | 2 |
| acceptance-5-20260922_012132_266429 | running | 26.064 | 4.833 | 1.050 | 96.88 | 237 | 0 | 2 |
| acceptance-6-20260922_012132_266429 | running | 25.748 | 4.766 | 1.033 | 135.71 | 255 | 0 | 2 |

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
| valid sampling timeline | PASS | 13 | >= 2 ordered samples |
| requested duration covered | PASS | 60.005622215991025 | >= 60.0 s |
| sampling continuity | PASS | 5.010176524010603 | maximum gap <= 10.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260922_012132_266429: every sample observed | PASS | 13 | 13 |
| acceptance-1-20260922_012132_266429: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260922_012132_266429: counters never reset | PASS | True | monotonic |
| acceptance-1-20260922_012132_266429: processed FPS | PASS | 27.147456185628624 | >= 25.0 |
| acceptance-1-20260922_012132_266429: low detection FPS | PASS | 4.88287579029416 | >= 4.0 |
| acceptance-1-20260922_012132_266429: high detection FPS | PASS | 1.116562040783989 | >= 0.5 |
| acceptance-1-20260922_012132_266429: result age P95 | PASS | 147.199072 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260922_012132_266429: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260922_012132_266429: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260922_012132_266429: every sample observed | PASS | 13 | 13 |
| acceptance-2-20260922_012132_266429: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260922_012132_266429: counters never reset | PASS | True | monotonic |
| acceptance-2-20260922_012132_266429: processed FPS | PASS | 26.980805134765344 | >= 25.0 |
| acceptance-2-20260922_012132_266429: low detection FPS | PASS | 4.916206000466817 | >= 4.0 |
| acceptance-2-20260922_012132_266429: high detection FPS | PASS | 1.033236515352348 | >= 0.5 |
| acceptance-2-20260922_012132_266429: result age P95 | PASS | 106.543947 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260922_012132_266429: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260922_012132_266429: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260922_012132_266429: every sample observed | PASS | 13 | 13 |
| acceptance-3-20260922_012132_266429: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260922_012132_266429: counters never reset | PASS | True | monotonic |
| acceptance-3-20260922_012132_266429: processed FPS | PASS | 28.863962009520428 | >= 25.0 |
| acceptance-3-20260922_012132_266429: low detection FPS | PASS | 4.999531525898457 | >= 4.0 |
| acceptance-3-20260922_012132_266429: high detection FPS | PASS | 1.1665573560429734 | >= 0.5 |
| acceptance-3-20260922_012132_266429: result age P95 | PASS | 99.384701 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260922_012132_266429: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260922_012132_266429: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260922_012132_266429: every sample observed | PASS | 13 | 13 |
| acceptance-4-20260922_012132_266429: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260922_012132_266429: counters never reset | PASS | True | monotonic |
| acceptance-4-20260922_012132_266429: processed FPS | PASS | 28.180692700980973 | >= 25.0 |
| acceptance-4-20260922_012132_266429: low detection FPS | PASS | 4.949536210639473 | >= 4.0 |
| acceptance-4-20260922_012132_266429: high detection FPS | PASS | 1.1665573560429734 | >= 0.5 |
| acceptance-4-20260922_012132_266429: result age P95 | PASS | 104.341452 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260922_012132_266429: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260922_012132_266429: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260922_012132_266429: every sample observed | PASS | 13 | 13 |
| acceptance-5-20260922_012132_266429: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260922_012132_266429: counters never reset | PASS | True | monotonic |
| acceptance-5-20260922_012132_266429: processed FPS | PASS | 26.06422435501729 | >= 25.0 |
| acceptance-5-20260922_012132_266429: low detection FPS | PASS | 4.832880475035176 | >= 4.0 |
| acceptance-5-20260922_012132_266429: high detection FPS | PASS | 1.0499016204386762 | >= 0.5 |
| acceptance-5-20260922_012132_266429: result age P95 | PASS | 96.878414 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260922_012132_266429: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260922_012132_266429: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260922_012132_266429: every sample observed | PASS | 13 | 13 |
| acceptance-6-20260922_012132_266429: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260922_012132_266429: counters never reset | PASS | True | monotonic |
| acceptance-6-20260922_012132_266429: processed FPS | PASS | 25.747587358377057 | >= 25.0 |
| acceptance-6-20260922_012132_266429: low detection FPS | PASS | 4.766220054689863 | >= 4.0 |
| acceptance-6-20260922_012132_266429: high detection FPS | PASS | 1.033236515352348 | >= 0.5 |
| acceptance-6-20260922_012132_266429: result age P95 | PASS | 135.711752 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260922_012132_266429: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260922_012132_266429: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 10.796766743648957 | <= 10.0% |
| process CPU average | PASS | 58.95291888601908 | <= 85.0% of host |
| process RSS growth | PASS | -8.549297881490622 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00748169599683, 'observation_count': 3, 'completed': True, 'final_counters': {'acceptance-1-20260922_012132_266429': {'decoded_frame_count': 301, 'processed_frame_count': 254, 'low_res_detection_count': 42, 'high_res_detection_count': 6, 'dropped_frame_count': 46, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 36, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260922_012132_266429': {'decoded_frame_count': 300, 'processed_frame_count': 244, 'low_res_detection_count': 39, 'high_res_detection_count': 7, 'dropped_frame_count': 55, 'decoder_queue_drop_count': 21, 'processor_coalesced_frame_count': 34, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260922_012132_266429': {'decoded_frame_count': 300, 'processed_frame_count': 267, 'low_res_detection_count': 43, 'high_res_detection_count': 8, 'dropped_frame_count': 33, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 32, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260922_012132_266429': {'decoded_frame_count': 300, 'processed_frame_count': 267, 'low_res_detection_count': 43, 'high_res_detection_count': 6, 'dropped_frame_count': 32, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 24, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260922_012132_266429': {'decoded_frame_count': 300, 'processed_frame_count': 236, 'low_res_detection_count': 41, 'high_res_detection_count': 9, 'dropped_frame_count': 63, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 54, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260922_012132_266429': {'decoded_frame_count': 300, 'processed_frame_count': 244, 'low_res_detection_count': 40, 'high_res_detection_count': 7, 'dropped_frame_count': 55, 'decoder_queue_drop_count': 16, 'processor_coalesced_frame_count': 39, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
