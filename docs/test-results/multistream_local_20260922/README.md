# 六路本地长测证据（2026-09-22）

承接[实施记录第63节](../../reports/多路实时监测实施记录_20260905.md)。[独立驱动](diagnostic_driver_soak_20260922.py)使用冻结的第62节缓存服务，六条本地720p30 MJPEG连接复用四段视频。每5秒追加指标/内存JSONL，内存只保留最新样本；真实摄像机RTSP与独立业务质量仍需验收。

[60秒短测原报告](diag63_journal_smoke_60s/acceptance/acceptance.md)为FAIL：公平10.797%超过10%，其他性能门槛通过。13个正式快照、15条持久化样本、111条累计单调，自有三个进程退出；[冻结审计](soak_smoke_summary_20260922.json)可由[只读脚本](verify_soak_20260922.py)复核。

八小时测试 `diag63_six_soak_8h` 已于2026-09-22 01:25:56 UTC启动采集器，先预热60秒再采样28800秒，预计约09:27 UTC结束。**历史启动记录：该运行已结束，正式结果FAIL，详见下方第64节终态归档。**[运行身份](diag63_six_soak_8h/run_state.json)与[末次采样](diag63_six_soak_8h/progress.json)已冻结；只有结合/proc启动标识、实际exe和端口检查，才能确认存活。原始样本为 `diag63_six_soak_8h/live_samples.jsonl`，现已保留最终验收/诊断，尾部指标缺口见下文。

运行中只读检查：

```bash
conda run -n yolo python docs/test-results/multistream_local_20260922/verify_soak_20260922.py diag63_six_soak_8h --live
```

中期固定前3250条样本已由[分段脚本](inspect_soak_windows_20260922.py)生成[冻结分析](soak_prefix_3250_summary_20260922.json)，前缀SHA及全部结果复跑一致。54个完整五分钟诊断窗中output/low/high/公平越界分别13/40/35/44窗；这只覆盖前约4h34m及四项速率诊断，该段保留中期口径；完整八小时结论见下方终态归档。

```bash
conda run -n yolo python docs/test-results/multistream_local_20260922/inspect_soak_windows_20260922.py --samples 3250
```

短测复核及长测结束后的完整复核：

```bash
conda run -n yolo python docs/test-results/multistream_local_20260922/verify_soak_20260922.py diag63_journal_smoke_60s
conda run -n yolo python docs/test-results/multistream_local_20260922/verify_soak_20260922.py diag63_six_soak_8h
```

## 2026-09-23终态归档（实施记录第64节）

原八小时采样完整结束，**性能FAIL / 退流指标审计INCOMPLETE**。四路low<4FPS、公平10.382%、正式RSS+14.632%；驱动BrokenPipeError跳过最终metrics抓取。原文件保持不变，原严格验证器仍应拒绝将其认作完整成功运行。

- [八小时原验收](diag63_six_soak_8h/acceptance/acceptance.md)、[终态冻结审计](soak_8h_completion_summary_20260923.json)、[全部5690条分段](soak_prefix_5690_summary_20260923.json)。
- [修正版驱动](diagnostic_driver_soak_filefirst_20260922.py)、[闭管道回归入口](run_closed_stdout_smoke_20260922.py)、[闭管道真实结果](closed_stdout_smoke_summary_20260922.json)。
- [短测原验收](diag64_closed_stdout_60s/acceptance/acceptance.md)为PASS；[短测冻结审计](closed_stdout_audit_summary_20260923.json)确认最终active/registered/两tier queue/in-flight全0。该短窗不替代八小时FAIL。
- 2026-09-23验收工具40/40及新增三脚本py_compile通过；未改C++/依赖/模型/部署YAML。后续需CPU/内存资源分段观测、四八路长测、真实RTSP及业务质量。

只读复核（审计命令退出0表示与原事实一致，不表示性能或缺失证据被判通过）：

```bash
conda run -n yolo python docs/test-results/multistream_local_20260922/audit_soak_completion_20260923.py diag63_six_soak_8h
conda run -n yolo python docs/test-results/multistream_local_20260922/audit_soak_completion_20260923.py diag64_closed_stdout_60s
conda run -n yolo python docs/test-results/multistream_local_20260922/inspect_soak_windows_20260922.py --samples 5690
```
