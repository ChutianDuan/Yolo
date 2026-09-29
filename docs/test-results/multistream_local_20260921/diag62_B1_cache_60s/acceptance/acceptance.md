# 多路实时监测验收报告

- 开始时间：2026-09-21T23:41:23.107382+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：PASS
- 同一批流采样前预热：预热 10.003/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260921_234123_107398 | running | 27.048 | 4.850 | 1.017 | 140.51 | 177 | 0 | 2 |
| acceptance-2-20260921_234123_107398 | running | 26.815 | 4.866 | 1.083 | 183.02 | 191 | 0 | 2 |
| acceptance-3-20260921_234123_107398 | running | 29.165 | 5.000 | 1.183 | 105.82 | 53 | 0 | 2 |
| acceptance-4-20260921_234123_107398 | running | 28.415 | 5.000 | 1.217 | 104.60 | 95 | 0 | 2 |
| acceptance-5-20260921_234123_107398 | running | 26.398 | 4.716 | 1.033 | 182.92 | 217 | 0 | 2 |
| acceptance-6-20260921_234123_107398 | running | 26.465 | 4.716 | 0.967 | 161.21 | 211 | 0 | 2 |

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
| requested duration covered | PASS | 60.00349191999703 | >= 60.0 s |
| sampling continuity | PASS | 1.0058776679943549 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_234123_107398: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_234123_107398: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_234123_107398: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_234123_107398: processed FPS | PASS | 27.048425817683317 | >= 25.0 |
| acceptance-1-20260921_234123_107398: low detection FPS | PASS | 4.849717752893312 | >= 4.0 |
| acceptance-1-20260921_234123_107398: high detection FPS | PASS | 1.0166075014656084 | >= 0.5 |
| acceptance-1-20260921_234123_107398: result age P95 | PASS | 140.507772 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_234123_107398: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_234123_107398: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_234123_107398: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_234123_107398: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_234123_107398: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_234123_107398: processed FPS | PASS | 26.815106063248585 | >= 25.0 |
| acceptance-2-20260921_234123_107398: low detection FPS | PASS | 4.86638344963865 | >= 4.0 |
| acceptance-2-20260921_234123_107398: high detection FPS | PASS | 1.0832702884469596 | >= 0.5 |
| acceptance-2-20260921_234123_107398: result age P95 | PASS | 183.020236 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_234123_107398: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_234123_107398: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_234123_107398: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_234123_107398: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_234123_107398: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_234123_107398: processed FPS | PASS | 29.164969304341223 | >= 25.0 |
| acceptance-3-20260921_234123_107398: low detection FPS | PASS | 4.999709023601352 | >= 4.0 |
| acceptance-3-20260921_234123_107398: high detection FPS | PASS | 1.1832644689189868 | >= 0.5 |
| acceptance-3-20260921_234123_107398: result age P95 | PASS | 105.820953 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_234123_107398: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_234123_107398: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_234123_107398: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_234123_107398: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_234123_107398: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_234123_107398: processed FPS | PASS | 28.41501295080102 | >= 25.0 |
| acceptance-4-20260921_234123_107398: low detection FPS | PASS | 4.999709023601352 | >= 4.0 |
| acceptance-4-20260921_234123_107398: high detection FPS | PASS | 1.2165958624096624 | >= 0.5 |
| acceptance-4-20260921_234123_107398: result age P95 | PASS | 104.597325 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_234123_107398: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_234123_107398: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_234123_107398: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_234123_107398: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_234123_107398: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_234123_107398: processed FPS | PASS | 26.39846364461514 | >= 25.0 |
| acceptance-5-20260921_234123_107398: low detection FPS | PASS | 4.716392178930609 | >= 4.0 |
| acceptance-5-20260921_234123_107398: high detection FPS | PASS | 1.0332731982109462 | >= 0.5 |
| acceptance-5-20260921_234123_107398: result age P95 | PASS | 182.915045 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_234123_107398: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_234123_107398: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_234123_107398: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_234123_107398: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_234123_107398: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_234123_107398: processed FPS | PASS | 26.46512643159649 | >= 25.0 |
| acceptance-6-20260921_234123_107398: low detection FPS | PASS | 4.716392178930609 | >= 4.0 |
| acceptance-6-20260921_234123_107398: high detection FPS | PASS | 0.9666104112295948 | >= 0.5 |
| acceptance-6-20260921_234123_107398: result age P95 | PASS | 161.207927 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_234123_107398: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_234123_107398: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 9.485714285714288 | <= 10.0% |
| process CPU average | PASS | 58.78107296050041 | <= 85.0% of host |
| process RSS growth | PASS | -5.485668360621193 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.002527261007344, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_234123_107398': {'decoded_frame_count': 300, 'processed_frame_count': 276, 'low_res_detection_count': 50, 'high_res_detection_count': 11, 'dropped_frame_count': 24, 'decoder_queue_drop_count': 4, 'processor_coalesced_frame_count': 20, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_234123_107398': {'decoded_frame_count': 300, 'processed_frame_count': 269, 'low_res_detection_count': 49, 'high_res_detection_count': 11, 'dropped_frame_count': 30, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 20, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_234123_107398': {'decoded_frame_count': 300, 'processed_frame_count': 279, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 18, 'decoder_queue_drop_count': 3, 'processor_coalesced_frame_count': 15, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_234123_107398': {'decoded_frame_count': 300, 'processed_frame_count': 291, 'low_res_detection_count': 49, 'high_res_detection_count': 11, 'dropped_frame_count': 8, 'decoder_queue_drop_count': 2, 'processor_coalesced_frame_count': 6, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_234123_107398': {'decoded_frame_count': 300, 'processed_frame_count': 271, 'low_res_detection_count': 47, 'high_res_detection_count': 9, 'dropped_frame_count': 28, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 18, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_234123_107398': {'decoded_frame_count': 300, 'processed_frame_count': 269, 'low_res_detection_count': 50, 'high_res_detection_count': 10, 'dropped_frame_count': 31, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 26, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
