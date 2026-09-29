# 本地长测中断归档与独立会话验证（2026-09-24）

详见[实施记录第66节](../../reports/多路实时监测实施记录_20260905.md)。本轮未改生产C++、模型、部署YAML或验收门槛。

- [原四路中断收据](diag65_interruption_receipt.json)：原三个PID均不存在，231条样本后无正式报告/终态；INCOMPLETE_INTERRUPTED，不是八小时完成。
- [231条分段诊断](diag65_interrupted_prefix_231.json)：只覆盖首次四流后约19.6分钟，3个完整五分钟窗四项速率/公平通过，原始数据未覆盖或拼接。
- [独立会话启动器](../../../yolo_onnx_cpp/tools/launch_local_multistream_soak.py)：当前yolo Python、独立session、文件日志，记录launch身份并保护旧输出。
- [独立会话60秒冻结审计](diag66_detached_smoke_summary.json)：启动命令退出后任务继续并完成，原性能PASS、退流全0；实际防覆盖检查和启动器编译通过。
- 新八小时原始结果路径：`yolo_onnx_cpp/test_outputs/multistream_local_20260924/diag66_four_detached_soak_8h/`。尚未取得最终验收，需核对真实进程及样本，不能仅看launch/run_state。

运行中只读检查：

```bash
conda run -n yolo python yolo_onnx_cpp/tools/verify_local_multistream_soak.py yolo_onnx_cpp/test_outputs/multistream_local_20260924/diag66_four_detached_soak_8h --live
```

真实结束后去掉`--live`进行完整复核，再保存冻结汇总和完整分段结果。

新采集器开始UTC：`2026-09-24T04:36:48.525151+00:00`；预计结束：`2026-09-24 12:37:48 UTC`。启动命令退出后，独立会话ID=87299和三个进程/服务exe/8080/新采样已验证。此记录不是最终验收。

## 09:42中期复核

同一独立窗口及三个进程仍活跃，3566条采样推进；超过旧窗口中断范围。新冻结[前3500条资源诊断](diag66_prefix_3500_resources.json)复跑一致：约5小时、59个完整五分钟窗的输出/low/high/公平四项无越界。RSS独立端点约+5.106%、峰792616KiB，不能据该近似边界判断正式八小时内存门槛；完整性能、P95和退流仍待结束。

```bash
conda run -n yolo python yolo_onnx_cpp/tools/summarize_multistream_soak_windows.py yolo_onnx_cpp/test_outputs/multistream_local_20260924/diag66_four_detached_soak_8h --samples 3500
```

## 2026-09-27 终态补记

原四路独立会话已于2026-09-24 12:37:50 UTC正常收尾，完整8小时原门槛PASS，退流全部归零；最新冻结终态和全量分段见[20260927证据索引](../multistream_local_20260927/README.md)。上文运行态记录仅为当时历史状态。
