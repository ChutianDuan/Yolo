# 多路实时监测验收报告

- 开始时间：2026-09-14T07:51:46.417421+00:00
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
| acceptance-1-20260914_075146_417450 | running | 22.630 | 3.533 | 0.833 | 153.72 | 221 | 0 | 2 |
| acceptance-2-20260914_075146_417450 | running | 21.430 | 3.366 | 0.667 | 180.63 | 257 | 0 | 2 |
| acceptance-3-20260914_075146_417450 | running | 24.929 | 3.933 | 0.833 | 145.19 | 154 | 0 | 2 |
| acceptance-4-20260914_075146_417450 | running | 25.596 | 3.933 | 1.066 | 123.41 | 133 | 0 | 2 |
| acceptance-5-20260914_075146_417450 | running | 23.296 | 3.599 | 0.867 | 178.83 | 201 | 0 | 2 |
| acceptance-6-20260914_075146_417450 | running | 21.263 | 3.433 | 0.867 | 174.93 | 262 | 0 | 2 |

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
| requested duration covered | PASS | 30.00488797505386 | >= 30.0 s |
| sampling continuity | PASS | 1.008646798087284 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_075146_417450: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_075146_417450: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_075146_417450: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_075146_417450: processed FPS | FAIL | 22.629646228458586 | >= 25.0 |
| acceptance-1-20260914_075146_417450: low detection FPS | FAIL | 3.5327577322777763 | >= 4.0 |
| acceptance-1-20260914_075146_417450: high detection FPS | PASS | 0.8331975783674 | >= 0.5 |
| acceptance-1-20260914_075146_417450: result age P95 | PASS | 153.717762 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_075146_417450: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_075146_417450: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_075146_417450: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_075146_417450: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_075146_417450: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_075146_417450: processed FPS | FAIL | 21.42984171560953 | >= 25.0 |
| acceptance-2-20260914_075146_417450: low detection FPS | FAIL | 3.3661182166042964 | >= 4.0 |
| acceptance-2-20260914_075146_417450: high detection FPS | PASS | 0.66655806269392 | >= 0.5 |
| acceptance-2-20260914_075146_417450: result age P95 | PASS | 180.631306 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_075146_417450: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_075146_417450: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_075146_417450: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_075146_417450: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_075146_417450: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_075146_417450: processed FPS | FAIL | 24.92927154475261 | >= 25.0 |
| acceptance-3-20260914_075146_417450: low detection FPS | FAIL | 3.932692569894128 | >= 4.0 |
| acceptance-3-20260914_075146_417450: high detection FPS | PASS | 0.8331975783674 | >= 0.5 |
| acceptance-3-20260914_075146_417450: result age P95 | PASS | 145.189302 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_075146_417450: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_075146_417450: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_075146_417450: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_075146_417450: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_075146_417450: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_075146_417450: processed FPS | PASS | 25.59582960744653 | >= 25.0 |
| acceptance-4-20260914_075146_417450: low detection FPS | FAIL | 3.932692569894128 | >= 4.0 |
| acceptance-4-20260914_075146_417450: high detection FPS | PASS | 1.066492900310272 | >= 0.5 |
| acceptance-4-20260914_075146_417450: result age P95 | PASS | 123.412179 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_075146_417450: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_075146_417450: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_075146_417450: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_075146_417450: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_075146_417450: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_075146_417450: processed FPS | FAIL | 23.296204291152506 | >= 25.0 |
| acceptance-5-20260914_075146_417450: low detection FPS | FAIL | 3.5994135385471684 | >= 4.0 |
| acceptance-5-20260914_075146_417450: high detection FPS | PASS | 0.8665254815020961 | >= 0.5 |
| acceptance-5-20260914_075146_417450: result age P95 | PASS | 178.830793 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_075146_417450: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_075146_417450: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_075146_417450: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_075146_417450: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_075146_417450: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_075146_417450: processed FPS | FAIL | 21.26320219993605 | >= 25.0 |
| acceptance-6-20260914_075146_417450: low detection FPS | FAIL | 3.4327740228736885 | >= 4.0 |
| acceptance-6-20260914_075146_417450: high detection FPS | PASS | 0.8665254815020961 | >= 0.5 |
| acceptance-6-20260914_075146_417450: result age P95 | PASS | 174.932507 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_075146_417450: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_075146_417450: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 16.92708333333333 | <= 10.0% |
| process CPU average | PASS | 61.138107379152615 | <= 85.0% of host |
| process RSS growth | PASS | -4.605810048435543 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.002994895912707, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_075146_417450': {'decoded_frame_count': 300, 'processed_frame_count': 229, 'low_res_detection_count': 32, 'high_res_detection_count': 3, 'dropped_frame_count': 69, 'decoder_queue_drop_count': 11, 'processor_coalesced_frame_count': 58, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_075146_417450': {'decoded_frame_count': 300, 'processed_frame_count': 227, 'low_res_detection_count': 29, 'high_res_detection_count': 4, 'dropped_frame_count': 72, 'decoder_queue_drop_count': 21, 'processor_coalesced_frame_count': 51, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_075146_417450': {'decoded_frame_count': 300, 'processed_frame_count': 240, 'low_res_detection_count': 32, 'high_res_detection_count': 7, 'dropped_frame_count': 58, 'decoder_queue_drop_count': 11, 'processor_coalesced_frame_count': 47, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_075146_417450': {'decoded_frame_count': 300, 'processed_frame_count': 240, 'low_res_detection_count': 32, 'high_res_detection_count': 7, 'dropped_frame_count': 59, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 49, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_075146_417450': {'decoded_frame_count': 300, 'processed_frame_count': 223, 'low_res_detection_count': 29, 'high_res_detection_count': 5, 'dropped_frame_count': 75, 'decoder_queue_drop_count': 22, 'processor_coalesced_frame_count': 52, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_075146_417450': {'decoded_frame_count': 300, 'processed_frame_count': 209, 'low_res_detection_count': 30, 'high_res_detection_count': 6, 'dropped_frame_count': 90, 'decoder_queue_drop_count': 27, 'processor_coalesced_frame_count': 63, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
