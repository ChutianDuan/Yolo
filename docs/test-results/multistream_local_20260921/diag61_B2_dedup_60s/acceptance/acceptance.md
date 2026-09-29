# 多路实时监测验收报告

- 开始时间：2026-09-21T23:06:01.988153+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 同一批流采样前预热：预热 10.003/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260921_230601_988168 | running | 26.765 | 4.850 | 1.033 | 154.91 | 194 | 0 | 2 |
| acceptance-2-20260921_230601_988168 | running | 24.365 | 4.850 | 0.917 | 222.10 | 338 | 0 | 2 |
| acceptance-3-20260921_230601_988168 | running | 28.365 | 5.016 | 1.150 | 107.40 | 99 | 0 | 2 |
| acceptance-4-20260921_230601_988168 | running | 28.015 | 4.983 | 1.150 | 106.66 | 119 | 0 | 2 |
| acceptance-5-20260921_230601_988168 | running | 25.665 | 4.816 | 1.033 | 182.46 | 261 | 0 | 2 |
| acceptance-6-20260921_230601_988168 | running | 26.849 | 4.883 | 1.033 | 146.90 | 189 | 0 | 2 |

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
| requested duration covered | PASS | 60.003340962008224 | >= 60.0 s |
| sampling continuity | PASS | 1.0059936850011582 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_230601_988168: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_230601_988168: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_230601_988168: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_230601_988168: processed FPS | PASS | 26.765176309380116 | >= 25.0 |
| acceptance-1-20260921_230601_988168: low detection FPS | PASS | 4.849729953941229 | >= 4.0 |
| acceptance-1-20260921_230601_988168: high detection FPS | PASS | 1.0332757977469285 | >= 0.5 |
| acceptance-1-20260921_230601_988168: result age P95 | PASS | 154.910559 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_230601_988168: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_230601_988168: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_230601_988168: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_230601_988168: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_230601_988168: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_230601_988168: processed FPS | FAIL | 24.365309940419507 | >= 25.0 |
| acceptance-2-20260921_230601_988168: low detection FPS | PASS | 4.849729953941229 | >= 4.0 |
| acceptance-2-20260921_230601_988168: high detection FPS | PASS | 0.9166156270335656 | >= 0.5 |
| acceptance-2-20260921_230601_988168: result age P95 | PASS | 222.104096 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_230601_988168: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_230601_988168: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_230601_988168: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_230601_988168: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_230601_988168: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_230601_988168: processed FPS | PASS | 28.36508722202052 | >= 25.0 |
| acceptance-3-20260921_230601_988168: low detection FPS | PASS | 5.016387340674605 | >= 4.0 |
| acceptance-3-20260921_230601_988168: high detection FPS | PASS | 1.1499359684602914 | >= 0.5 |
| acceptance-3-20260921_230601_988168: result age P95 | PASS | 107.400234 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_230601_988168: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_230601_988168: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_230601_988168: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_230601_988168: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_230601_988168: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_230601_988168: processed FPS | PASS | 28.01510670988043 | >= 25.0 |
| acceptance-4-20260921_230601_988168: low detection FPS | PASS | 4.983055863327929 | >= 4.0 |
| acceptance-4-20260921_230601_988168: high detection FPS | PASS | 1.1499359684602914 | >= 0.5 |
| acceptance-4-20260921_230601_988168: result age P95 | PASS | 106.663847 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_230601_988168: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_230601_988168: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_230601_988168: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_230601_988168: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_230601_988168: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_230601_988168: processed FPS | PASS | 25.665237556939836 | >= 25.0 |
| acceptance-5-20260921_230601_988168: low detection FPS | PASS | 4.816398476594554 | >= 4.0 |
| acceptance-5-20260921_230601_988168: high detection FPS | PASS | 1.0332757977469285 | >= 0.5 |
| acceptance-5-20260921_230601_988168: result age P95 | PASS | 182.462954 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_230601_988168: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_230601_988168: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_230601_988168: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_230601_988168: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_230601_988168: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_230601_988168: processed FPS | PASS | 26.848505002746805 | >= 25.0 |
| acceptance-6-20260921_230601_988168: low detection FPS | PASS | 4.883061431287904 | >= 4.0 |
| acceptance-6-20260921_230601_988168: high detection FPS | PASS | 1.0332757977469285 | >= 0.5 |
| acceptance-6-20260921_230601_988168: result age P95 | PASS | 146.900679 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_230601_988168: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_230601_988168: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 14.101057579318446 | <= 10.0% |
| process CPU average | PASS | 60.53258869998637 | <= 85.0% of host |
| process RSS growth | PASS | -7.252307935088504 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003426622992265, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_230601_988168': {'decoded_frame_count': 301, 'processed_frame_count': 256, 'low_res_detection_count': 48, 'high_res_detection_count': 10, 'dropped_frame_count': 44, 'decoder_queue_drop_count': 11, 'processor_coalesced_frame_count': 33, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_230601_988168': {'decoded_frame_count': 300, 'processed_frame_count': 259, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 40, 'decoder_queue_drop_count': 10, 'processor_coalesced_frame_count': 30, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_230601_988168': {'decoded_frame_count': 300, 'processed_frame_count': 257, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 42, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 37, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_230601_988168': {'decoded_frame_count': 300, 'processed_frame_count': 282, 'low_res_detection_count': 49, 'high_res_detection_count': 9, 'dropped_frame_count': 18, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 13, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_230601_988168': {'decoded_frame_count': 300, 'processed_frame_count': 264, 'low_res_detection_count': 47, 'high_res_detection_count': 10, 'dropped_frame_count': 35, 'decoder_queue_drop_count': 11, 'processor_coalesced_frame_count': 23, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_230601_988168': {'decoded_frame_count': 300, 'processed_frame_count': 270, 'low_res_detection_count': 50, 'high_res_detection_count': 10, 'dropped_frame_count': 29, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 20, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
