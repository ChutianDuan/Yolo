# 多路实时监测验收报告

- 开始时间：2026-09-21T11:13:15.759661+00:00
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
| acceptance-1-20260921_111315_759676 | running | 26.948 | 4.950 | 1.100 | 155.09 | 183 | 0 | 2 |
| acceptance-2-20260921_111315_759676 | running | 27.215 | 4.866 | 1.100 | 177.22 | 166 | 0 | 2 |
| acceptance-3-20260921_111315_759676 | running | 28.748 | 4.983 | 1.167 | 125.85 | 75 | 0 | 2 |
| acceptance-4-20260921_111315_759676 | running | 28.515 | 4.966 | 1.283 | 99.39 | 89 | 0 | 2 |
| acceptance-5-20260921_111315_759676 | running | 24.682 | 4.733 | 1.033 | 191.81 | 320 | 0 | 2 |
| acceptance-6-20260921_111315_759676 | running | 26.282 | 4.816 | 1.017 | 180.84 | 221 | 0 | 2 |

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
| requested duration covered | PASS | 60.00397103300202 | >= 60.0 s |
| sampling continuity | PASS | 1.0057787170007941 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_111315_759676: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_111315_759676: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_111315_759676: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_111315_759676: processed FPS | PASS | 26.948216462384707 | >= 25.0 |
| acceptance-1-20260921_111315_759676: low detection FPS | PASS | 4.949672411458415 | >= 4.0 |
| acceptance-1-20260921_111315_759676: high detection FPS | PASS | 1.0999272025463145 | >= 0.5 |
| acceptance-1-20260921_111315_759676: result age P95 | PASS | 155.094778 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_111315_759676: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_111315_759676: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_111315_759676: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_111315_759676: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_111315_759676: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_111315_759676: processed FPS | PASS | 27.214865481183814 | >= 25.0 |
| acceptance-2-20260921_111315_759676: low detection FPS | PASS | 4.866344593083695 | >= 4.0 |
| acceptance-2-20260921_111315_759676: high detection FPS | PASS | 1.0999272025463145 | >= 0.5 |
| acceptance-2-20260921_111315_759676: result age P95 | PASS | 177.219619 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_111315_759676: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_111315_759676: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_111315_759676: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_111315_759676: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_111315_759676: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_111315_759676: processed FPS | PASS | 28.748097339278676 | >= 25.0 |
| acceptance-3-20260921_111315_759676: low detection FPS | PASS | 4.983003538808304 | >= 4.0 |
| acceptance-3-20260921_111315_759676: high detection FPS | PASS | 1.166589457246091 | >= 0.5 |
| acceptance-3-20260921_111315_759676: result age P95 | PASS | 125.853891 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_111315_759676: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_111315_759676: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_111315_759676: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_111315_759676: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_111315_759676: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_111315_759676: processed FPS | PASS | 28.514779447829458 | >= 25.0 |
| acceptance-4-20260921_111315_759676: low detection FPS | PASS | 4.966337975133359 | >= 4.0 |
| acceptance-4-20260921_111315_759676: high detection FPS | PASS | 1.2832484029707003 | >= 0.5 |
| acceptance-4-20260921_111315_759676: result age P95 | PASS | 99.391042 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_111315_759676: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_111315_759676: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_111315_759676: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_111315_759676: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_111315_759676: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_111315_759676: processed FPS | FAIL | 24.681699802592302 | >= 25.0 |
| acceptance-5-20260921_111315_759676: low detection FPS | PASS | 4.733020083684141 | >= 4.0 |
| acceptance-5-20260921_111315_759676: high detection FPS | PASS | 1.0332649478465379 | >= 0.5 |
| acceptance-5-20260921_111315_759676: result age P95 | PASS | 191.805041 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_111315_759676: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_111315_759676: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_111315_759676: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_111315_759676: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_111315_759676: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_111315_759676: processed FPS | PASS | 26.28159391538694 | >= 25.0 |
| acceptance-6-20260921_111315_759676: low detection FPS | PASS | 4.816347902058862 | >= 4.0 |
| acceptance-6-20260921_111315_759676: high detection FPS | PASS | 1.0165993841715937 | >= 0.5 |
| acceptance-6-20260921_111315_759676: result age P95 | PASS | 180.839007 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_111315_759676: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_111315_759676: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 14.14492753623188 | <= 10.0% |
| process CPU average | PASS | 58.990522538224695 | <= 85.0% of host |
| process RSS growth | PASS | 4.362530940946993 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00310939499468, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_111315_759676': {'decoded_frame_count': 300, 'processed_frame_count': 281, 'low_res_detection_count': 48, 'high_res_detection_count': 10, 'dropped_frame_count': 19, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 10, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_111315_759676': {'decoded_frame_count': 300, 'processed_frame_count': 285, 'low_res_detection_count': 49, 'high_res_detection_count': 12, 'dropped_frame_count': 14, 'decoder_queue_drop_count': 2, 'processor_coalesced_frame_count': 12, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_111315_759676': {'decoded_frame_count': 300, 'processed_frame_count': 296, 'low_res_detection_count': 50, 'high_res_detection_count': 11, 'dropped_frame_count': 3, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 2, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_111315_759676': {'decoded_frame_count': 300, 'processed_frame_count': 287, 'low_res_detection_count': 49, 'high_res_detection_count': 12, 'dropped_frame_count': 13, 'decoder_queue_drop_count': 3, 'processor_coalesced_frame_count': 10, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_111315_759676': {'decoded_frame_count': 300, 'processed_frame_count': 263, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 36, 'decoder_queue_drop_count': 6, 'processor_coalesced_frame_count': 30, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_111315_759676': {'decoded_frame_count': 300, 'processed_frame_count': 269, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 31, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 22, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
