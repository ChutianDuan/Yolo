# 多路实时监测验收报告

- 开始时间：2026-09-14T02:07:43.517311+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：2
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：PASS
- 同一批流采样前预热：预热 10.003/10.000 s，观察 11 次，完成 True；不计入采样时长/资源基线。
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_020743_517334 | running | 28.529 | 4.999 | 1.300 | 99.55 | 44 | 0 | 2 |
| acceptance-2-20260914_020743_517334 | running | 29.129 | 5.033 | 1.300 | 108.09 | 27 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2183 | f44101dfca320f48da58b98bb05fdd94b0030b0b621fd97760b8c38d68b9a4d5 | f44101dfca320f48da58b98bb05fdd94b0030b0b621fd97760b8c38d68b9a4d5 | True |
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
| requested duration covered | PASS | 30.00441282708198 | >= 30.0 s |
| sampling continuity | PASS | 1.0095181160140783 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 2/2 | 2/2 |
| acceptance-1-20260914_020743_517334: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_020743_517334: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_020743_517334: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_020743_517334: processed FPS | PASS | 28.529136861741033 | >= 25.0 |
| acceptance-1-20260914_020743_517334: low detection FPS | PASS | 4.999264636987331 | >= 4.0 |
| acceptance-1-20260914_020743_517334: high detection FPS | PASS | 1.299808805616706 | >= 0.5 |
| acceptance-1-20260914_020743_517334: result age P95 | PASS | 99.546981 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_020743_517334: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_020743_517334: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_020743_517334: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_020743_517334: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_020743_517334: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_020743_517334: processed FPS | PASS | 29.129048618179514 | >= 25.0 |
| acceptance-2-20260914_020743_517334: low detection FPS | PASS | 5.03259306790058 | >= 4.0 |
| acceptance-2-20260914_020743_517334: high detection FPS | PASS | 1.299808805616706 | >= 0.5 |
| acceptance-2-20260914_020743_517334: result age P95 | PASS | 108.087673 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_020743_517334: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_020743_517334: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 2.059496567505723 | <= 10.0% |
| process CPU average | PASS | 17.582179260063686 | <= 85.0% of host |
| process RSS growth | PASS | 0.01304429352921514 | <= 5.0% |
| sampling warmup completed | PASS | {'requested_seconds': 10.0, 'elapsed_seconds': 10.003122911090031, 'observation_count': 11, 'completed': True, 'final_counters': {'acceptance-1-20260914_020743_517334': {'decoded_frame_count': 300, 'processed_frame_count': 291, 'low_res_detection_count': 50, 'high_res_detection_count': 11, 'dropped_frame_count': 9, 'decoder_queue_drop_count': 4, 'processor_coalesced_frame_count': 5, 'inference_error_count': 0, 'reconnect_count': 0}, 'acceptance-2-20260914_020743_517334': {'decoded_frame_count': 300, 'processed_frame_count': 293, 'low_res_detection_count': 49, 'high_res_detection_count': 14, 'dropped_frame_count': 6, 'decoder_queue_drop_count': 1, 'processor_coalesced_frame_count': 5, 'inference_error_count': 0, 'reconnect_count': 0}}, 'errors': []} | >= 10.0 s healthy same-stream warmup |
