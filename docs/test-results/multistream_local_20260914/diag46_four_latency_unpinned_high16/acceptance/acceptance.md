# 多路实时监测验收报告

- 开始时间：2026-09-14T00:01:54.984005+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_000154_984021 | running | 28.730 | 3.933 | 0.767 | 99.39 | 37 | 0 | 2 |
| acceptance-2-20260914_000154_984021 | running | 28.530 | 3.900 | 0.633 | 97.53 | 44 | 0 | 2 |
| acceptance-3-20260914_000154_984021 | running | 29.763 | 3.933 | 0.333 | 92.22 | 7 | 0 | 2 |
| acceptance-4-20260914_000154_984021 | running | 29.563 | 4.000 | 0.533 | 96.34 | 13 | 0 | 2 |

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
| valid sampling timeline | PASS | 31 | >= 2 ordered samples |
| requested duration covered | PASS | 30.003572979941964 | >= 30.0 s |
| sampling continuity | PASS | 1.0054726130329072 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260914_000154_984021: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_000154_984021: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_000154_984021: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_000154_984021: processed FPS | PASS | 28.729911620068236 | >= 25.0 |
| acceptance-1-20260914_000154_984021: low detection FPS | FAIL | 3.93286493174948 | >= 4.0 |
| acceptance-1-20260914_000154_984021: high detection FPS | PASS | 0.7665753680528647 | >= 0.5 |
| acceptance-1-20260914_000154_984021: result age P95 | PASS | 99.3869 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_000154_984021: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_000154_984021: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_000154_984021: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_000154_984021: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_000154_984021: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_000154_984021: processed FPS | PASS | 28.529935437097922 | >= 25.0 |
| acceptance-2-20260914_000154_984021: low detection FPS | FAIL | 3.899535567921095 | >= 4.0 |
| acceptance-2-20260914_000154_984021: high detection FPS | PASS | 0.6332579127393231 | >= 0.5 |
| acceptance-2-20260914_000154_984021: result age P95 | PASS | 97.531139 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_000154_984021: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_000154_984021: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_000154_984021: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_000154_984021: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_000154_984021: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_000154_984021: processed FPS | PASS | 29.763121898748185 | >= 25.0 |
| acceptance-3-20260914_000154_984021: low detection FPS | FAIL | 3.93286493174948 | >= 4.0 |
| acceptance-3-20260914_000154_984021: high detection FPS | FAIL | 0.3332936382838543 | >= 0.5 |
| acceptance-3-20260914_000154_984021: result age P95 | PASS | 92.220353 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_000154_984021: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_000154_984021: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_000154_984021: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_000154_984021: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_000154_984021: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_000154_984021: processed FPS | PASS | 29.56314571577787 | >= 25.0 |
| acceptance-4-20260914_000154_984021: low detection FPS | FAIL | 3.999523659406251 | >= 4.0 |
| acceptance-4-20260914_000154_984021: high detection FPS | PASS | 0.5332698212541668 | >= 0.5 |
| acceptance-4-20260914_000154_984021: result age P95 | PASS | 96.339423 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_000154_984021: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_000154_984021: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 4.143337066069436 | <= 10.0% |
| process CPU average | PASS | 30.4247283635314 | <= 85.0% of host |
| process RSS growth | FAIL | 102.1158082886478 | <= 5.0% |
