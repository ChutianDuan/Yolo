# 多路实时监测验收报告

- 开始时间：2026-09-14T09:02:19.027874+00:00
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
| acceptance-1-20260914_090219_027892 | running | 24.663 | 4.799 | 0.967 | 130.61 | 161 | 0 | 2 |
| acceptance-2-20260914_090219_027892 | running | 22.430 | 4.366 | 0.967 | 161.04 | 229 | 0 | 2 |
| acceptance-3-20260914_090219_027892 | running | 26.563 | 4.899 | 1.100 | 100.21 | 103 | 0 | 2 |
| acceptance-4-20260914_090219_027892 | running | 26.596 | 4.799 | 1.033 | 116.18 | 103 | 0 | 2 |
| acceptance-5-20260914_090219_027892 | running | 22.363 | 4.466 | 0.967 | 167.24 | 229 | 0 | 2 |
| acceptance-6-20260914_090219_027892 | running | 23.197 | 4.699 | 0.900 | 181.75 | 203 | 0 | 2 |

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
| requested duration covered | PASS | 30.004345824941993 | >= 30.0 s |
| sampling continuity | PASS | 1.0059109148569405 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_090219_027892: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_090219_027892: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_090219_027892: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_090219_027892: processed FPS | FAIL | 24.663093950372126 | >= 25.0 |
| acceptance-1-20260914_090219_027892: low detection FPS | PASS | 4.799304768721062 | >= 4.0 |
| acceptance-1-20260914_090219_027892: high detection FPS | PASS | 0.9665266548118806 | >= 0.5 |
| acceptance-1-20260914_090219_027892: result age P95 | PASS | 130.610515 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_090219_027892: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_090219_027892: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_090219_027892: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_090219_027892: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_090219_027892: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_090219_027892: processed FPS | FAIL | 22.430084092703297 | >= 25.0 |
| acceptance-2-20260914_090219_027892: low detection FPS | PASS | 4.366034199322633 | >= 4.0 |
| acceptance-2-20260914_090219_027892: high detection FPS | PASS | 0.9665266548118806 | >= 0.5 |
| acceptance-2-20260914_090219_027892: result age P95 | PASS | 161.038278 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_090219_027892: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_090219_027892: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_090219_027892: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_090219_027892: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_090219_027892: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_090219_027892: processed FPS | PASS | 26.562818754657545 | >= 25.0 |
| acceptance-3-20260914_090219_027892: low detection FPS | PASS | 4.899290284736084 | >= 4.0 |
| acceptance-3-20260914_090219_027892: high detection FPS | PASS | 1.0998406761652435 | >= 0.5 |
| acceptance-3-20260914_090219_027892: result age P95 | PASS | 100.210156 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_090219_027892: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_090219_027892: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_090219_027892: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_090219_027892: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_090219_027892: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_090219_027892: processed FPS | PASS | 26.596147259995888 | >= 25.0 |
| acceptance-4-20260914_090219_027892: low detection FPS | PASS | 4.799304768721062 | >= 4.0 |
| acceptance-4-20260914_090219_027892: high detection FPS | PASS | 1.033183665488562 | >= 0.5 |
| acceptance-4-20260914_090219_027892: result age P95 | PASS | 116.179585 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_090219_027892: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_090219_027892: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_090219_027892: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_090219_027892: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_090219_027892: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_090219_027892: processed FPS | FAIL | 22.363427082026618 | >= 25.0 |
| acceptance-5-20260914_090219_027892: low detection FPS | PASS | 4.466019715337655 | >= 4.0 |
| acceptance-5-20260914_090219_027892: high detection FPS | PASS | 0.9665266548118806 | >= 0.5 |
| acceptance-5-20260914_090219_027892: result age P95 | PASS | 167.24234 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_090219_027892: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_090219_027892: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_090219_027892: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_090219_027892: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_090219_027892: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_090219_027892: processed FPS | FAIL | 23.196639715485134 | >= 25.0 |
| acceptance-6-20260914_090219_027892: low detection FPS | PASS | 4.69931925270604 | >= 4.0 |
| acceptance-6-20260914_090219_027892: high detection FPS | PASS | 0.8998696441351992 | >= 0.5 |
| acceptance-6-20260914_090219_027892: result age P95 | PASS | 181.75233 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_090219_027892: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_090219_027892: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 15.914786967418545 | <= 10.0% |
| process CPU average | PASS | 61.494003348944545 | <= 85.0% of host |
| process RSS growth | PASS | -3.75204329241672 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.002809271914884, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_090219_027892': {'decoded_frame_count': 300, 'processed_frame_count': 207, 'low_res_detection_count': 40, 'high_res_detection_count': 5, 'dropped_frame_count': 92, 'decoder_queue_drop_count': 13, 'processor_coalesced_frame_count': 79, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_090219_027892': {'decoded_frame_count': 300, 'processed_frame_count': 205, 'low_res_detection_count': 34, 'high_res_detection_count': 7, 'dropped_frame_count': 92, 'decoder_queue_drop_count': 41, 'processor_coalesced_frame_count': 51, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_090219_027892': {'decoded_frame_count': 300, 'processed_frame_count': 229, 'low_res_detection_count': 40, 'high_res_detection_count': 5, 'dropped_frame_count': 70, 'decoder_queue_drop_count': 14, 'processor_coalesced_frame_count': 56, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_090219_027892': {'decoded_frame_count': 300, 'processed_frame_count': 226, 'low_res_detection_count': 38, 'high_res_detection_count': 3, 'dropped_frame_count': 73, 'decoder_queue_drop_count': 17, 'processor_coalesced_frame_count': 56, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_090219_027892': {'decoded_frame_count': 300, 'processed_frame_count': 225, 'low_res_detection_count': 39, 'high_res_detection_count': 5, 'dropped_frame_count': 74, 'decoder_queue_drop_count': 17, 'processor_coalesced_frame_count': 57, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_090219_027892': {'decoded_frame_count': 300, 'processed_frame_count': 236, 'low_res_detection_count': 39, 'high_res_detection_count': 6, 'dropped_frame_count': 63, 'decoder_queue_drop_count': 18, 'processor_coalesced_frame_count': 45, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
