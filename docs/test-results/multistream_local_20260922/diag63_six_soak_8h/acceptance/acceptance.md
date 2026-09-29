# 多路实时监测验收报告

- 开始时间：2026-09-22T01:25:57.144098+00:00
- 实际采样时长：28800.0 s
- 要求采样时长：28800.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 60.006/60.000 s，观察 13 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260922_012557_144126 | running | 25.347 | 3.851 | 0.599 | 186.94 | 134012 | 0 | 2 |
| acceptance-2-20260922_012557_144126 | running | 25.459 | 3.854 | 0.598 | 184.94 | 130788 | 0 | 2 |
| acceptance-3-20260922_012557_144126 | running | 28.283 | 4.144 | 0.666 | 102.72 | 49446 | 0 | 2 |
| acceptance-4-20260922_012557_144126 | running | 27.256 | 4.049 | 0.660 | 123.99 | 79020 | 0 | 2 |
| acceptance-5-20260922_012557_144126 | running | 25.495 | 3.872 | 0.602 | 185.19 | 129756 | 0 | 2 |
| acceptance-6-20260922_012557_144126 | running | 25.466 | 3.850 | 0.601 | 185.40 | 130568 | 0 | 2 |

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
| valid sampling timeline | PASS | 5753 | >= 2 ordered samples |
| requested duration covered | PASS | 28800.005034207003 | >= 28800.0 s |
| sampling continuity | PASS | 5.119291524999426 | maximum gap <= 10.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260922_012557_144126: every sample observed | PASS | 5753 | 5753 |
| acceptance-1-20260922_012557_144126: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260922_012557_144126: counters never reset | PASS | True | monotonic |
| acceptance-1-20260922_012557_144126: processed FPS | PASS | 25.34676640274761 | >= 25.0 |
| acceptance-1-20260922_012557_144126: low detection FPS | FAIL | 3.8506243269152796 | >= 4.0 |
| acceptance-1-20260922_012557_144126: high detection FPS | PASS | 0.5991665619330381 | >= 0.5 |
| acceptance-1-20260922_012557_144126: result age P95 | PASS | 186.939658 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260922_012557_144126: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260922_012557_144126: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260922_012557_144126: every sample observed | PASS | 5753 | 5753 |
| acceptance-2-20260922_012557_144126: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260922_012557_144126: counters never reset | PASS | True | monotonic |
| acceptance-2-20260922_012557_144126: processed FPS | PASS | 25.4587802720566 | >= 25.0 |
| acceptance-2-20260922_012557_144126: low detection FPS | FAIL | 3.8539923818821027 | >= 4.0 |
| acceptance-2-20260922_012557_144126: high detection FPS | PASS | 0.5981596176646063 | >= 0.5 |
| acceptance-2-20260922_012557_144126: result age P95 | PASS | 184.936697 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260922_012557_144126: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260922_012557_144126: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260922_012557_144126: every sample observed | PASS | 5753 | 5753 |
| acceptance-3-20260922_012557_144126: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260922_012557_144126: counters never reset | PASS | True | monotonic |
| acceptance-3-20260922_012557_144126: processed FPS | PASS | 28.28312005614302 | >= 25.0 |
| acceptance-3-20260922_012557_144126: low detection FPS | PASS | 4.143679831245069 | >= 4.0 |
| acceptance-3-20260922_012557_144126: high detection FPS | PASS | 0.6662846057564366 | >= 0.5 |
| acceptance-3-20260922_012557_144126: result age P95 | PASS | 102.716935 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260922_012557_144126: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260922_012557_144126: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260922_012557_144126: every sample observed | PASS | 5753 | 5753 |
| acceptance-4-20260922_012557_144126: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260922_012557_144126: counters never reset | PASS | True | monotonic |
| acceptance-4-20260922_012557_144126: processed FPS | PASS | 27.25624523563956 | >= 25.0 |
| acceptance-4-20260922_012557_144126: low detection FPS | PASS | 4.048957625580179 | >= 4.0 |
| acceptance-4-20260922_012557_144126: high detection FPS | PASS | 0.6597568291197067 | >= 0.5 |
| acceptance-4-20260922_012557_144126: result age P95 | PASS | 123.989653 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260922_012557_144126: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260922_012557_144126: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260922_012557_144126: every sample observed | PASS | 5753 | 5753 |
| acceptance-5-20260922_012557_144126: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260922_012557_144126: counters never reset | PASS | True | monotonic |
| acceptance-5-20260922_012557_144126: processed FPS | PASS | 25.494544154694005 | >= 25.0 |
| acceptance-5-20260922_012557_144126: low detection FPS | FAIL | 3.8718743232008044 | >= 4.0 |
| acceptance-5-20260922_012557_144126: high detection FPS | PASS | 0.6017360059283468 | >= 0.5 |
| acceptance-5-20260922_012557_144126: result age P95 | PASS | 185.192906 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260922_012557_144126: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260922_012557_144126: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260922_012557_144126: every sample observed | PASS | 5753 | 5753 |
| acceptance-6-20260922_012557_144126: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260922_012557_144126: counters never reset | PASS | True | monotonic |
| acceptance-6-20260922_012557_144126: processed FPS | PASS | 25.466384437394066 | >= 25.0 |
| acceptance-6-20260922_012557_144126: low detection FPS | FAIL | 3.8498257159437648 | >= 4.0 |
| acceptance-6-20260922_012557_144126: high detection FPS | PASS | 0.6009373949568318 | >= 0.5 |
| acceptance-6-20260922_012557_144126: result age P95 | PASS | 185.403397 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260922_012557_144126: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260922_012557_144126: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 10.382000456691634 | <= 10.0% |
| process CPU average | PASS | 54.54486344497229 | <= 85.0% of host |
| process RSS growth | FAIL | 14.631676609668986 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 60.0, 'elapsed_seconds': 60.00581859200611, 'observation_count': 13, 'completed': True, 'final_counters': {'acceptance-1-20260922_012557_144126': {'decoded_frame_count': 1801, 'processed_frame_count': 1607, 'low_res_detection_count': 286, 'high_res_detection_count': 63, 'dropped_frame_count': 193, 'decoder_queue_drop_count': 81, 'processor_coalesced_frame_count': 110, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260922_012557_144126': {'decoded_frame_count': 1800, 'processed_frame_count': 1649, 'low_res_detection_count': 295, 'high_res_detection_count': 67, 'dropped_frame_count': 150, 'decoder_queue_drop_count': 52, 'processor_coalesced_frame_count': 98, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260922_012557_144126': {'decoded_frame_count': 1800, 'processed_frame_count': 1743, 'low_res_detection_count': 298, 'high_res_detection_count': 72, 'dropped_frame_count': 57, 'decoder_queue_drop_count': 14, 'processor_coalesced_frame_count': 43, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260922_012557_144126': {'decoded_frame_count': 1800, 'processed_frame_count': 1743, 'low_res_detection_count': 299, 'high_res_detection_count': 74, 'dropped_frame_count': 56, 'decoder_queue_drop_count': 17, 'processor_coalesced_frame_count': 39, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260922_012557_144126': {'decoded_frame_count': 1800, 'processed_frame_count': 1617, 'low_res_detection_count': 290, 'high_res_detection_count': 64, 'dropped_frame_count': 183, 'decoder_queue_drop_count': 64, 'processor_coalesced_frame_count': 117, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260922_012557_144126': {'decoded_frame_count': 1800, 'processed_frame_count': 1673, 'low_res_detection_count': 298, 'high_res_detection_count': 68, 'dropped_frame_count': 126, 'decoder_queue_drop_count': 41, 'processor_coalesced_frame_count': 85, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 60.0 s healthy same-stream warmup |
