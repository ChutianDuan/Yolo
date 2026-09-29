# 多路实时监测验收报告

- 开始时间：2026-09-14T08:18:20.896876+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.006/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_081820_896907 | running | 22.230 | 4.366 | 0.900 | 192.79 | 233 | 0 | 2 |
| acceptance-2-20260914_081820_896907 | running | 22.463 | 4.533 | 0.767 | 167.59 | 225 | 0 | 2 |
| acceptance-3-20260914_081820_896907 | running | 23.697 | 4.499 | 0.767 | 175.30 | 190 | 0 | 2 |
| acceptance-4-20260914_081820_896907 | running | 22.630 | 4.633 | 0.967 | 131.14 | 221 | 0 | 2 |
| acceptance-5-20260914_081820_896907 | running | 22.763 | 4.366 | 0.900 | 124.44 | 217 | 0 | 2 |
| acceptance-6-20260914_081820_896907 | running | 19.397 | 4.033 | 0.733 | 254.42 | 317 | 0 | 2 |

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
| requested duration covered | PASS | 30.00442270305939 | >= 30.0 s |
| sampling continuity | PASS | 1.0065339158754796 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_081820_896907: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_081820_896907: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_081820_896907: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_081820_896907: processed FPS | FAIL | 22.23005610209556 | >= 25.0 |
| acceptance-1-20260914_081820_896907: low detection FPS | PASS | 4.3660230125555 | >= 4.0 |
| acceptance-1-20260914_081820_896907: high detection FPS | PASS | 0.8998673384656374 | >= 0.5 |
| acceptance-1-20260914_081820_896907: result age P95 | PASS | 192.789083 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_081820_896907: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_081820_896907: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_081820_896907: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_081820_896907: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_081820_896907: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_081820_896907: processed FPS | FAIL | 22.463355041697763 | >= 25.0 |
| acceptance-2-20260914_081820_896907: low detection FPS | PASS | 4.532665112271359 | >= 4.0 |
| acceptance-2-20260914_081820_896907: high detection FPS | PASS | 0.7665536586929504 | >= 0.5 |
| acceptance-2-20260914_081820_896907: result age P95 | PASS | 167.591487 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_081820_896907: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_081820_896907: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_081820_896907: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_081820_896907: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_081820_896907: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_081820_896907: processed FPS | FAIL | 23.69650657959512 | >= 25.0 |
| acceptance-3-20260914_081820_896907: low detection FPS | PASS | 4.499336692328187 | >= 4.0 |
| acceptance-3-20260914_081820_896907: high detection FPS | PASS | 0.7665536586929504 | >= 0.5 |
| acceptance-3-20260914_081820_896907: result age P95 | PASS | 175.295993 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_081820_896907: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_081820_896907: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_081820_896907: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_081820_896907: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_081820_896907: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_081820_896907: processed FPS | FAIL | 22.62999714141362 | >= 25.0 |
| acceptance-4-20260914_081820_896907: low detection FPS | PASS | 4.6326503721008745 | >= 4.0 |
| acceptance-4-20260914_081820_896907: high detection FPS | PASS | 0.9665241783519809 | >= 0.5 |
| acceptance-4-20260914_081820_896907: result age P95 | PASS | 131.141051 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_081820_896907: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_081820_896907: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_081820_896907: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_081820_896907: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_081820_896907: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_081820_896907: processed FPS | FAIL | 22.76331082118631 | >= 25.0 |
| acceptance-5-20260914_081820_896907: low detection FPS | PASS | 4.3660230125555 | >= 4.0 |
| acceptance-5-20260914_081820_896907: high detection FPS | PASS | 0.8998673384656374 | >= 0.5 |
| acceptance-5-20260914_081820_896907: result age P95 | PASS | 124.438025 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_081820_896907: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_081820_896907: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_081820_896907: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_081820_896907: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_081820_896907: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_081820_896907: processed FPS | FAIL | 19.397140406925963 | >= 25.0 |
| acceptance-6-20260914_081820_896907: low detection FPS | PASS | 4.032738813123783 | >= 4.0 |
| acceptance-6-20260914_081820_896907: high detection FPS | PASS | 0.7332252387497786 | >= 0.5 |
| acceptance-6-20260914_081820_896907: result age P95 | PASS | 254.415326 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_081820_896907: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_081820_896907: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 18.14345991561181 | <= 10.0% |
| process CPU average | PASS | 60.785585404300704 | <= 85.0% of host |
| process RSS growth | PASS | -2.6771007055805 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00646052788943, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_081820_896907': {'decoded_frame_count': 300, 'processed_frame_count': 223, 'low_res_detection_count': 40, 'high_res_detection_count': 7, 'dropped_frame_count': 77, 'decoder_queue_drop_count': 17, 'processor_coalesced_frame_count': 60, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_081820_896907': {'decoded_frame_count': 300, 'processed_frame_count': 214, 'low_res_detection_count': 35, 'high_res_detection_count': 7, 'dropped_frame_count': 85, 'decoder_queue_drop_count': 40, 'processor_coalesced_frame_count': 45, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_081820_896907': {'decoded_frame_count': 300, 'processed_frame_count': 224, 'low_res_detection_count': 39, 'high_res_detection_count': 6, 'dropped_frame_count': 75, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 65, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_081820_896907': {'decoded_frame_count': 300, 'processed_frame_count': 222, 'low_res_detection_count': 37, 'high_res_detection_count': 5, 'dropped_frame_count': 77, 'decoder_queue_drop_count': 17, 'processor_coalesced_frame_count': 60, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_081820_896907': {'decoded_frame_count': 300, 'processed_frame_count': 216, 'low_res_detection_count': 36, 'high_res_detection_count': 8, 'dropped_frame_count': 83, 'decoder_queue_drop_count': 28, 'processor_coalesced_frame_count': 55, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_081820_896907': {'decoded_frame_count': 300, 'processed_frame_count': 187, 'low_res_detection_count': 35, 'high_res_detection_count': 5, 'dropped_frame_count': 112, 'decoder_queue_drop_count': 56, 'processor_coalesced_frame_count': 56, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
