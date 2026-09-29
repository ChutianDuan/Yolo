# 多路实时监测验收报告

- 开始时间：2026-09-21T11:15:09.643283+00:00
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
| acceptance-1-20260921_111509_643300 | running | 25.249 | 4.766 | 1.000 | 167.78 | 285 | 0 | 2 |
| acceptance-2-20260921_111509_643300 | running | 24.815 | 4.700 | 0.900 | 175.88 | 312 | 0 | 2 |
| acceptance-3-20260921_111509_643300 | running | 27.448 | 4.933 | 0.983 | 167.84 | 153 | 0 | 2 |
| acceptance-4-20260921_111509_643300 | running | 26.998 | 4.883 | 1.067 | 128.44 | 179 | 0 | 2 |
| acceptance-5-20260921_111509_643300 | running | 25.099 | 4.850 | 0.967 | 187.30 | 295 | 0 | 2 |
| acceptance-6-20260921_111509_643300 | running | 24.449 | 4.600 | 0.883 | 184.66 | 333 | 0 | 2 |

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
| requested duration covered | PASS | 60.003343037999 | >= 60.0 s |
| sampling continuity | PASS | 1.0086124840017874 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260921_111509_643300: every sample observed | PASS | 61 | 61 |
| acceptance-1-20260921_111509_643300: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260921_111509_643300: counters never reset | PASS | True | monotonic |
| acceptance-1-20260921_111509_643300: processed FPS | PASS | 25.248593216557595 | >= 25.0 |
| acceptance-1-20260921_111509_643300: low detection FPS | PASS | 4.7664010956669784 | >= 4.0 |
| acceptance-1-20260921_111509_643300: high detection FPS | PASS | 0.9999442858042612 | >= 0.5 |
| acceptance-1-20260921_111509_643300: result age P95 | PASS | 167.78447 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260921_111509_643300: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260921_111509_643300: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260921_111509_643300: every sample observed | PASS | 61 | 61 |
| acceptance-2-20260921_111509_643300: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260921_111509_643300: counters never reset | PASS | True | monotonic |
| acceptance-2-20260921_111509_643300: processed FPS | FAIL | 24.815284026042416 | >= 25.0 |
| acceptance-2-20260921_111509_643300: low detection FPS | PASS | 4.699738143280028 | >= 4.0 |
| acceptance-2-20260921_111509_643300: high detection FPS | PASS | 0.899949857223835 | >= 0.5 |
| acceptance-2-20260921_111509_643300: result age P95 | PASS | 175.88224 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260921_111509_643300: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260921_111509_643300: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260921_111509_643300: every sample observed | PASS | 61 | 61 |
| acceptance-3-20260921_111509_643300: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260921_111509_643300: counters never reset | PASS | True | monotonic |
| acceptance-3-20260921_111509_643300: processed FPS | PASS | 27.44847064532697 | >= 25.0 |
| acceptance-3-20260921_111509_643300: low detection FPS | PASS | 4.933058476634355 | >= 4.0 |
| acceptance-3-20260921_111509_643300: high detection FPS | PASS | 0.9832785477075235 | >= 0.5 |
| acceptance-3-20260921_111509_643300: result age P95 | PASS | 167.837572 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260921_111509_643300: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260921_111509_643300: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260921_111509_643300: every sample observed | PASS | 61 | 61 |
| acceptance-4-20260921_111509_643300: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260921_111509_643300: counters never reset | PASS | True | monotonic |
| acceptance-4-20260921_111509_643300: processed FPS | PASS | 26.998495716715052 | >= 25.0 |
| acceptance-4-20260921_111509_643300: low detection FPS | PASS | 4.8830612623441425 | >= 4.0 |
| acceptance-4-20260921_111509_643300: high detection FPS | PASS | 1.066607238191212 | >= 0.5 |
| acceptance-4-20260921_111509_643300: result age P95 | PASS | 128.440006 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260921_111509_643300: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260921_111509_643300: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260921_111509_643300: every sample observed | PASS | 61 | 61 |
| acceptance-5-20260921_111509_643300: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260921_111509_643300: counters never reset | PASS | True | monotonic |
| acceptance-5-20260921_111509_643300: processed FPS | PASS | 25.098601573686956 | >= 25.0 |
| acceptance-5-20260921_111509_643300: low detection FPS | PASS | 4.849729786150667 | >= 4.0 |
| acceptance-5-20260921_111509_643300: high detection FPS | PASS | 0.9666128096107858 | >= 0.5 |
| acceptance-5-20260921_111509_643300: result age P95 | PASS | 187.298142 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260921_111509_643300: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260921_111509_643300: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260921_111509_643300: every sample observed | PASS | 61 | 61 |
| acceptance-6-20260921_111509_643300: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260921_111509_643300: counters never reset | PASS | True | monotonic |
| acceptance-6-20260921_111509_643300: processed FPS | FAIL | 24.448637787914187 | >= 25.0 |
| acceptance-6-20260921_111509_643300: low detection FPS | PASS | 4.599743714699602 | >= 4.0 |
| acceptance-6-20260921_111509_643300: high detection FPS | PASS | 0.8832841191270974 | >= 0.5 |
| acceptance-6-20260921_111509_643300: result age P95 | PASS | 184.660463 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260921_111509_643300: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260921_111509_643300: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | FAIL | 10.928961748633881 | <= 10.0% |
| process CPU average | PASS | 61.041599831906076 | <= 85.0% of host |
| process RSS growth | PASS | -0.42206528387921927 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.00200836500153, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260921_111509_643300': {'decoded_frame_count': 300, 'processed_frame_count': 282, 'low_res_detection_count': 50, 'high_res_detection_count': 11, 'dropped_frame_count': 17, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 12, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260921_111509_643300': {'decoded_frame_count': 300, 'processed_frame_count': 253, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 46, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 37, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260921_111509_643300': {'decoded_frame_count': 300, 'processed_frame_count': 282, 'low_res_detection_count': 50, 'high_res_detection_count': 11, 'dropped_frame_count': 18, 'decoder_queue_drop_count': 3, 'processor_coalesced_frame_count': 15, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260921_111509_643300': {'decoded_frame_count': 300, 'processed_frame_count': 265, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 35, 'decoder_queue_drop_count': 9, 'processor_coalesced_frame_count': 26, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260921_111509_643300': {'decoded_frame_count': 300, 'processed_frame_count': 273, 'low_res_detection_count': 45, 'high_res_detection_count': 9, 'dropped_frame_count': 26, 'decoder_queue_drop_count': 15, 'processor_coalesced_frame_count': 9, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260921_111509_643300': {'decoded_frame_count': 300, 'processed_frame_count': 273, 'low_res_detection_count': 49, 'high_res_detection_count': 10, 'dropped_frame_count': 26, 'decoder_queue_drop_count': 3, 'processor_coalesced_frame_count': 23, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
