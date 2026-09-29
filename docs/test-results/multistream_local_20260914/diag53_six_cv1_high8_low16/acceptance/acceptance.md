# 多路实时监测验收报告

- 开始时间：2026-09-14T07:36:20.017952+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.004/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_073620_017974 | running | 22.260 | 3.899 | 0.100 | 125.13 | 232 | 0 | 2 |
| acceptance-2-20260914_073620_017974 | running | 18.794 | 3.032 | 0.033 | 1067.09 | 338 | 0 | 2 |
| acceptance-3-20260914_073620_017974 | running | 26.192 | 4.265 | 0.133 | 140.49 | 113 | 0 | 2 |
| acceptance-4-20260914_073620_017974 | running | 23.859 | 4.032 | 0.067 | 136.01 | 185 | 0 | 2 |
| acceptance-5-20260914_073620_017974 | running | 23.459 | 3.899 | 0.100 | 235.71 | 198 | 0 | 2 |
| acceptance-6-20260914_073620_017974 | running | 21.060 | 3.466 | 0.100 | 189.73 | 267 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2183 | 5718e0c12d03cca51448cb823cb4acbc666747c699ba1dacd8c390b18ac0f202 | 5718e0c12d03cca51448cb823cb4acbc666747c699ba1dacd8c390b18ac0f202 | True |
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
| requested duration covered | PASS | 30.009287572000176 | >= 30.0 s |
| sampling continuity | PASS | 1.0059439989272505 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260914_073620_017974: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_073620_017974: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_073620_017974: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_073620_017974: processed FPS | FAIL | 22.25977535778856 | >= 25.0 |
| acceptance-1-20260914_073620_017974: low detection FPS | FAIL | 3.898792989313266 | >= 4.0 |
| acceptance-1-20260914_073620_017974: high detection FPS | FAIL | 0.09996905100803245 | >= 0.5 |
| acceptance-1-20260914_073620_017974: result age P95 | PASS | 125.130493 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_073620_017974: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_073620_017974: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_073620_017974: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_073620_017974: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_073620_017974: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_073620_017974: processed FPS | FAIL | 18.794181589510103 | >= 25.0 |
| acceptance-2-20260914_073620_017974: low detection FPS | FAIL | 3.0323945472436513 | >= 4.0 |
| acceptance-2-20260914_073620_017974: high detection FPS | FAIL | 0.03332301700267749 | >= 0.5 |
| acceptance-2-20260914_073620_017974: result age P95 | FAIL | 1067.092761 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_073620_017974: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_073620_017974: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_073620_017974: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_073620_017974: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_073620_017974: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_073620_017974: processed FPS | PASS | 26.191891364104503 | >= 25.0 |
| acceptance-3-20260914_073620_017974: low detection FPS | PASS | 4.265346176342718 | >= 4.0 |
| acceptance-3-20260914_073620_017974: high detection FPS | FAIL | 0.13329206801070995 | >= 0.5 |
| acceptance-3-20260914_073620_017974: result age P95 | PASS | 140.492132 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_073620_017974: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_073620_017974: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_073620_017974: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_073620_017974: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_073620_017974: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_073620_017974: processed FPS | FAIL | 23.859280173917078 | >= 25.0 |
| acceptance-4-20260914_073620_017974: low detection FPS | PASS | 4.032085057323975 | >= 4.0 |
| acceptance-4-20260914_073620_017974: high detection FPS | FAIL | 0.06664603400535497 | >= 0.5 |
| acceptance-4-20260914_073620_017974: result age P95 | PASS | 136.014713 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_073620_017974: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_073620_017974: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260914_073620_017974: every sample observed | PASS | 31 | 31 |
| acceptance-5-20260914_073620_017974: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260914_073620_017974: counters never reset | PASS | True | monotonic |
| acceptance-5-20260914_073620_017974: processed FPS | FAIL | 23.45940396988495 | >= 25.0 |
| acceptance-5-20260914_073620_017974: low detection FPS | FAIL | 3.898792989313266 | >= 4.0 |
| acceptance-5-20260914_073620_017974: high detection FPS | FAIL | 0.09996905100803245 | >= 0.5 |
| acceptance-5-20260914_073620_017974: result age P95 | PASS | 235.706617 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260914_073620_017974: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260914_073620_017974: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260914_073620_017974: every sample observed | PASS | 31 | 31 |
| acceptance-6-20260914_073620_017974: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260914_073620_017974: counters never reset | PASS | True | monotonic |
| acceptance-6-20260914_073620_017974: processed FPS | FAIL | 21.06014674569217 | >= 25.0 |
| acceptance-6-20260914_073620_017974: low detection FPS | FAIL | 3.4655937682784583 | >= 4.0 |
| acceptance-6-20260914_073620_017974: high detection FPS | FAIL | 0.09996905100803245 | >= 0.5 |
| acceptance-6-20260914_073620_017974: result age P95 | PASS | 189.728061 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260914_073620_017974: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260914_073620_017974: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 28.244274809160295 | <= 10.0% |
| process CPU average | PASS | 45.725496810734654 | <= 85.0% of host |
| process RSS growth | PASS | -0.6091596029886893 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003777185920626, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_073620_017974': {'decoded_frame_count': 300, 'processed_frame_count': 199, 'low_res_detection_count': 35, 'high_res_detection_count': 1, 'dropped_frame_count': 100, 'decoder_queue_drop_count': 23, 'processor_coalesced_frame_count': 75, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_073620_017974': {'decoded_frame_count': 300, 'processed_frame_count': 207, 'low_res_detection_count': 36, 'high_res_detection_count': 1, 'dropped_frame_count': 90, 'decoder_queue_drop_count': 33, 'processor_coalesced_frame_count': 57, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_073620_017974': {'decoded_frame_count': 300, 'processed_frame_count': 214, 'low_res_detection_count': 36, 'high_res_detection_count': 1, 'dropped_frame_count': 84, 'decoder_queue_drop_count': 22, 'processor_coalesced_frame_count': 62, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_073620_017974': {'decoded_frame_count': 300, 'processed_frame_count': 220, 'low_res_detection_count': 34, 'high_res_detection_count': 0, 'dropped_frame_count': 79, 'decoder_queue_drop_count': 33, 'processor_coalesced_frame_count': 44, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260914_073620_017974': {'decoded_frame_count': 300, 'processed_frame_count': 245, 'low_res_detection_count': 37, 'high_res_detection_count': 2, 'dropped_frame_count': 53, 'decoder_queue_drop_count': 19, 'processor_coalesced_frame_count': 33, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260914_073620_017974': {'decoded_frame_count': 300, 'processed_frame_count': 192, 'low_res_detection_count': 35, 'high_res_detection_count': 0, 'dropped_frame_count': 107, 'decoder_queue_drop_count': 49, 'processor_coalesced_frame_count': 58, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
