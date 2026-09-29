# 多路实时监测验收报告

- 开始时间：2026-09-22T11:54:56.411212+00:00
- 实际采样时长：60.0 s
- 要求采样时长：60.0 s
- 请求流数：6
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：PASS
- 同一批流采样前预热：预热 10.007/10.000 s，观察 3 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260922_115456_411227 | running | 27.513 | 4.816 | 1.067 | 113.26 | 149 | 0 | 2 |
| acceptance-2-20260922_115456_411227 | running | 27.380 | 4.916 | 1.100 | 106.49 | 156 | 0 | 2 |
| acceptance-3-20260922_115456_411227 | running | 29.313 | 4.999 | 1.250 | 108.72 | 41 | 0 | 2 |
| acceptance-4-20260922_115456_411227 | running | 29.079 | 4.983 | 1.250 | 104.64 | 55 | 0 | 2 |
| acceptance-5-20260922_115456_411227 | running | 27.713 | 4.916 | 1.067 | 109.40 | 137 | 0 | 2 |
| acceptance-6-20260922_115456_411227 | running | 27.730 | 4.916 | 1.133 | 168.25 | 137 | 0 | 2 |

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
| valid sampling timeline | PASS | 13 | >= 2 ordered samples |
| requested duration covered | PASS | 60.00794808199862 | >= 60.0 s |
| sampling continuity | PASS | 5.00921889799065 | maximum gap <= 10.0 s |
| all requested streams observed | PASS | 6/6 | 6/6 |
| acceptance-1-20260922_115456_411227: every sample observed | PASS | 13 | 13 |
| acceptance-1-20260922_115456_411227: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260922_115456_411227: counters never reset | PASS | True | monotonic |
| acceptance-1-20260922_115456_411227: processed FPS | PASS | 27.513022070742533 | >= 25.0 |
| acceptance-1-20260922_115456_411227: low detection FPS | PASS | 4.816028696816833 | >= 4.0 |
| acceptance-1-20260922_115456_411227: high detection FPS | PASS | 1.0665253861462882 | >= 0.5 |
| acceptance-1-20260922_115456_411227: result age P95 | PASS | 113.263289 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260922_115456_411227: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260922_115456_411227: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260922_115456_411227: every sample observed | PASS | 13 | 13 |
| acceptance-2-20260922_115456_411227: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260922_115456_411227: counters never reset | PASS | True | monotonic |
| acceptance-2-20260922_115456_411227: processed FPS | PASS | 27.379706397474244 | >= 25.0 |
| acceptance-2-20260922_115456_411227: low detection FPS | PASS | 4.916015451768048 | >= 4.0 |
| acceptance-2-20260922_115456_411227: high detection FPS | PASS | 1.0998543044633597 | >= 0.5 |
| acceptance-2-20260922_115456_411227: result age P95 | PASS | 106.492308 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260922_115456_411227: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260922_115456_411227: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260922_115456_411227: every sample observed | PASS | 13 | 13 |
| acceptance-3-20260922_115456_411227: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260922_115456_411227: counters never reset | PASS | True | monotonic |
| acceptance-3-20260922_115456_411227: processed FPS | PASS | 29.312783659864394 | >= 25.0 |
| acceptance-3-20260922_115456_411227: low detection FPS | PASS | 4.999337747560727 | >= 4.0 |
| acceptance-3-20260922_115456_411227: high detection FPS | PASS | 1.2498344368901817 | >= 0.5 |
| acceptance-3-20260922_115456_411227: result age P95 | PASS | 108.719393 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260922_115456_411227: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260922_115456_411227: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260922_115456_411227: every sample observed | PASS | 13 | 13 |
| acceptance-4-20260922_115456_411227: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260922_115456_411227: counters never reset | PASS | True | monotonic |
| acceptance-4-20260922_115456_411227: processed FPS | PASS | 29.07948123164489 | >= 25.0 |
| acceptance-4-20260922_115456_411227: low detection FPS | PASS | 4.982673288402191 | >= 4.0 |
| acceptance-4-20260922_115456_411227: high detection FPS | PASS | 1.2498344368901817 | >= 0.5 |
| acceptance-4-20260922_115456_411227: result age P95 | PASS | 104.643446 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260922_115456_411227: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260922_115456_411227: queue bound | PASS | 2 | <= 2 |
| acceptance-5-20260922_115456_411227: every sample observed | PASS | 13 | 13 |
| acceptance-5-20260922_115456_411227: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-5-20260922_115456_411227: counters never reset | PASS | True | monotonic |
| acceptance-5-20260922_115456_411227: processed FPS | PASS | 27.71299558064496 | >= 25.0 |
| acceptance-5-20260922_115456_411227: low detection FPS | PASS | 4.916015451768048 | >= 4.0 |
| acceptance-5-20260922_115456_411227: high detection FPS | PASS | 1.0665253861462882 | >= 0.5 |
| acceptance-5-20260922_115456_411227: result age P95 | PASS | 109.400614 | <= 300.0 ms (local decode to query) |
| acceptance-5-20260922_115456_411227: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-5-20260922_115456_411227: queue bound | PASS | 2 | <= 2 |
| acceptance-6-20260922_115456_411227: every sample observed | PASS | 13 | 13 |
| acceptance-6-20260922_115456_411227: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-6-20260922_115456_411227: counters never reset | PASS | True | monotonic |
| acceptance-6-20260922_115456_411227: processed FPS | PASS | 27.729660039803495 | >= 25.0 |
| acceptance-6-20260922_115456_411227: low detection FPS | PASS | 4.916015451768048 | >= 4.0 |
| acceptance-6-20260922_115456_411227: high detection FPS | PASS | 1.1331832227804313 | >= 0.5 |
| acceptance-6-20260922_115456_411227: result age P95 | PASS | 168.254672 | <= 300.0 ms (local decode to query) |
| acceptance-6-20260922_115456_411227: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-6-20260922_115456_411227: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 6.59465605457647 | <= 10.0% |
| process CPU average | PASS | 57.66702664824829 | <= 85.0% of host |
| process RSS growth | PASS | 1.765070454277709 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.006504980992759, 'observation_count': 3, 'completed': True, 'final_counters': {'acceptance-1-20260922_115456_411227': {'decoded_frame_count': 301, 'processed_frame_count': 282, 'low_res_detection_count': 49, 'high_res_detection_count': 11, 'dropped_frame_count': 18, 'decoder_queue_drop_count': 8, 'processor_coalesced_frame_count': 10, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260922_115456_411227': {'decoded_frame_count': 300, 'processed_frame_count': 288, 'low_res_detection_count': 48, 'high_res_detection_count': 13, 'dropped_frame_count': 12, 'decoder_queue_drop_count': 4, 'processor_coalesced_frame_count': 8, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-3-20260922_115456_411227': {'decoded_frame_count': 300, 'processed_frame_count': 295, 'low_res_detection_count': 50, 'high_res_detection_count': 12, 'dropped_frame_count': 5, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 4, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-4-20260922_115456_411227': {'decoded_frame_count': 300, 'processed_frame_count': 288, 'low_res_detection_count': 50, 'high_res_detection_count': 11, 'dropped_frame_count': 12, 'decoder_queue_drop_count': 4, 'processor_coalesced_frame_count': 8, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-5-20260922_115456_411227': {'decoded_frame_count': 300, 'processed_frame_count': 280, 'low_res_detection_count': 49, 'high_res_detection_count': 12, 'dropped_frame_count': 20, 'decoder_queue_drop_count': 5, 'processor_coalesced_frame_count': 15, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-6-20260922_115456_411227': {'decoded_frame_count': 300, 'processed_frame_count': 283, 'low_res_detection_count': 49, 'high_res_detection_count': 11, 'dropped_frame_count': 16, 'decoder_queue_drop_count': 4, 'processor_coalesced_frame_count': 12, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
