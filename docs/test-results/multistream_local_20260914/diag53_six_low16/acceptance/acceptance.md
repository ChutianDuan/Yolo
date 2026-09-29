# 多路实时监测验收报告

- 开始时间：2026-09-14T07:32:05.461132+00:00
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
| acceptance-1-20260914_073205_461154 | running | 22.064 | 3.400 | 0.933 | 141.03 | 237 | 0 | 2 |
| acceptance-2-20260914_073205_461154 | running | 19.664 | 3.066 | 0.800 | 227.27 | 310 | 0 | 2 |
| acceptance-3-20260914_073205_461154 | running | 25.697 | 3.799 | 0.867 | 124.14 | 130 | 0 | 2 |
| acceptance-4-20260914_073205_461154 | running | 24.397 | 3.666 | 0.933 | 133.25 | 169 | 0 | 2 |
| acceptance-5-20260914_073205_461154 | running | 23.730 | 3.566 | 0.800 | 172.07 | 188 | 0 | 2 |
| acceptance-6-20260914_073205_461154 | running | 20.664 | 3.200 | 0.667 | 209.53 | 279 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2184 | 3a434c75a9e23da5d1af7601ef55bb078d760c56174bf436fcaade6000d275eb | 3a434c75a9e23da5d1af7601ef55bb078d760c56174bf436fcaade6000d275eb | True |
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
| requested duration covered | PASS | 30.00403999397531 | >= 30.0 s |
| sampling continuity | PASS | 1.0067937490530312 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_073205_461154: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_073205_461154: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_073205_461154: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_073205_461154: processed FPS | FAIL | 22.06369542678009 | >= 25.0 |
| acceptance-1-20260914_073205_461154: low detection FPS | FAIL | 3.399542195667023 | >= 4.0 |
| acceptance-1-20260914_073205_461154: high detection FPS | PASS | 0.9332076615556534 | >= 0.5 |
| acceptance-1-20260914_073205_461154: result age P95 | PASS | 141.027687 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_073205_461154: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_073205_461154: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_073205_461154: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_073205_461154: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_073205_461154: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_073205_461154: processed FPS | FAIL | 19.66401858277984 | >= 25.0 |
| acceptance-2-20260914_073205_461154: low detection FPS | FAIL | 3.0662537451114322 | >= 4.0 |
| acceptance-2-20260914_073205_461154: high detection FPS | PASS | 0.7998922813334172 | >= 0.5 |
| acceptance-2-20260914_073205_461154: result age P95 | PASS | 227.270541 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_073205_461154: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_073205_461154: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_073205_461154: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_073205_461154: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_073205_461154: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_073205_461154: processed FPS | PASS | 25.696539537836028 | >= 25.0 |
| acceptance-3-20260914_073205_461154: low detection FPS | FAIL | 3.7994883363337317 | >= 4.0 |
| acceptance-3-20260914_073205_461154: high detection FPS | PASS | 0.8665499714445353 | >= 0.5 |
| acceptance-3-20260914_073205_461154: result age P95 | PASS | 124.136382 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_073205_461154: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_073205_461154: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_073205_461154: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_073205_461154: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_073205_461154: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_073205_461154: processed FPS | FAIL | 24.396714580669222 | >= 25.0 |
| acceptance-4-20260914_073205_461154: low detection FPS | FAIL | 3.6661729561114953 | >= 4.0 |
| acceptance-4-20260914_073205_461154: high detection FPS | PASS | 0.9332076615556534 | >= 0.5 |
| acceptance-4-20260914_073205_461154: result age P95 | PASS | 133.252739 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_073205_461154: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_073205_461154: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_073205_461154: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_073205_461154: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_073205_461154: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_073205_461154: processed FPS | FAIL | 23.73013767955804 | >= 25.0 |
| acceptance-5-20260914_073205_461154: low detection FPS | FAIL | 3.566186420944818 | >= 4.0 |
| acceptance-5-20260914_073205_461154: high detection FPS | PASS | 0.7998922813334172 | >= 0.5 |
| acceptance-5-20260914_073205_461154: result age P95 | PASS | 172.071007 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_073205_461154: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_073205_461154: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_073205_461154: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_073205_461154: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_073205_461154: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_073205_461154: processed FPS | FAIL | 20.66388393444661 | >= 25.0 |
| acceptance-6-20260914_073205_461154: low detection FPS | FAIL | 3.1995691253336687 | >= 4.0 |
| acceptance-6-20260914_073205_461154: high detection FPS | PASS | 0.666576901111181 | >= 0.5 |
| acceptance-6-20260914_073205_461154: result age P95 | PASS | 209.528427 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_073205_461154: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_073205_461154: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 23.476005188067447 | <= 10.0% |
| process CPU average | PASS | 60.58047673767074 | <= 85.0% of host |
| process RSS growth | PASS | -0.13184685737174137 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.002974852919579, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_073205_461154': {'decoded_frame_count': 300, 'processed_frame_count': 211, 'low_res_detection_count': 29, 'high_res_detection_count': 4, 'dropped_frame_count': 88, 'decoder_queue_drop_count': 19, 'processor_coalesced_frame_count': 69, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_073205_461154': {'decoded_frame_count': 300, 'processed_frame_count': 237, 'low_res_detection_count': 31, 'high_res_detection_count': 5, 'dropped_frame_count': 62, 'decoder_queue_drop_count': 12, 'processor_coalesced_frame_count': 50, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_073205_461154': {'decoded_frame_count': 300, 'processed_frame_count': 227, 'low_res_detection_count': 30, 'high_res_detection_count': 4, 'dropped_frame_count': 71, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 61, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_073205_461154': {'decoded_frame_count': 300, 'processed_frame_count': 230, 'low_res_detection_count': 29, 'high_res_detection_count': 6, 'dropped_frame_count': 68, 'decoder_queue_drop_count': 15, 'processor_coalesced_frame_count': 53, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_073205_461154': {'decoded_frame_count': 300, 'processed_frame_count': 233, 'low_res_detection_count': 29, 'high_res_detection_count': 4, 'dropped_frame_count': 66, 'decoder_queue_drop_count': 12, 'processor_coalesced_frame_count': 54, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_073205_461154': {'decoded_frame_count': 300, 'processed_frame_count': 213, 'low_res_detection_count': 25, 'high_res_detection_count': 5, 'dropped_frame_count': 86, 'decoder_queue_drop_count': 43, 'processor_coalesced_frame_count': 43, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
