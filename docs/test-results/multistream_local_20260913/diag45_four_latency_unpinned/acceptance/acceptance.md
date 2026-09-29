# 多路实时监测验收报告

- 开始时间：2026-09-13T02:27:13.035117+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260913_022713_035134 | running | 29.030 | 3.833 | 0.567 | 101.29 | 29 | 0 | 2 |
| acceptance-2-20260913_022713_035134 | running | 29.163 | 3.833 | 0.567 | 97.24 | 25 | 0 | 2 |
| acceptance-3-20260913_022713_035134 | running | 29.797 | 3.866 | 0.400 | 95.31 | 6 | 0 | 2 |
| acceptance-4-20260913_022713_035134 | running | 29.663 | 3.933 | 0.467 | 91.99 | 10 | 0 | 2 |

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
| requested duration covered | PASS | 30.003308310057037 | >= 30.0 s |
| sampling continuity | PASS | 1.015171863953583 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260913_022713_035134: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260913_022713_035134: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260913_022713_035134: counters never reset | PASS | True | monotonic |
| acceptance-1-20260913_022713_035134: processed FPS | PASS | 29.03013197741407 | >= 25.0 |
| acceptance-1-20260913_022713_035134: low detection FPS | FAIL | 3.8329106514381377 | >= 4.0 |
| acceptance-1-20260913_022713_035134: high detection FPS | PASS | 0.5666041832560725 | >= 0.5 |
| acceptance-1-20260913_022713_035134: result age P95 | PASS | 101.290474 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260913_022713_035134: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260913_022713_035134: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260913_022713_035134: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260913_022713_035134: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260913_022713_035134: counters never reset | PASS | True | monotonic |
| acceptance-2-20260913_022713_035134: processed FPS | PASS | 29.163450608768436 | >= 25.0 |
| acceptance-2-20260913_022713_035134: low detection FPS | FAIL | 3.8329106514381377 | >= 4.0 |
| acceptance-2-20260913_022713_035134: high detection FPS | PASS | 0.5666041832560725 | >= 0.5 |
| acceptance-2-20260913_022713_035134: result age P95 | PASS | 97.239825 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260913_022713_035134: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260913_022713_035134: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260913_022713_035134: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260913_022713_035134: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260913_022713_035134: counters never reset | PASS | True | monotonic |
| acceptance-3-20260913_022713_035134: processed FPS | PASS | 29.796714107701696 | >= 25.0 |
| acceptance-3-20260913_022713_035134: low detection FPS | FAIL | 3.86624030927673 | >= 4.0 |
| acceptance-3-20260913_022713_035134: high detection FPS | FAIL | 0.39995589406311 | >= 0.5 |
| acceptance-3-20260913_022713_035134: result age P95 | PASS | 95.311602 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260913_022713_035134: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260913_022713_035134: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260913_022713_035134: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260913_022713_035134: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260913_022713_035134: counters never reset | PASS | True | monotonic |
| acceptance-4-20260913_022713_035134: processed FPS | PASS | 29.663395476347326 | >= 25.0 |
| acceptance-4-20260913_022713_035134: low detection FPS | FAIL | 3.932899624953915 | >= 4.0 |
| acceptance-4-20260913_022713_035134: high detection FPS | FAIL | 0.466615209740295 | >= 0.5 |
| acceptance-4-20260913_022713_035134: result age P95 | PASS | 91.987731 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260913_022713_035134: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260913_022713_035134: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 2.5727069351230423 | <= 10.0% |
| process CPU average | PASS | 23.6291363562984 | <= 85.0% of host |
| process RSS growth | FAIL | 100.30094165614989 | <= 5.0% |
