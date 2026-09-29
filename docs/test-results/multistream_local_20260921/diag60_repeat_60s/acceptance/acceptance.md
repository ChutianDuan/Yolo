# 多路实时监测验收报告

- 开始时间：2026-09-21T10:50:34.858558+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.002/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260921_105034_858574 | running | 25.898 | 4.816 | 1.083 | 173.42 | 246 | 0 | 2 |
| acceptance-2-20260921_105034_858574 | running | 25.998 | 4.866 | 1.067 | 188.03 | 241 | 0 | 2 |
| acceptance-3-20260921_105034_858574 | running | 27.765 | 4.933 | 1.217 | 121.51 | 133 | 0 | 2 |
| acceptance-4-20260921_105034_858574 | running | 27.498 | 4.966 | 1.150 | 142.21 | 148 | 0 | 2 |
| acceptance-5-20260921_105034_858574 | running | 26.082 | 4.883 | 1.050 | 186.15 | 235 | 0 | 2 |
| acceptance-6-20260921_105034_858574 | running | 24.882 | 4.783 | 1.017 | 189.87 | 307 | 0 | 2 |

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
| requested duration covered | PASS | 60.00385744900268 | >= 60.0 s |
| sampling continuity | PASS | 1.0063577869950677 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_105034_858574: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_105034_858574: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_105034_858574: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_105034_858574: processed FPS | PASS | 25.898334974893 | >= 25.0 |
| acceptance-1-20260921_105034_858574: low detection FPS | PASS | 4.816357019140333 | >= 4.0 |
| acceptance-1-20260921_105034_858574: high detection FPS | PASS | 1.0832636894260264 | >= 0.5 |
| acceptance-1-20260921_105034_858574: result age P95 | PASS | 173.42233 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_105034_858574: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_105034_858574: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_105034_858574: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_105034_858574: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_105034_858574: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_105034_858574: processed FPS | PASS | 25.998328546224634 | >= 25.0 |
| acceptance-2-20260921_105034_858574: low detection FPS | PASS | 4.866353804806149 | >= 4.0 |
| acceptance-2-20260921_105034_858574: high detection FPS | PASS | 1.0665980942040876 | >= 0.5 |
| acceptance-2-20260921_105034_858574: result age P95 | PASS | 188.029997 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_105034_858574: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_105034_858574: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_105034_858574: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_105034_858574: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_105034_858574: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_105034_858574: processed FPS | PASS | 27.764881639750154 | >= 25.0 |
| acceptance-3-20260921_105034_858574: low detection FPS | PASS | 4.933016185693905 | >= 4.0 |
| acceptance-3-20260921_105034_858574: high detection FPS | PASS | 1.2165884512015372 | >= 0.5 |
| acceptance-3-20260921_105034_858574: result age P95 | PASS | 121.511957 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_105034_858574: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_105034_858574: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_105034_858574: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_105034_858574: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_105034_858574: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_105034_858574: processed FPS | PASS | 27.49823211619913 | >= 25.0 |
| acceptance-4-20260921_105034_858574: low detection FPS | PASS | 4.966347376137783 | >= 4.0 |
| acceptance-4-20260921_105034_858574: high detection FPS | PASS | 1.149926070313782 | >= 0.5 |
| acceptance-4-20260921_105034_858574: result age P95 | PASS | 142.209624 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_105034_858574: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_105034_858574: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_105034_858574: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_105034_858574: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_105034_858574: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_105034_858574: processed FPS | PASS | 26.081656522334328 | >= 25.0 |
| acceptance-5-20260921_105034_858574: low detection FPS | PASS | 4.883019400028088 | >= 4.0 |
| acceptance-5-20260921_105034_858574: high detection FPS | PASS | 1.0499324989821486 | >= 0.5 |
| acceptance-5-20260921_105034_858574: result age P95 | PASS | 186.1546 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_105034_858574: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_105034_858574: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_105034_858574: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_105034_858574: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_105034_858574: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_105034_858574: processed FPS | FAIL | 24.88173366635473 | >= 25.0 |
| acceptance-6-20260921_105034_858574: low detection FPS | PASS | 4.783025828696455 | >= 4.0 |
| acceptance-6-20260921_105034_858574: high detection FPS | PASS | 1.016601308538271 | >= 0.5 |
| acceptance-6-20260921_105034_858574: result age P95 | PASS | 189.867575 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_105034_858574: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_105034_858574: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 10.384153661464584 | <= 10.0% |
| process CPU average | PASS | 60.059613187200334 | <= 85.0% of host |
| process RSS growth | FAIL | 7.223619155247916 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.002375963995291, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_105034_858574': {'decoded_frame_count': 300, 'processed_frame_count': 281, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 19, 'decoder_queue_drop_count': 6, 'processor_coalesced_frame_count': 13, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_105034_858574': {'decoded_frame_count': 301, 'processed_frame_count': 259, 'low_res_detection_count': 47, 'high_res_detection_count': 11, 'dropped_frame_count': 40, 'decoder_queue_drop_count': 16, 'processor_coalesced_frame_count': 24, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_105034_858574': {'decoded_frame_count': 300, 'processed_frame_count': 296, 'low_res_detection_count': 50, 'high_res_detection_count': 11, 'dropped_frame_count': 4, 'decoder_queue_drop_count': 0, 'processor_coalesced_frame_count': 4, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_105034_858574': {'decoded_frame_count': 300, 'processed_frame_count': 283, 'low_res_detection_count': 49, 'high_res_detection_count': 11, 'dropped_frame_count': 17, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 16, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_105034_858574': {'decoded_frame_count': 300, 'processed_frame_count': 270, 'low_res_detection_count': 47, 'high_res_detection_count': 11, 'dropped_frame_count': 30, 'decoder_queue_drop_count': 12, 'processor_coalesced_frame_count': 17, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_105034_858574': {'decoded_frame_count': 300, 'processed_frame_count': 277, 'low_res_detection_count': 47, 'high_res_detection_count': 12, 'dropped_frame_count': 22, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 14, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
