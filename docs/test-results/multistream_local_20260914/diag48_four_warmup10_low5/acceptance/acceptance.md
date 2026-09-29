# 多路实时监测验收报告

- 开始时间：2026-09-14T00:36:37.488060+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：PASS
- 同一批流采样前预热：预热 10.004/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_003637_488076 | running | 27.531 | 4.816 | 1.267 | 156.53 | 148 | 0 | 2 |
| acceptance-2-20260914_003637_488076 | running | 26.981 | 4.766 | 1.267 | 130.65 | 182 | 0 | 2 |
| acceptance-3-20260914_003637_488076 | running | 29.498 | 5.000 | 1.433 | 106.11 | 31 | 0 | 2 |
| acceptance-4-20260914_003637_488076 | running | 28.881 | 4.933 | 1.367 | 83.19 | 67 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2183 | eb98c9c251cbb8e6a041dfd70f0dc7d064061dac9eec2ccac43cb558b0e3fa11 | eb98c9c251cbb8e6a041dfd70f0dc7d064061dac9eec2ccac43cb558b0e3fa11 | True |
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
| requested duration covered | PASS | 60.004727059043944 | >= 60.0 s |
| sampling continuity | PASS | 1.0052175080636516 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260914_003637_488076: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260914_003637_488076: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_003637_488076: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_003637_488076: processed FPS | PASS | 27.53116430934602 | >= 25.0 |
| acceptance-1-20260914_003637_488076: low detection FPS | PASS | 4.816287218765738 | >= 4.0 |
| acceptance-1-20260914_003637_488076: high detection FPS | PASS | 1.266566881059502 | >= 0.5 |
| acceptance-1-20260914_003637_488076: result age P95 | PASS | 156.52628 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_003637_488076: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_003637_488076: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_003637_488076: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260914_003637_488076: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_003637_488076: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_003637_488076: processed FPS | PASS | 26.981207637307026 | >= 25.0 |
| acceptance-2-20260914_003637_488076: low detection FPS | PASS | 4.766291157671285 | >= 4.0 |
| acceptance-2-20260914_003637_488076: high detection FPS | PASS | 1.266566881059502 | >= 0.5 |
| acceptance-2-20260914_003637_488076: result age P95 | PASS | 130.646354 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_003637_488076: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_003637_488076: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_003637_488076: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260914_003637_488076: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_003637_488076: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_003637_488076: processed FPS | PASS | 29.49767604572788 | >= 25.0 |
| acceptance-3-20260914_003637_488076: low detection FPS | PASS | 4.999606109445403 | >= 4.0 |
| acceptance-3-20260914_003637_488076: high detection FPS | PASS | 1.4332204180410155 | >= 0.5 |
| acceptance-3-20260914_003637_488076: result age P95 | PASS | 106.114135 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_003637_488076: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_003637_488076: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_003637_488076: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260914_003637_488076: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_003637_488076: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_003637_488076: processed FPS | PASS | 28.881057958896278 | >= 25.0 |
| acceptance-4-20260914_003637_488076: low detection FPS | PASS | 4.9329446946527975 | >= 4.0 |
| acceptance-4-20260914_003637_488076: high detection FPS | PASS | 1.3665590032484103 | >= 0.5 |
| acceptance-4-20260914_003637_488076: result age P95 | PASS | 83.187552 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_003637_488076: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_003637_488076: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 8.531073446327682 | <= 10.0% |
| process CPU average | PASS | 38.09781450143857 | <= 85.0% of host |
| process RSS growth | PASS | 1.6191227064220184 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003792647039518, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_003637_488076': {'decoded_frame_count': 300, 'processed_frame_count': 289, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 11, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 6, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_003637_488076': {'decoded_frame_count': 300, 'processed_frame_count': 285, 'low_res_detection_count': 48, 'high_res_detection_count': 13, 'dropped_frame_count': 14, 'decoder_queue_drop_count': 2, 'processor_coalesced_frame_count': 12, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_003637_488076': {'decoded_frame_count': 300, 'processed_frame_count': 295, 'low_res_detection_count': 49, 'high_res_detection_count': 14, 'dropped_frame_count': 4, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 3, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_003637_488076': {'decoded_frame_count': 300, 'processed_frame_count': 291, 'low_res_detection_count': 49, 'high_res_detection_count': 11, 'dropped_frame_count': 9, 'decoder_queue_drop_count': 2, 'processor_coalesced_frame_count': 7, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
