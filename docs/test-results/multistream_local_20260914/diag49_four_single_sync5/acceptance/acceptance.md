# 多路实时监测验收报告

- 开始时间：2026-09-14T01:30:24.984786+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.003/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_013024_984801 | running | 23.464 | 0.000 | 5.999 | 132.56 | 196 | 0 | 2 |
| acceptance-2-20260914_013024_984801 | running | 22.397 | 0.000 | 6.033 | 164.07 | 229 | 0 | 2 |
| acceptance-3-20260914_013024_984801 | running | 18.831 | 0.000 | 5.999 | 140.92 | 335 | 0 | 2 |
| acceptance-4-20260914_013024_984801 | running | 17.364 | 0.000 | 6.066 | 145.83 | 379 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2134 | a62d586c9ef05e613a231ea61cea3da96828ea70bf1cd6304ccbd4d8bdc6047e | a62d586c9ef05e613a231ea61cea3da96828ea70bf1cd6304ccbd4d8bdc6047e | True |
| model | best_640x384.onnx | 38114065 | 439a56ee159a52ffafdce6befa4e8486c8e311f8d6095ffb9cbbad0443fdfcbf | 439a56ee159a52ffafdce6befa4e8486c8e311f8d6095ffb9cbbad0443fdfcbf | True |

## 验收门槛

| gate | result | actual | target |
| --- | --- | --- | --- |
| collection and cleanup completed | PASS | [] | no errors |
| reproducibility evidence recorded | PASS | {'config_files': 1, 'model_files': 1, 'cpu_identity_recorded': True} | config and >=1 model fingerprint plus collector CPU identity |
| artifact fingerprints stable | PASS | [] | start/end fingerprints match after cleanup |
| lifecycle churn completed | PASS | disabled | disabled |
| valid sampling timeline | PASS | 31 | >= 2 ordered samples |
| requested duration covered | PASS | 30.00399322202429 | >= 30.0 s |
| sampling continuity | PASS | 1.0113908229395747 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260914_013024_984801: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_013024_984801: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_013024_984801: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_013024_984801: processed FPS | FAIL | 23.46354349537821 | >= 25.0 |
| acceptance-1-20260914_013024_984801: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-1-20260914_013024_984801: high detection FPS | PASS | 5.999201461886474 | >= 4.0 |
| acceptance-1-20260914_013024_984801: result age P95 | PASS | 132.563107 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_013024_984801: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_013024_984801: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_013024_984801: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_013024_984801: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_013024_984801: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_013024_984801: processed FPS | FAIL | 22.39701879104284 | >= 25.0 |
| acceptance-2-20260914_013024_984801: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-2-20260914_013024_984801: high detection FPS | PASS | 6.032530358896955 | >= 4.0 |
| acceptance-2-20260914_013024_984801: result age P95 | PASS | 164.07475 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_013024_984801: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_013024_984801: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_013024_984801: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_013024_984801: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_013024_984801: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_013024_984801: processed FPS | FAIL | 18.830826810921433 | >= 25.0 |
| acceptance-3-20260914_013024_984801: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-3-20260914_013024_984801: high detection FPS | PASS | 5.999201461886474 | >= 4.0 |
| acceptance-3-20260914_013024_984801: result age P95 | PASS | 140.915803 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_013024_984801: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_013024_984801: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_013024_984801: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_013024_984801: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_013024_984801: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_013024_984801: processed FPS | FAIL | 17.364355342460296 | >= 25.0 |
| acceptance-4-20260914_013024_984801: low detection FPS | PASS | 0.0 | >= 0.0 |
| acceptance-4-20260914_013024_984801: high detection FPS | PASS | 6.065859255907435 | >= 4.0 |
| acceptance-4-20260914_013024_984801: result age P95 | PASS | 145.828351 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_013024_984801: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_013024_984801: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 25.994318181818176 | <= 10.0% |
| process CPU average | PASS | 16.670177984010646 | <= 85.0% of host |
| process RSS growth | PASS | -0.4700139470013947 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00295020500198, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_013024_984801': {'decoded_frame_count': 300, 'processed_frame_count': 251, 'low_res_detection_count': 0, 'high_res_detection_count': 60, 'dropped_frame_count': 49, 'decoder_queue_drop_count': 17, 'processor_coalesced_frame_count': 32, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_013024_984801': {'decoded_frame_count': 300, 'processed_frame_count': 186, 'low_res_detection_count': 0, 'high_res_detection_count': 62, 'dropped_frame_count': 113, 'decoder_queue_drop_count': 60, 'processor_coalesced_frame_count': 53, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_013024_984801': {'decoded_frame_count': 300, 'processed_frame_count': 141, 'low_res_detection_count': 0, 'high_res_detection_count': 60, 'dropped_frame_count': 159, 'decoder_queue_drop_count': 101, 'processor_coalesced_frame_count': 58, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_013024_984801': {'decoded_frame_count': 300, 'processed_frame_count': 175, 'low_res_detection_count': 0, 'high_res_detection_count': 61, 'dropped_frame_count': 125, 'decoder_queue_drop_count': 68, 'processor_coalesced_frame_count': 57, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
