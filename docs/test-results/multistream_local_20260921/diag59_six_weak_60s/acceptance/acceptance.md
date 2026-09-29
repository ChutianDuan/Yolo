# 多路实时监测验收报告

- 开始时间：2026-09-21T10:26:35.082273+00:00
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
| acceptance-1-20260921_102635_082288 | running | 26.748 | 4.933 | 1.167 | 154.72 | 195 | 0 | 2 |
| acceptance-2-20260921_102635_082288 | running | 25.698 | 4.766 | 0.983 | 187.89 | 257 | 0 | 2 |
| acceptance-3-20260921_102635_082288 | running | 28.365 | 5.000 | 1.217 | 108.02 | 99 | 0 | 2 |
| acceptance-4-20260921_102635_082288 | running | 28.198 | 4.916 | 1.150 | 129.50 | 110 | 0 | 2 |
| acceptance-5-20260921_102635_082288 | running | 25.132 | 4.833 | 1.033 | 181.44 | 293 | 0 | 2 |
| acceptance-6-20260921_102635_082288 | running | 27.098 | 4.900 | 1.033 | 192.98 | 174 | 0 | 2 |

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
| requested duration covered | PASS | 60.00372912199964 | >= 60.0 s |
| sampling continuity | PASS | 1.005629882998619 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_102635_082288: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_102635_082288: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_102635_082288: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_102635_082288: processed FPS | PASS | 26.748337536433983 | >= 25.0 |
| acceptance-1-20260921_102635_082288: low detection FPS | PASS | 4.933026735691252 | >= 4.0 |
| acceptance-1-20260921_102635_082288: high detection FPS | PASS | 1.1665941604675258 | >= 0.5 |
| acceptance-1-20260921_102635_082288: result age P95 | PASS | 154.717597 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_102635_082288: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_102635_082288: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_102635_082288: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_102635_082288: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_102635_082288: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_102635_082288: processed FPS | PASS | 25.698402792013212 | >= 25.0 |
| acceptance-2-20260921_102635_082288: low detection FPS | PASS | 4.766370427053034 | >= 4.0 |
| acceptance-2-20260921_102635_082288: high detection FPS | PASS | 0.983272220965486 | >= 0.5 |
| acceptance-2-20260921_102635_082288: result age P95 | PASS | 187.88976 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_102635_082288: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_102635_082288: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_102635_082288: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_102635_082288: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_102635_082288: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_102635_082288: processed FPS | PASS | 28.3649037302247 | >= 25.0 |
| acceptance-3-20260921_102635_082288: low detection FPS | PASS | 4.999689259146539 | >= 4.0 |
| acceptance-3-20260921_102635_082288: high detection FPS | PASS | 1.2165910530589912 | >= 0.5 |
| acceptance-3-20260921_102635_082288: result age P95 | PASS | 108.020022 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_102635_082288: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_102635_082288: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_102635_082288: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_102635_082288: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_102635_082288: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_102635_082288: processed FPS | PASS | 28.19824742158648 | >= 25.0 |
| acceptance-4-20260921_102635_082288: low detection FPS | PASS | 4.9163611048274305 | >= 4.0 |
| acceptance-4-20260921_102635_082288: high detection FPS | PASS | 1.149928529603704 | >= 0.5 |
| acceptance-4-20260921_102635_082288: result age P95 | PASS | 129.498219 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_102635_082288: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_102635_082288: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_102635_082288: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_102635_082288: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_102635_082288: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_102635_082288: processed FPS | PASS | 25.13177134264327 | >= 25.0 |
| acceptance-5-20260921_102635_082288: low detection FPS | PASS | 4.833032950508321 | >= 4.0 |
| acceptance-5-20260921_102635_082288: high detection FPS | PASS | 1.0332691135569514 | >= 0.5 |
| acceptance-5-20260921_102635_082288: result age P95 | PASS | 181.435165 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_102635_082288: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_102635_082288: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_102635_082288: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_102635_082288: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_102635_082288: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_102635_082288: processed FPS | PASS | 27.098315784574243 | >= 25.0 |
| acceptance-6-20260921_102635_082288: low detection FPS | PASS | 4.899695473963608 | >= 4.0 |
| acceptance-6-20260921_102635_082288: high detection FPS | PASS | 1.0332691135569514 | >= 0.5 |
| acceptance-6-20260921_102635_082288: result age P95 | PASS | 192.981849 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_102635_082288: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_102635_082288: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 11.398354876615754 | <= 10.0% |
| process CPU average | PASS | 60.14493436544157 | <= 85.0% of host |
| process RSS growth | PASS | 2.181346148534814 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.002430986998661, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_102635_082288': {'decoded_frame_count': 300, 'processed_frame_count': 264, 'low_res_detection_count': 50, 'high_res_detection_count': 9, 'dropped_frame_count': 36, 'decoder_queue_drop_count': 6, 'processor_coalesced_frame_count': 30, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_102635_082288': {'decoded_frame_count': 300, 'processed_frame_count': 247, 'low_res_detection_count': 48, 'high_res_detection_count': 10, 'dropped_frame_count': 52, 'decoder_queue_drop_count': 12, 'processor_coalesced_frame_count': 40, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_102635_082288': {'decoded_frame_count': 300, 'processed_frame_count': 292, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 7, 'decoder_queue_drop_count': 2, 'processor_coalesced_frame_count': 5, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_102635_082288': {'decoded_frame_count': 300, 'processed_frame_count': 286, 'low_res_detection_count': 49, 'high_res_detection_count': 9, 'dropped_frame_count': 12, 'decoder_queue_drop_count': 0, 'processor_coalesced_frame_count': 12, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_102635_082288': {'decoded_frame_count': 300, 'processed_frame_count': 262, 'low_res_detection_count': 47, 'high_res_detection_count': 10, 'dropped_frame_count': 36, 'decoder_queue_drop_count': 11, 'processor_coalesced_frame_count': 25, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_102635_082288': {'decoded_frame_count': 300, 'processed_frame_count': 283, 'low_res_detection_count': 45, 'high_res_detection_count': 11, 'dropped_frame_count': 16, 'decoder_queue_drop_count': 7, 'processor_coalesced_frame_count': 9, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
