# 多路实时监测验收报告

- 开始时间：2026-09-14T00:32:41.568407+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.003/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_003241_568426 | running | 27.581 | 3.933 | 1.167 | 171.24 | 145 | 0 | 2 |
| acceptance-2-20260914_003241_568426 | running | 28.365 | 3.983 | 1.333 | 169.40 | 97 | 0 | 2 |
| acceptance-3-20260914_003241_568426 | running | 29.365 | 4.000 | 1.400 | 93.73 | 38 | 0 | 2 |
| acceptance-4-20260914_003241_568426 | running | 28.881 | 4.000 | 1.400 | 96.13 | 67 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2183 | d97be977942b7776b8ebc09e4c5a9f19d51075630ed06aad436b8d847e88d65e | d97be977942b7776b8ebc09e4c5a9f19d51075630ed06aad436b8d847e88d65e | True |
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
| requested duration covered | PASS | 60.00419881695416 | >= 60.0 s |
| sampling continuity | PASS | 1.0052869990468025 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260914_003241_568426: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260914_003241_568426: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_003241_568426: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_003241_568426: processed FPS | PASS | 27.581403178945212 | >= 25.0 |
| acceptance-1-20260914_003241_568426: low detection FPS | FAIL | 3.9330580968163567 | >= 4.0 |
| acceptance-1-20260914_003241_568426: high detection FPS | PASS | 1.1665850287167159 | >= 0.5 |
| acceptance-1-20260914_003241_568426: result age P95 | PASS | 171.243053 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_003241_568426: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_003241_568426: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_003241_568426: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260914_003241_568426: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_003241_568426: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_003241_568426: processed FPS | PASS | 28.364681698226438 | >= 25.0 |
| acceptance-2-20260914_003241_568426: low detection FPS | FAIL | 3.983054598047073 | >= 4.0 |
| acceptance-2-20260914_003241_568426: high detection FPS | PASS | 1.3332400328191039 | >= 0.5 |
| acceptance-2-20260914_003241_568426: result age P95 | PASS | 169.399832 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_003241_568426: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_003241_568426: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_003241_568426: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260914_003241_568426: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_003241_568426: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_003241_568426: processed FPS | PASS | 29.364611722840763 | >= 25.0 |
| acceptance-3-20260914_003241_568426: low detection FPS | FAIL | 3.999720098457312 | >= 4.0 |
| acceptance-3-20260914_003241_568426: high detection FPS | PASS | 1.3999020344600592 | >= 0.5 |
| acceptance-3-20260914_003241_568426: result age P95 | PASS | 93.734559 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_003241_568426: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_003241_568426: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_003241_568426: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260914_003241_568426: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_003241_568426: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_003241_568426: processed FPS | PASS | 28.88131221094384 | >= 25.0 |
| acceptance-4-20260914_003241_568426: low detection FPS | FAIL | 3.999720098457312 | >= 4.0 |
| acceptance-4-20260914_003241_568426: high detection FPS | PASS | 1.3999020344600592 | >= 0.5 |
| acceptance-4-20260914_003241_568426: result age P95 | PASS | 96.128931 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_003241_568426: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_003241_568426: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 6.072644721906922 | <= 10.0% |
| process CPU average | PASS | 34.820609432511226 | <= 85.0% of host |
| process RSS growth | PASS | 1.3815918911632041 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003093323088251, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_003241_568426': {'decoded_frame_count': 300, 'processed_frame_count': 289, 'low_res_detection_count': 40, 'high_res_detection_count': 11, 'dropped_frame_count': 11, 'decoder_queue_drop_count': 3, 'processor_coalesced_frame_count': 8, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_003241_568426': {'decoded_frame_count': 300, 'processed_frame_count': 293, 'low_res_detection_count': 40, 'high_res_detection_count': 15, 'dropped_frame_count': 7, 'decoder_queue_drop_count': 3, 'processor_coalesced_frame_count': 4, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260914_003241_568426': {'decoded_frame_count': 300, 'processed_frame_count': 290, 'low_res_detection_count': 40, 'high_res_detection_count': 12, 'dropped_frame_count': 10, 'decoder_queue_drop_count': 3, 'processor_coalesced_frame_count': 7, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260914_003241_568426': {'decoded_frame_count': 300, 'processed_frame_count': 292, 'low_res_detection_count': 40, 'high_res_detection_count': 13, 'dropped_frame_count': 8, 'decoder_queue_drop_count': 2, 'processor_coalesced_frame_count': 6, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
