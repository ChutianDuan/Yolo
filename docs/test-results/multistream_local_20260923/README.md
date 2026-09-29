# 本地资源观测与四路长期基线（2026-09-23）

详见[实施记录第65节](../../reports/多路实时监测实施记录_20260905.md)。用户确认暂时无真实摄像机，继续本地验证。

- [初版短测冻结审计](diag65_initial_probe_smoke_summary.json)：60秒四路原门槛PASS、退流全0；探针峰1.362秒，未直接用于长测。
- 初版来源：[资源探针](initial_multistream_resource_probe.py)、[有界驱动](initial_local_multistream_soak.py)。后续频率采样已降为每NUMA节点一个代表CPU，保留原始结果。
- 当前工具：[驱动](../../../yolo_onnx_cpp/tools/local_multistream_soak.py)、[资源采样](../../../yolo_onnx_cpp/tools/multistream_resource_probe.py)、[只读复核](../../../yolo_onnx_cpp/tools/verify_local_multistream_soak.py)。
- 完整原始结果放在`yolo_onnx_cpp/test_outputs/multistream_local_20260923/`，避免把新长测JSONL存入文档目录。
- 资源计算与身份防护4项回归通过；修正版短测已通过终态检查，四路八小时已启动但未完成，真实RTSP/业务质量未验收。

```bash
conda run -n yolo python yolo_onnx_cpp/tools/verify_local_multistream_soak.py yolo_onnx_cpp/test_outputs/multistream_local_20260923/diag65_four_resource_smoke_60s
```

## 修正版与当前八小时窗口

[修正版短测冻结审计](diag65_final_probe_smoke_summary.json)：原性能PASS，探针峰0.088秒、15条持久化样本，退流active/registered/两tier queue/in-flight全0。此短窗不代表长期验收。

`diag65_four_soak_8h`于`2026-09-23T06:45:41.175910+00:00`开始采集，60秒预热+28800秒正式窗，预计约`2026-09-23 14:46:41 UTC`结束。当前只确认真实driver/服务/collector、服务exe/8080和样本推进；尚无最终PASS/FAIL。原始日志和JSONL位于上述`test_outputs`新目录。用户已选择继续本地验证，真实摄像机和业务真值仍未覆盖。

运行中只读复核：

```bash
conda run -n yolo python yolo_onnx_cpp/tools/verify_local_multistream_soak.py yolo_onnx_cpp/test_outputs/multistream_local_20260923/diag65_four_soak_8h --live
```

结束后完整复核（退出0表示审计一致，原性能状态以输出为准）：

```bash
conda run -n yolo python yolo_onnx_cpp/tools/verify_local_multistream_soak.py yolo_onnx_cpp/test_outputs/multistream_local_20260923/diag65_four_soak_8h
```

本轮最终4项资源测试和4个Python文件编译通过；验收工具既有40项已在第64节通过。生产C++/模型/YAML未改。

## 固定前缀资源诊断

[前90条冻结分析](diag65_prefix_90_resources.json)复跑一致：首个完整五分钟窗四项速率/公平通过，整体前缀约7.5分钟，不能代替八小时正式验收。新分析工具5项离线回归与编译通过；原长测继续且采集源码保持不变。

```bash
conda run -n yolo python yolo_onnx_cpp/tools/summarize_multistream_soak_windows.py yolo_onnx_cpp/test_outputs/multistream_local_20260923/diag65_four_soak_8h --samples 90
```

## 2026-09-24状态更正

上述原八小时窗口已中断，只有231条样本且没有正式验收/退流结果。以[第66节证据](../multistream_local_20260924/README.md)的INCOMPLETE_INTERRUPTED为准；原启动和中期记录保留，不表示当前仍存活。新窗口使用不同目录和独立会话，未拼接原数据。
