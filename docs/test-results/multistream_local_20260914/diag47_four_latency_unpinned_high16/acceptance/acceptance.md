# 多路实时监测验收报告

- 开始时间：2026-09-14T00:18:45.705588+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260914_001845_705603 | running | 28.497 | 4.000 | 1.233 | 91.26 | 44 | 0 | 2 |
| acceptance-2-20260914_001845_705603 | running | 28.764 | 3.966 | 1.367 | 97.31 | 36 | 0 | 2 |
| acceptance-3-20260914_001845_705603 | running | 29.497 | 4.000 | 1.433 | 92.70 | 15 | 0 | 2 |
| acceptance-4-20260914_001845_705603 | running | 29.464 | 4.000 | 1.300 | 91.90 | 16 | 0 | 2 |

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
| requested duration covered | PASS | 30.00294949603267 | >= 30.0 s |
| sampling continuity | PASS | 1.006604133057408 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260914_001845_705603: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260914_001845_705603: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260914_001845_705603: counters never reset | PASS | True | monotonic |
| acceptance-1-20260914_001845_705603: processed FPS | PASS | 28.4971982542269 | >= 25.0 |
| acceptance-1-20260914_001845_705603: low detection FPS | FAIL | 3.9996067725230735 | >= 4.0 |
| acceptance-1-20260914_001845_705603: high detection FPS | PASS | 1.2332120881946143 | >= 0.5 |
| acceptance-1-20260914_001845_705603: result age P95 | PASS | 91.260672 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260914_001845_705603: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260914_001845_705603: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260914_001845_705603: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260914_001845_705603: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260914_001845_705603: counters never reset | PASS | True | monotonic |
| acceptance-2-20260914_001845_705603: processed FPS | PASS | 28.763838705728435 | >= 25.0 |
| acceptance-2-20260914_001845_705603: low detection FPS | FAIL | 3.9662767160853813 | >= 4.0 |
| acceptance-2-20260914_001845_705603: high detection FPS | PASS | 1.3665323139453833 | >= 0.5 |
| acceptance-2-20260914_001845_705603: result age P95 | PASS | 97.31213 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260914_001845_705603: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260914_001845_705603: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260914_001845_705603: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260914_001845_705603: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260914_001845_705603: counters never reset | PASS | True | monotonic |
| acceptance-3-20260914_001845_705603: processed FPS | PASS | 29.497099947357665 | >= 25.0 |
| acceptance-3-20260914_001845_705603: low detection FPS | FAIL | 3.9996067725230735 | >= 4.0 |
| acceptance-3-20260914_001845_705603: high detection FPS | PASS | 1.433192426820768 | >= 0.5 |
| acceptance-3-20260914_001845_705603: result age P95 | PASS | 92.695165 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260914_001845_705603: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260914_001845_705603: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260914_001845_705603: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260914_001845_705603: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260914_001845_705603: counters never reset | PASS | True | monotonic |
| acceptance-4-20260914_001845_705603: processed FPS | PASS | 29.463769890919973 | >= 25.0 |
| acceptance-4-20260914_001845_705603: low detection FPS | FAIL | 3.9996067725230735 | >= 4.0 |
| acceptance-4-20260914_001845_705603: high detection FPS | PASS | 1.2998722010699988 | >= 0.5 |
| acceptance-4-20260914_001845_705603: result age P95 | PASS | 91.898514 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260914_001845_705603: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260914_001845_705603: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 3.38983050847457 | <= 10.0% |
| process CPU average | PASS | 34.89543305596231 | <= 85.0% of host |
| process RSS growth | FAIL | 87.8789565992038 | <= 5.0% |
