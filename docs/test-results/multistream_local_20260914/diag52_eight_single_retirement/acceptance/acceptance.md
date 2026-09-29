# 多路实时监测验收报告

- 开始时间：2026-09-14T02:26:29.903534+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：8
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.004/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_022629_903558 | running | 28.697 | 0.000 | 3.133 | 137.32 | 39 | 0 | 2 |
| acceptance-2-20260914_022629_903558 | running | 28.963 | 0.000 | 3.133 | 99.07 | 31 | 0 | 2 |
| acceptance-3-20260914_022629_903558 | running | 29.297 | 0.000 | 3.166 | 113.94 | 21 | 0 | 2 |
| acceptance-4-20260914_022629_903558 | running | 28.930 | 0.000 | 3.133 | 109.21 | 31 | 0 | 2 |
| acceptance-5-20260914_022629_903558 | running | 28.997 | 0.000 | 3.133 | 81.18 | 31 | 0 | 2 |
| acceptance-6-20260914_022629_903558 | running | 28.697 | 0.000 | 3.133 | 84.73 | 38 | 0 | 2 |
| acceptance-7-20260914_022629_903558 | running | 29.163 | 0.000 | 3.166 | 80.00 | 24 | 0 | 2 |
| acceptance-8-20260914_022629_903558 | running | 28.897 | 0.000 | 3.100 | 93.15 | 34 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2133 | 8baa9f5b8a8b6b0f8be2bc49a66ffaf0a0b8f1dcc9e5a1af46c16d2f976f0921 | 8baa9f5b8a8b6b0f8be2bc49a66ffaf0a0b8f1dcc9e5a1af46c16d2f976f0921 | True |
| model | best_640x384.onnx | 38114065 | 439a56ee159a52ffafdce6befa4e8486c8e311f8d6095ffb9cbbad0443fdfcbf | 439a56ee159a52ffafdce6befa4e8486c8e311f8d6095ffb9cbbad0443fdfcbf | True |

## 验收门槛

