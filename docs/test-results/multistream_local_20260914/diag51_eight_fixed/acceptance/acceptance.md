# 多路实时监测验收报告

- 开始时间：2026-09-14T02:05:01.000893+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：8
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.003/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_020501_000909 | running | 25.730 | 1.900 | 0.733 | 139.74 | 129 | 0 | 2 |
| acceptance-2-20260914_020501_000909 | running | 25.297 | 1.900 | 0.800 | 150.38 | 142 | 0 | 2 |
| acceptance-3-20260914_020501_000909 | running | 27.596 | 2.133 | 0.800 | 138.70 | 73 | 0 | 2 |
| acceptance-4-20260914_020501_000909 | running | 27.963 | 2.166 | 0.867 | 118.67 | 61 | 0 | 2 |
| acceptance-5-20260914_020501_000909 | running | 25.663 | 2.066 | 0.767 | 136.73 | 130 | 0 | 2 |
| acceptance-6-20260914_020501_000909 | running | 25.430 | 2.000 | 0.667 | 121.99 | 138 | 0 | 2 |
| acceptance-7-20260914_020501_000909 | running | 27.763 | 2.166 | 0.800 | 110.11 | 67 | 0 | 2 |
| acceptance-8-20260914_020501_000909 | running | 27.596 | 2.133 | 0.867 | 114.95 | 72 | 0 | 2 |

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
| requested duration covered | PASS | 30.003861999954097 | >= 30.0 s |
| sampling continuity | PASS | 1.0131300990469754 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 8/8 | 8/8 |
| acceptance-1-20260914_020501_000909: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_020501_000909: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_020501_000909: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_020501_000909: processed FPS | PASS | 25.730021021999804 | >= 25.0 |
| acceptance-1-20260914_020501_000909: low detection FPS | FAIL | 1.8997554381528352 | >= 4.0 |
| acceptance-1-20260914_020501_000909: high detection FPS | PASS | 0.7332389410414452 | >= 0.5 |
| acceptance-1-20260914_020501_000909: result age P95 | PASS | 139.744455 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_020501_000909: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_020501_000909: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_020501_000909: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_020501_000909: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_020501_000909: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_020501_000909: processed FPS | PASS | 25.296743465929858 | >= 25.0 |
| acceptance-2-20260914_020501_000909: low detection FPS | FAIL | 1.8997554381528352 | >= 4.0 |
| acceptance-2-20260914_020501_000909: high detection FPS | PASS | 0.7998970265906675 | >= 0.5 |
| acceptance-2-20260914_020501_000909: result age P95 | PASS | 150.381472 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_020501_000909: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_020501_000909: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_020501_000909: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_020501_000909: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_020501_000909: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_020501_000909: processed FPS | PASS | 27.596447417378027 | >= 25.0 |
| acceptance-3-20260914_020501_000909: low detection FPS | FAIL | 2.133058737575113 | >= 4.0 |
| acceptance-3-20260914_020501_000909: high detection FPS | PASS | 0.7998970265906675 | >= 0.5 |
| acceptance-3-20260914_020501_000909: result age P95 | PASS | 138.695756 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_020501_000909: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_020501_000909: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_020501_000909: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_020501_000909: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_020501_000909: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_020501_000909: processed FPS | PASS | 27.96306688789875 | >= 25.0 |
| acceptance-4-20260914_020501_000909: low detection FPS | FAIL | 2.1663877803497242 | >= 4.0 |
| acceptance-4-20260914_020501_000909: high detection FPS | PASS | 0.8665551121398898 | >= 0.5 |
| acceptance-4-20260914_020501_000909: result age P95 | PASS | 118.673615 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_020501_000909: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_020501_000909: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_020501_000909: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_020501_000909: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_020501_000909: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_020501_000909: processed FPS | PASS | 25.663362936450582 | >= 25.0 |
| acceptance-5-20260914_020501_000909: low detection FPS | FAIL | 2.066400652025891 | >= 4.0 |
| acceptance-5-20260914_020501_000909: high detection FPS | PASS | 0.7665679838160563 | >= 0.5 |
| acceptance-5-20260914_020501_000909: result age P95 | PASS | 136.728693 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_020501_000909: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_020501_000909: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_020501_000909: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_020501_000909: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_020501_000909: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_020501_000909: processed FPS | PASS | 25.430059637028304 | >= 25.0 |
| acceptance-6-20260914_020501_000909: low detection FPS | FAIL | 1.9997425664766688 | >= 4.0 |
| acceptance-6-20260914_020501_000909: high detection FPS | PASS | 0.6665808554922229 | >= 0.5 |
| acceptance-6-20260914_020501_000909: result age P95 | PASS | 121.98873 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_020501_000909: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_020501_000909: queue bound | PASS | 2 | <= 2 |
| acceptance-7-20260914_020501_000909: every sample observed | PASS | 31 | 31 |
| acceptance-7-20260914_020501_000909: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-7-20260914_020501_000909: counters never reset | PASS | True | monotonic |
| acceptance-7-20260914_020501_000909: processed FPS | PASS | 27.763092631251084 | >= 25.0 |
| acceptance-7-20260914_020501_000909: low detection FPS | FAIL | 2.1663877803497242 | >= 4.0 |
| acceptance-7-20260914_020501_000909: high detection FPS | PASS | 0.7998970265906675 | >= 0.5 |
| acceptance-7-20260914_020501_000909: result age P95 | PASS | 110.111224 | <= 300.0 ms (local decode to query) |
| acceptance-7-20260914_020501_000909: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-7-20260914_020501_000909: queue bound | PASS | 2 | <= 2 |
| acceptance-8-20260914_020501_000909: every sample observed | PASS | 31 | 31 |
| acceptance-8-20260914_020501_000909: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-8-20260914_020501_000909: counters never reset | PASS | True | monotonic |
| acceptance-8-20260914_020501_000909: processed FPS | PASS | 27.596447417378027 | >= 25.0 |
| acceptance-8-20260914_020501_000909: low detection FPS | FAIL | 2.133058737575113 | >= 4.0 |
| acceptance-8-20260914_020501_000909: high detection FPS | PASS | 0.8665551121398898 | >= 0.5 |
| acceptance-8-20260914_020501_000909: result age P95 | PASS | 114.954923 | <= 300.0 ms (local decode to query) |
| acceptance-8-20260914_020501_000909: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-8-20260914_020501_000909: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 9.535160905840295 | <= 10.0% |
| process CPU average | PASS | 52.28644826086158 | <= 85.0% of host |
| process RSS growth | PASS | 1.2497351258446543 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.002845986979082, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_020501_000909': {'decoded_frame_count': 300, 'processed_frame_count': 248, 'low_res_detection_count': 17, 'high_res_detection_count': 6, 'dropped_frame_count': 51, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 42, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_020501_000909': {'decoded_frame_count': 300, 'processed_frame_count': 218, 'low_res_detection_count': 15, 'high_res_detection_count': 7, 'dropped_frame_count': 81, 'decoder_queue_drop_count': 30, 'processor_coalesced_frame_count': 51, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_020501_000909': {'decoded_frame_count': 300, 'processed_frame_count': 253, 'low_res_detection_count': 18, 'high_res_detection_count': 7, 'dropped_frame_count': 46, 'decoder_queue_drop_count': 7, 'processor_coalesced_frame_count': 39, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_020501_000909': {'decoded_frame_count': 300, 'processed_frame_count': 241, 'low_res_detection_count': 16, 'high_res_detection_count': 7, 'dropped_frame_count': 58, 'decoder_queue_drop_count': 21, 'processor_coalesced_frame_count': 36, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_020501_000909': {'decoded_frame_count': 300, 'processed_frame_count': 230, 'low_res_detection_count': 18, 'high_res_detection_count': 6, 'dropped_frame_count': 69, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 60, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_020501_000909': {'decoded_frame_count': 300, 'processed_frame_count': 236, 'low_res_detection_count': 18, 'high_res_detection_count': 6, 'dropped_frame_count': 62, 'decoder_queue_drop_count': 23, 'processor_coalesced_frame_count': 39, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-7-20260914_020501_000909': {'decoded_frame_count': 300, 'processed_frame_count': 267, 'low_res_detection_count': 18, 'high_res_detection_count': 5, 'dropped_frame_count': 31, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 26, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-8-20260914_020501_000909': {'decoded_frame_count': 300, 'processed_frame_count': 287, 'low_res_detection_count': 18, 'high_res_detection_count': 6, 'dropped_frame_count': 12, 'decoder_queue_drop_count': 2, 'processor_coalesced_frame_count': 10, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
