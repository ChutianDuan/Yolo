# 多路实时监测验收报告

- 开始时间：2026-09-14T09:05:49.296459+00:00
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
| acceptance-1-20260914_090549_296476 | running | 24.963 | 4.399 | 0.967 | 133.34 | 152 | 0 | 2 |
| acceptance-2-20260914_090549_296476 | running | 23.096 | 4.266 | 0.800 | 149.66 | 209 | 0 | 2 |
| acceptance-3-20260914_090549_296476 | running | 27.329 | 4.766 | 1.000 | 121.17 | 80 | 0 | 2 |
| acceptance-4-20260914_090549_296476 | running | 26.629 | 4.866 | 1.033 | 115.09 | 101 | 0 | 2 |
| acceptance-5-20260914_090549_296476 | running | 22.630 | 4.433 | 0.833 | 136.59 | 222 | 0 | 2 |
| acceptance-6-20260914_090549_296476 | running | 24.596 | 4.299 | 0.933 | 191.73 | 163 | 0 | 2 |

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
| requested duration covered | PASS | 30.00480775698088 | >= 30.0 s |
| sampling continuity | PASS | 1.005758441053331 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_090549_296476: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_090549_296476: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_090549_296476: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_090549_296476: processed FPS | FAIL | 24.962666185579497 | >= 25.0 |
| acceptance-1-20260914_090549_296476: low detection FPS | PASS | 4.399294975295719 | >= 4.0 |
| acceptance-1-20260914_090549_296476: high detection FPS | PASS | 0.9665117748755746 | >= 0.5 |
| acceptance-1-20260914_090549_296476: result age P95 | PASS | 133.340767 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_090549_296476: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_090549_296476: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_090549_296476: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_090549_296476: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_090549_296476: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_090549_296476: processed FPS | FAIL | 23.096298620302523 | >= 25.0 |
| acceptance-2-20260914_090549_296476: low detection FPS | PASS | 4.265983006347364 | >= 4.0 |
| acceptance-2-20260914_090549_296476: high detection FPS | PASS | 0.7998718136901307 | >= 0.5 |
| acceptance-2-20260914_090549_296476: result age P95 | PASS | 149.658328 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_090549_296476: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_090549_296476: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_090549_296476: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_090549_296476: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_090549_296476: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_090549_296476: processed FPS | PASS | 27.328953634412798 | >= 25.0 |
| acceptance-3-20260914_090549_296476: low detection FPS | PASS | 4.765902889903695 | >= 4.0 |
| acceptance-3-20260914_090549_296476: high detection FPS | PASS | 0.9998397671126634 | >= 0.5 |
| acceptance-3-20260914_090549_296476: result age P95 | PASS | 121.166875 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_090549_296476: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_090549_296476: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_090549_296476: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_090549_296476: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_090549_296476: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_090549_296476: processed FPS | PASS | 26.629065797433935 | >= 25.0 |
| acceptance-4-20260914_090549_296476: low detection FPS | PASS | 4.865886866614962 | >= 4.0 |
| acceptance-4-20260914_090549_296476: high detection FPS | PASS | 1.0331677593497521 | >= 0.5 |
| acceptance-4-20260914_090549_296476: result age P95 | PASS | 115.085147 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_090549_296476: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_090549_296476: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_090549_296476: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_090549_296476: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_090549_296476: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_090549_296476: processed FPS | FAIL | 22.62970672898328 | >= 25.0 |
| acceptance-5-20260914_090549_296476: low detection FPS | PASS | 4.432622967532808 | >= 4.0 |
| acceptance-5-20260914_090549_296476: high detection FPS | PASS | 0.8331998059272195 | >= 0.5 |
| acceptance-5-20260914_090549_296476: result age P95 | PASS | 136.593758 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_090549_296476: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_090549_296476: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_090549_296476: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_090549_296476: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_090549_296476: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_090549_296476: processed FPS | FAIL | 24.59605827097152 | >= 25.0 |
| acceptance-6-20260914_090549_296476: low detection FPS | PASS | 4.299310998584453 | >= 4.0 |
| acceptance-6-20260914_090549_296476: high detection FPS | PASS | 0.9331837826384858 | >= 0.5 |
| acceptance-6-20260914_090549_296476: result age P95 | PASS | 191.726112 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_090549_296476: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_090549_296476: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 17.195121951219516 | <= 10.0% |
| process CPU average | PASS | 60.94457179801369 | <= 85.0% of host |
| process RSS growth | PASS | -7.810867558603062 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003243931103498, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_090549_296476': {'decoded_frame_count': 300, 'processed_frame_count': 250, 'low_res_detection_count': 48, 'high_res_detection_count': 8, 'dropped_frame_count': 49, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 39, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_090549_296476': {'decoded_frame_count': 300, 'processed_frame_count': 213, 'low_res_detection_count': 39, 'high_res_detection_count': 10, 'dropped_frame_count': 84, 'decoder_queue_drop_count': 31, 'processor_coalesced_frame_count': 53, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_090549_296476': {'decoded_frame_count': 300, 'processed_frame_count': 268, 'low_res_detection_count': 48, 'high_res_detection_count': 10, 'dropped_frame_count': 32, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 31, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_090549_296476': {'decoded_frame_count': 300, 'processed_frame_count': 262, 'low_res_detection_count': 47, 'high_res_detection_count': 8, 'dropped_frame_count': 37, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 27, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_090549_296476': {'decoded_frame_count': 300, 'processed_frame_count': 263, 'low_res_detection_count': 45, 'high_res_detection_count': 9, 'dropped_frame_count': 35, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 27, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_090549_296476': {'decoded_frame_count': 300, 'processed_frame_count': 231, 'low_res_detection_count': 43, 'high_res_detection_count': 8, 'dropped_frame_count': 67, 'decoder_queue_drop_count': 20, 'processor_coalesced_frame_count': 47, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
