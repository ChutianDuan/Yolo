# 多路实时监测验收报告

- 开始时间：2026-09-22T01:01:10.290608+00:00
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
| acceptance-1-20260922_010110_290625 | running | 27.032 | 4.866 | 1.050 | 172.66 | 178 | 0 | 2 |
| acceptance-2-20260922_010110_290625 | running | 26.715 | 4.833 | 1.083 | 161.20 | 197 | 0 | 2 |
| acceptance-3-20260922_010110_290625 | running | 28.732 | 5.016 | 1.183 | 109.36 | 76 | 0 | 2 |
| acceptance-4-20260922_010110_290625 | running | 28.315 | 4.933 | 1.133 | 129.71 | 102 | 0 | 2 |
| acceptance-5-20260922_010110_290625 | running | 26.765 | 4.833 | 1.017 | 165.85 | 194 | 0 | 2 |
| acceptance-6-20260922_010110_290625 | running | 26.932 | 4.916 | 1.000 | 174.53 | 184 | 0 | 2 |

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
| requested duration covered | PASS | 60.00329153900384 | >= 60.0 s |
| sampling continuity | PASS | 1.0056390929967165 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260922_010110_290625: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260922_010110_290625: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260922_010110_290625: counters never reset | PASS | True | monotonic |
| acceptance-1-20260922_010110_290625: processed FPS | PASS | 27.031850393501397 | >= 25.0 |
| acceptance-1-20260922_010110_290625: low detection FPS | PASS | 4.8663997009262685 | >= 4.0 |
| acceptance-1-20260922_010110_290625: high detection FPS | PASS | 1.049942401227243 | >= 0.5 |
| acceptance-1-20260922_010110_290625: result age P95 | PASS | 172.655836 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260922_010110_290625: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260922_010110_290625: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260922_010110_290625: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260922_010110_290625: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260922_010110_290625: counters never reset | PASS | True | monotonic |
| acceptance-2-20260922_010110_290625: processed FPS | PASS | 26.715201097893182 | >= 25.0 |
| acceptance-2-20260922_010110_290625: low detection FPS | PASS | 4.833068196125404 | >= 4.0 |
| acceptance-2-20260922_010110_290625: high detection FPS | PASS | 1.0832739060281078 | >= 0.5 |
| acceptance-2-20260922_010110_290625: result age P95 | PASS | 161.198057 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260922_010110_290625: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260922_010110_290625: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260922_010110_290625: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260922_010110_290625: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260922_010110_290625: counters never reset | PASS | True | monotonic |
| acceptance-3-20260922_010110_290625: processed FPS | PASS | 28.731757138345504 | >= 25.0 |
| acceptance-3-20260922_010110_290625: low detection FPS | PASS | 5.01639147253016 | >= 4.0 |
| acceptance-3-20260922_010110_290625: high detection FPS | PASS | 1.1832684204307022 | >= 0.5 |
| acceptance-3-20260922_010110_290625: result age P95 | PASS | 109.363469 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260922_010110_290625: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260922_010110_290625: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260922_010110_290625: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260922_010110_290625: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260922_010110_290625: counters never reset | PASS | True | monotonic |
| acceptance-4-20260922_010110_290625: processed FPS | PASS | 28.315113328334693 | >= 25.0 |
| acceptance-4-20260922_010110_290625: low detection FPS | PASS | 4.933062710527999 | >= 4.0 |
| acceptance-4-20260922_010110_290625: high detection FPS | PASS | 1.133271163229405 | >= 0.5 |
| acceptance-4-20260922_010110_290625: result age P95 | PASS | 129.705563 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260922_010110_290625: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260922_010110_290625: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260922_010110_290625: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260922_010110_290625: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260922_010110_290625: counters never reset | PASS | True | monotonic |
| acceptance-5-20260922_010110_290625: processed FPS | PASS | 26.76519835509448 | >= 25.0 |
| acceptance-5-20260922_010110_290625: low detection FPS | PASS | 4.833068196125404 | >= 4.0 |
| acceptance-5-20260922_010110_290625: high detection FPS | PASS | 1.016610896426378 | >= 0.5 |
| acceptance-5-20260922_010110_290625: result age P95 | PASS | 165.852193 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260922_010110_290625: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260922_010110_290625: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260922_010110_290625: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260922_010110_290625: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260922_010110_290625: counters never reset | PASS | True | monotonic |
| acceptance-6-20260922_010110_290625: processed FPS | PASS | 26.9318558790988 | >= 25.0 |
| acceptance-6-20260922_010110_290625: low detection FPS | PASS | 4.916396958127566 | >= 4.0 |
| acceptance-6-20260922_010110_290625: high detection FPS | PASS | 0.9999451440259456 | >= 0.5 |
| acceptance-6-20260922_010110_290625: result age P95 | PASS | 174.525318 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260922_010110_290625: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260922_010110_290625: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 7.018561484918788 | <= 10.0% |
| process CPU average | PASS | 59.48544846525705 | <= 85.0% of host |
| process RSS growth | PASS | -3.097601161171371 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00258590100566, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260922_010110_290625': {'decoded_frame_count': 300, 'processed_frame_count': 255, 'low_res_detection_count': 46, 'high_res_detection_count': 9, 'dropped_frame_count': 45, 'decoder_queue_drop_count': 20, 'processor_coalesced_frame_count': 25, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260922_010110_290625': {'decoded_frame_count': 300, 'processed_frame_count': 276, 'low_res_detection_count': 47, 'high_res_detection_count': 11, 'dropped_frame_count': 24, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 16, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260922_010110_290625': {'decoded_frame_count': 300, 'processed_frame_count': 287, 'low_res_detection_count': 49, 'high_res_detection_count': 12, 'dropped_frame_count': 13, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 12, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260922_010110_290625': {'decoded_frame_count': 300, 'processed_frame_count': 287, 'low_res_detection_count': 50, 'high_res_detection_count': 12, 'dropped_frame_count': 12, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 7, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260922_010110_290625': {'decoded_frame_count': 300, 'processed_frame_count': 279, 'low_res_detection_count': 48, 'high_res_detection_count': 10, 'dropped_frame_count': 21, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 13, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260922_010110_290625': {'decoded_frame_count': 300, 'processed_frame_count': 273, 'low_res_detection_count': 50, 'high_res_detection_count': 10, 'dropped_frame_count': 26, 'decoder_queue_drop_count': 7, 'processor_coalesced_frame_count': 19, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