| gate | result | actual | target |
| --- | --- | --- | --- |
| collection and cleanup completed | PASS | [] | no errors |
| reproducibility evidence recorded | PASS | {'config_files': 1, 'model_files': 1, 'cpu_identity_recorded': True} | config and >=1 model fingerprint plus collector CPU identity |
| artifact fingerprints stable | PASS | [] | start/end fingerprints match after cleanup |
| lifecycle churn completed | PASS | disabled | disabled |
| valid sampling timeline | PASS | 31 | >= 2 ordered samples |
| requested duration covered | PASS | 30.0034960960038 | >= 30.0 s |
| sampling continuity | PASS | 1.006312715006061 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 8/8 | 8/8 |
| acceptance-1-20260914_022629_903558: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_022629_903558: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_022629_903558: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_022629_903558: processed FPS | PASS | 28.696655791212198 | >= 25.0 |
| acceptance-1-20260914_022629_903558: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-1-20260914_022629_903558: high detection FPS | FAIL | 3.1329682280765927 | >= 4.0 |
| acceptance-1-20260914_022629_903558: result age P95 | PASS | 137.319359 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_022629_903558: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_022629_903558: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_022629_903558: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_022629_903558: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_022629_903558: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_022629_903558: processed FPS | PASS | 28.963291385091054 | >= 25.0 |
| acceptance-2-20260914_022629_903558: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-2-20260914_022629_903558: high detection FPS | FAIL | 3.1329682280765927 | >= 4.0 |
| acceptance-2-20260914_022629_903558: result age P95 | PASS | 99.067227 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_022629_903558: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_022629_903558: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_022629_903558: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_022629_903558: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_022629_903558: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_022629_903558: processed FPS | PASS | 29.29658587743963 | >= 25.0 |
| acceptance-3-20260914_022629_903558: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-3-20260914_022629_903558: high detection FPS | FAIL | 3.16629767731145 | >= 4.0 |
| acceptance-3-20260914_022629_903558: result age P95 | PASS | 113.941717 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_022629_903558: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_022629_903558: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_022629_903558: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_022629_903558: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_022629_903558: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_022629_903558: processed FPS | PASS | 28.9299619358562 | >= 25.0 |
| acceptance-4-20260914_022629_903558: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-4-20260914_022629_903558: high detection FPS | FAIL | 3.1329682280765927 | >= 4.0 |
| acceptance-4-20260914_022629_903558: result age P95 | PASS | 109.214662 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_022629_903558: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_022629_903558: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_022629_903558: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_022629_903558: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_022629_903558: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_022629_903558: processed FPS | PASS | 28.996620834325913 | >= 25.0 |
| acceptance-5-20260914_022629_903558: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-5-20260914_022629_903558: high detection FPS | FAIL | 3.1329682280765927 | >= 4.0 |
| acceptance-5-20260914_022629_903558: result age P95 | PASS | 81.179215 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_022629_903558: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_022629_903558: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_022629_903558: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_022629_903558: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_022629_903558: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_022629_903558: processed FPS | PASS | 28.696655791212198 | >= 25.0 |
| acceptance-6-20260914_022629_903558: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-6-20260914_022629_903558: high detection FPS | FAIL | 3.1329682280765927 | >= 4.0 |
| acceptance-6-20260914_022629_903558: result age P95 | PASS | 84.726211 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_022629_903558: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_022629_903558: queue bound | PASS | 2 | <= 2 |
| acceptance-7-20260914_022629_903558: every sample observed | PASS | 31 | 31 |
| acceptance-7-20260914_022629_903558: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-7-20260914_022629_903558: counters never reset | PASS | True | monotonic |
| acceptance-7-20260914_022629_903558: processed FPS | PASS | 29.1632680805002 | >= 25.0 |
| acceptance-7-20260914_022629_903558: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-7-20260914_022629_903558: high detection FPS | FAIL | 3.16629767731145 | >= 4.0 |
| acceptance-7-20260914_022629_903558: result age P95 | PASS | 80.003214 | <= 300.0 ms (local decode to query) |
| acceptance-7-20260914_022629_903558: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-7-20260914_022629_903558: queue bound | PASS | 2 | <= 2 |
| acceptance-8-20260914_022629_903558: every sample observed | PASS | 31 | 31 |
| acceptance-8-20260914_022629_903558: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-8-20260914_022629_903558: counters never reset | PASS | True | monotonic |
| acceptance-8-20260914_022629_903558: processed FPS | PASS | 28.896632486621343 | >= 25.0 |
| acceptance-8-20260914_022629_903558: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-8-20260914_022629_903558: high detection FPS | FAIL | 3.0996387788417357 | >= 4.0 |
| acceptance-8-20260914_022629_903558: result age P95 | PASS | 93.147158 | <= 300.0 ms (local decode to query) |
| acceptance-8-20260914_022629_903558: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-8-20260914_022629_903558: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 2.047781569965865 | <= 10.0% |
| process CPU average | PASS | 28.530607608899576 | <= 85.0% of host |
| process RSS growth | PASS | 2.61242822218432 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003517083008774, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_022629_903558': {'decoded_frame_count': 300, 'processed_frame_count': 276, 'low_res_detection_count': 0, 'high_res_detection_count': 28, 'dropped_frame_count': 24, 'decoder_queue_drop_count': 7, 'processor_coalesced_frame_count': 17, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_022629_903558': {'decoded_frame_count': 300, 'processed_frame_count': 281, 'low_res_detection_count': 0, 'high_res_detection_count': 28, 'dropped_frame_count': 19, 'decoder_queue_drop_count': 4, 'processor_coalesced_frame_count': 15, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_022629_903558': {'decoded_frame_count': 300, 'processed_frame_count': 288, 'low_res_detection_count': 0, 'high_res_detection_count': 27, 'dropped_frame_count': 12, 'decoder_queue_drop_count': 0, 'processor_coalesced_frame_count': 12, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_022629_903558': {'decoded_frame_count': 300, 'processed_frame_count': 276, 'low_res_detection_count': 0, 'high_res_detection_count': 27, 'dropped_frame_count': 24, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 16, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_022629_903558': {'decoded_frame_count': 300, 'processed_frame_count': 279, 'low_res_detection_count': 0, 'high_res_detection_count': 26, 'dropped_frame_count': 20, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 15, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_022629_903558': {'decoded_frame_count': 300, 'processed_frame_count': 279, 'low_res_detection_count': 0, 'high_res_detection_count': 27, 'dropped_frame_count': 20, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 12, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-7-20260914_022629_903558': {'decoded_frame_count': 300, 'processed_frame_count': 290, 'low_res_detection_count': 0, 'high_res_detection_count': 27, 'dropped_frame_count': 10, 'decoder_queue_drop_count': 0, 'processor_coalesced_frame_count': 10, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-8-20260914_022629_903558': {'decoded_frame_count': 300, 'processed_frame_count': 265, 'low_res_detection_count': 0, 'high_res_detection_count': 26, 'dropped_frame_count': 34, 'decoder_queue_drop_count': 12, 'processor_coalesced_frame_count': 22, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
