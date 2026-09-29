# 多路实时监测验收报告

- 开始时间：2026-09-13T02:26:03.074276+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260913_022603_074292 | running | 28.730 | 3.566 | 0.367 | 110.90 | 38 | 0 | 2 |
| acceptance-2-20260913_022603_074292 | running | 29.297 | 3.066 | 0.500 | 90.25 | 20 | 0 | 2 |
| acceptance-3-20260913_022603_074292 | running | 29.497 | 3.266 | 0.133 | 90.47 | 15 | 0 | 2 |
| acceptance-4-20260913_022603_074292 | running | 29.630 | 3.333 | 0.200 | 92.98 | 11 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2154 | 6b42dc11154b229fee90d2949ceace5735d8c77cd7afa28b1221a59fed566d11 | 6b42dc11154b229fee90d2949ceace5735d8c77cd7afa28b1221a59fed566d11 | True |
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
| requested duration covered | PASS | 30.003438133047894 | >= 30.0 s |
| sampling continuity | PASS | 1.0051073050126433 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260913_022603_074292: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260913_022603_074292: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260913_022603_074292: counters never reset | PASS | True | monotonic |
| acceptance-1-20260913_022603_074292: processed FPS | PASS | 28.730040743248445 | >= 25.0 |
| acceptance-1-20260913_022603_074292: low detection FPS | FAIL | 3.5662579576886118 | >= 4.0 |
| acceptance-1-20260913_022603_074292: high detection FPS | FAIL | 0.3666246498558386 | >= 0.5 |
| acceptance-1-20260913_022603_074292: result age P95 | PASS | 110.897299 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260913_022603_074292: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260913_022603_074292: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260913_022603_074292: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260913_022603_074292: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260913_022603_074292: counters never reset | PASS | True | monotonic |
| acceptance-2-20260913_022603_074292: processed FPS | PASS | 29.296642474843832 | >= 25.0 |
| acceptance-2-20260913_022603_074292: low detection FPS | FAIL | 3.066315253339741 | >= 4.0 |
| acceptance-2-20260913_022603_074292: high detection FPS | FAIL | 0.49994270434887084 | >= 0.5 |
| acceptance-2-20260913_022603_074292: result age P95 | PASS | 90.250201 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260913_022603_074292: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260913_022603_074292: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260913_022603_074292: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260913_022603_074292: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260913_022603_074292: counters never reset | PASS | True | monotonic |
| acceptance-3-20260913_022603_074292: processed FPS | PASS | 29.49661955658338 | >= 25.0 |
| acceptance-3-20260913_022603_074292: low detection FPS | FAIL | 3.2662923350792896 | >= 4.0 |
| acceptance-3-20260913_022603_074292: high detection FPS | FAIL | 0.13331805449303222 | >= 0.5 |
| acceptance-3-20260913_022603_074292: result age P95 | PASS | 90.472515 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260913_022603_074292: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260913_022603_074292: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260913_022603_074292: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260913_022603_074292: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260913_022603_074292: counters never reset | PASS | True | monotonic |
| acceptance-4-20260913_022603_074292: processed FPS | PASS | 29.62993761107641 | >= 25.0 |
| acceptance-4-20260913_022603_074292: low detection FPS | FAIL | 3.3329513623258054 | >= 4.0 |
| acceptance-4-20260913_022603_074292: high detection FPS | FAIL | 0.19997708173954834 | >= 0.5 |
| acceptance-4-20260913_022603_074292: result age P95 | PASS | 92.976912 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260913_022603_074292: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260913_022603_074292: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 3.0371203599550007 | <= 10.0% |
| process CPU average | PASS | 18.14472132199064 | <= 85.0% of host |
| process RSS growth | FAIL | 102.01372169725984 | <= 5.0% |
