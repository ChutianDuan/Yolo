# 多路实时监测验收报告

- 开始时间：2026-09-13T02:08:16.756883+00:00
- 实际采样时长：30.0 s
- 要求采样时长：30.0 s
- 请求流数：4
- 生命周期循环：0/0，创建 0 次，删除 0/0 次
- 总体结果：FAIL
- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。

## 每路结果

| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-1-20260913_020816_756900 | running | 29.096 | 2.300 | 0.000 | 127.31 | 27 | 0 | 2 |
| acceptance-2-20260913_020816_756900 | running | 29.696 | 2.300 | 0.000 | 69.36 | 9 | 0 | 2 |
| acceptance-3-20260913_020816_756900 | running | 29.929 | 1.733 | 0.000 | 43.43 | 2 | 0 | 2 |
| acceptance-4-20260913_020816_756900 | running | 29.963 | 2.100 | 0.000 | 53.05 | 1 | 0 | 2 |

## 复现证据

- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。
- CPU：Intel(R) Xeon(R) Gold 5218 CPU @ 2.30GHz
- 逻辑核/物理核/插槽：64 / 32 / 2
- 采集器 CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]
- 监测 PID CPU affinity：[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63]

| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |
| --- | --- | ---: | --- | --- | --- |
| config | diagnostic-config.yaml | 2157 | 97af3d4825dd4b96d30c88b0599ea54ae0ff7f2fc45bcd48e0034fd5f65730cf | 97af3d4825dd4b96d30c88b0599ea54ae0ff7f2fc45bcd48e0034fd5f65730cf | True |
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
| requested duration covered | PASS | 30.00406279298477 | >= 30.0 s |
| sampling continuity | PASS | 1.0053789649391547 | maximum gap <= 6.0 s |
| all requested streams observed | PASS | 4/4 | 4/4 |
| acceptance-1-20260913_020816_756900: every sample observed | PASS | 31 | 31 |
| acceptance-1-20260913_020816_756900: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-1-20260913_020816_756900: counters never reset | PASS | True | monotonic |
| acceptance-1-20260913_020816_756900: processed FPS | PASS | 29.09605962443578 | >= 25.0 |
| acceptance-1-20260913_020816_756900: low detection FPS | FAIL | 2.2996885613815223 | >= 4.0 |
| acceptance-1-20260913_020816_756900: high detection FPS | FAIL | 0.0 | >= 0.5 |
| acceptance-1-20260913_020816_756900: result age P95 | PASS | 127.307398 | <= 300.0 ms (local decode to query) |
| acceptance-1-20260913_020816_756900: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-1-20260913_020816_756900: queue bound | PASS | 2 | <= 2 |
| acceptance-2-20260913_020816_756900: every sample observed | PASS | 31 | 31 |
| acceptance-2-20260913_020816_756900: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-2-20260913_020816_756900: counters never reset | PASS | True | monotonic |
| acceptance-2-20260913_020816_756900: processed FPS | PASS | 29.695978379578786 | >= 25.0 |
| acceptance-2-20260913_020816_756900: low detection FPS | FAIL | 2.2996885613815223 | >= 4.0 |
| acceptance-2-20260913_020816_756900: high detection FPS | FAIL | 0.0 | >= 0.5 |
| acceptance-2-20260913_020816_756900: result age P95 | PASS | 69.361814 | <= 300.0 ms (local decode to query) |
| acceptance-2-20260913_020816_756900: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-2-20260913_020816_756900: queue bound | PASS | 2 | <= 2 |
| acceptance-3-20260913_020816_756900: every sample observed | PASS | 31 | 31 |
| acceptance-3-20260913_020816_756900: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-3-20260913_020816_756900: counters never reset | PASS | True | monotonic |
| acceptance-3-20260913_020816_756900: processed FPS | PASS | 29.929280117689956 | >= 25.0 |
| acceptance-3-20260913_020816_756900: low detection FPS | FAIL | 1.7330986259686834 | >= 4.0 |
| acceptance-3-20260913_020816_756900: high detection FPS | FAIL | 0.0 | >= 0.5 |
| acceptance-3-20260913_020816_756900: result age P95 | PASS | 43.425741 | <= 300.0 ms (local decode to query) |
| acceptance-3-20260913_020816_756900: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-3-20260913_020816_756900: queue bound | PASS | 2 | <= 2 |
| acceptance-4-20260913_020816_756900: every sample observed | PASS | 31 | 31 |
| acceptance-4-20260913_020816_756900: healthy status | PASS | {'final': 'running', 'terminal': []} | running at end; no terminal status during sampling |
| acceptance-4-20260913_020816_756900: counters never reset | PASS | True | monotonic |
| acceptance-4-20260913_020816_756900: processed FPS | PASS | 29.96260893742012 | >= 25.0 |
| acceptance-4-20260913_020816_756900: low detection FPS | FAIL | 2.09971564300052 | >= 4.0 |
| acceptance-4-20260913_020816_756900: high detection FPS | FAIL | 0.0 | >= 0.5 |
| acceptance-4-20260913_020816_756900: result age P95 | PASS | 53.051529 | <= 300.0 ms (local decode to query) |
| acceptance-4-20260913_020816_756900: inference errors | PASS | 0 | 0, including before the first sample |
| acceptance-4-20260913_020816_756900: queue bound | PASS | 2 | <= 2 |
| stream fairness spread | PASS | 2.8921023359288034 | <= 10.0% |
| process CPU average | PASS | 11.487396652497402 | <= 85.0% of host |
| process RSS growth | FAIL | 121.36124414059195 | <= 5.0% |
