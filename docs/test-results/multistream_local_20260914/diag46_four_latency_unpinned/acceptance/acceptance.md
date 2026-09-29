# 多路实时监测验收报告

- 开始时间：2026-09-14T00:00:46.925869+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_000046_925886 | running | 29.029 | 3.999 | 0.400 | 99.05 | 29 | 0 | 2 |
| acceptance-2-20260914_000046_925886 | running | 29.129 | 3.966 | 0.367 | 97.81 | 25 | 0 | 2 |
| acceptance-3-20260914_000046_925886 | running | 29.729 | 3.999 | 0.433 | 88.74 | 7 | 0 | 2 |
| acceptance-4-20260914_000046_925886 | running | 29.562 | 3.999 | 0.267 | 96.50 | 13 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2182 | 2b3a043ad415b2bae83b738a411349d038059baa89666dc08f3a8bb1a68d742b | 2b3a043ad415b2bae83b738a411349d038059baa89666dc08f3a8bb1a68d742b | True |
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
| requested duration covered | PASS | 30.004275769926608 | >= 30.0 s |
| sampling continuity | PASS | 1.0048585520125926 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260914_000046_925886: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_000046_925886: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_000046_925886: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_000046_925886: processed FPS | PASS | 29.02919592790193 | >= 25.0 |
| acceptance-1-20260914_000046_925886: low detection FPS | FAIL | 3.9994299785857996 | >= 4.0 |
| acceptance-1-20260914_000046_925886: high detection FPS | FAIL | 0.39994299785857995 | >= 0.5 |
| acceptance-1-20260914_000046_925886: result age P95 | PASS | 99.050912 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_000046_925886: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_000046_925886: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_000046_925886: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_000046_925886: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_000046_925886: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_000046_925886: processed FPS | PASS | 29.129181677366574 | >= 25.0 |
| acceptance-2-20260914_000046_925886: low detection FPS | FAIL | 3.966101395430918 | >= 4.0 |
| acceptance-2-20260914_000046_925886: high detection FPS | FAIL | 0.3666144147036983 | >= 0.5 |
| acceptance-2-20260914_000046_925886: result age P95 | PASS | 97.806542 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_000046_925886: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_000046_925886: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_000046_925886: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_000046_925886: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_000046_925886: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_000046_925886: processed FPS | PASS | 29.729096174154446 | >= 25.0 |
| acceptance-3-20260914_000046_925886: low detection FPS | FAIL | 3.9994299785857996 | >= 4.0 |
| acceptance-3-20260914_000046_925886: high detection FPS | FAIL | 0.43327158101346164 | >= 0.5 |
| acceptance-3-20260914_000046_925886: result age P95 | PASS | 88.735136 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_000046_925886: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_000046_925886: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_000046_925886: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_000046_925886: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_000046_925886: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_000046_925886: processed FPS | PASS | 29.562453258380035 | >= 25.0 |
| acceptance-4-20260914_000046_925886: low detection FPS | FAIL | 3.9994299785857996 | >= 4.0 |
| acceptance-4-20260914_000046_925886: high detection FPS | FAIL | 0.2666286652390533 | >= 0.5 |
| acceptance-4-20260914_000046_925886: result age P95 | PASS | 96.501192 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_000046_925886: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_000046_925886: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 2.354260089686098 | <= 10.0% |
| process CPU average | PASS | 24.951391894867463 | <= 85.0% of host |
| process RSS growth | FAIL | 100.61128091136426 | <= 5.0% |
