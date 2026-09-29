# 多路实时监测验收报告

- 开始时间：2026-09-21T10:46:11.768803+00:00
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
| acceptance-1-20260921_104611_768816 | running | 26.582 | 4.900 | 1.033 | 161.36 | 205 | 0 | 2 |
| acceptance-2-20260921_104611_768816 | running | 25.582 | 4.750 | 0.983 | 199.47 | 265 | 0 | 2 |
| acceptance-3-20260921_104611_768816 | running | 28.215 | 4.950 | 1.083 | 130.07 | 107 | 0 | 2 |
| acceptance-4-20260921_104611_768816 | running | 27.932 | 4.966 | 1.050 | 143.26 | 125 | 0 | 2 |
| acceptance-5-20260921_104611_768816 | running | 25.415 | 4.766 | 1.000 | 168.31 | 275 | 0 | 2 |
| acceptance-6-20260921_104611_768816 | running | 25.648 | 4.833 | 0.983 | 190.90 | 262 | 0 | 2 |

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
| requested duration covered | PASS | 60.00359033700079 | >= 60.0 s |
| sampling continuity | PASS | 1.0092867159983143 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_104611_768816: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_104611_768816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_104611_768816: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_104611_768816: processed FPS | PASS | 26.58174270976006 | >= 25.0 |
| acceptance-1-20260921_104611_768816: low detection FPS | PASS | 4.8997068066893155 | >= 4.0 |
| acceptance-1-20260921_104611_768816: high detection FPS | PASS | 1.0332715034514883 | >= 0.5 |
| acceptance-1-20260921_104611_768816: result age P95 | PASS | 161.357443 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_104611_768816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_104611_768816: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_104611_768816: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_104611_768816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_104611_768816: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_104611_768816: processed FPS | PASS | 25.58180254512959 | >= 25.0 |
| acceptance-2-20260921_104611_768816: low detection FPS | PASS | 4.749715781994745 | >= 4.0 |
| acceptance-2-20260921_104611_768816: high detection FPS | PASS | 0.9832744952199647 | >= 0.5 |
| acceptance-2-20260921_104611_768816: result age P95 | PASS | 199.465886 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_104611_768816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_104611_768816: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_104611_768816: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_104611_768816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_104611_768816: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_104611_768816: processed FPS | PASS | 28.214978311989835 | >= 25.0 |
| acceptance-3-20260921_104611_768816: low detection FPS | PASS | 4.949703814920839 | >= 4.0 |
| acceptance-3-20260921_104611_768816: high detection FPS | PASS | 1.0832685116830119 | >= 0.5 |
| acceptance-3-20260921_104611_768816: result age P95 | PASS | 130.070516 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_104611_768816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_104611_768816: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_104611_768816: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_104611_768816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_104611_768816: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_104611_768816: processed FPS | PASS | 27.9316619320112 | >= 25.0 |
| acceptance-4-20260921_104611_768816: low detection FPS | PASS | 4.966369484331347 | >= 4.0 |
| acceptance-4-20260921_104611_768816: high detection FPS | PASS | 1.0499371728619962 | >= 0.5 |
| acceptance-4-20260921_104611_768816: result age P95 | PASS | 143.255653 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_104611_768816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_104611_768816: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_104611_768816: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_104611_768816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_104611_768816: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_104611_768816: processed FPS | PASS | 25.415145851024512 | >= 25.0 |
| acceptance-5-20260921_104611_768816: low detection FPS | PASS | 4.766381451405253 | >= 4.0 |
| acceptance-5-20260921_104611_768816: high detection FPS | PASS | 0.9999401646304725 | >= 0.5 |
| acceptance-5-20260921_104611_768816: result age P95 | PASS | 168.307375 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_104611_768816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_104611_768816: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_104611_768816: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_104611_768816: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_104611_768816: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_104611_768816: processed FPS | PASS | 25.64846522277162 | >= 25.0 |
| acceptance-6-20260921_104611_768816: low detection FPS | PASS | 4.833044129047284 | >= 4.0 |
| acceptance-6-20260921_104611_768816: high detection FPS | PASS | 0.9832744952199647 | >= 0.5 |
| acceptance-6-20260921_104611_768816: result age P95 | PASS | 190.900347 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_104611_768816: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_104611_768816: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 9.923213230950974 | <= 10.0% |
| process CPU average | PASS | 60.47335343540233 | <= 85.0% of host |
| process RSS growth | PASS | -4.501134089392929 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003390254001715, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_104611_768816': {'decoded_frame_count': 300, 'processed_frame_count': 277, 'low_res_detection_count': 48, 'high_res_detection_count': 9, 'dropped_frame_count': 23, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 13, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_104611_768816': {'decoded_frame_count': 300, 'processed_frame_count': 259, 'low_res_detection_count': 48, 'high_res_detection_count': 8, 'dropped_frame_count': 40, 'decoder_queue_drop_count': 13, 'processor_coalesced_frame_count': 27, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_104611_768816': {'decoded_frame_count': 300, 'processed_frame_count': 288, 'low_res_detection_count': 50, 'high_res_detection_count': 11, 'dropped_frame_count': 12, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 11, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_104611_768816': {'decoded_frame_count': 300, 'processed_frame_count': 261, 'low_res_detection_count': 47, 'high_res_detection_count': 9, 'dropped_frame_count': 38, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 30, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_104611_768816': {'decoded_frame_count': 300, 'processed_frame_count': 247, 'low_res_detection_count': 48, 'high_res_detection_count': 10, 'dropped_frame_count': 53, 'decoder_queue_drop_count': 13, 'processor_coalesced_frame_count': 40, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_104611_768816': {'decoded_frame_count': 300, 'processed_frame_count': 253, 'low_res_detection_count': 44, 'high_res_detection_count': 10, 'dropped_frame_count': 46, 'decoder_queue_drop_count': 18, 'processor_coalesced_frame_count': 28, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
