# High + LK 分阶段计时与 CPU 实验

本目录仅用于完善 FP32/INT8 对比的实验统计。使用上一轮既有模型和视频，检测与跟踪逻辑保持一致。

原始逐帧数据、性能采样和日志仅保留在本地，不随仓库提交。运行实验需要先准备本地模型、视频和上一轮原始对照记录。已有报告记录的是跟踪时钟修复前的历史结果，当前代码的输出可能不同；脚本的逐帧一致性检查会检测这种变化。

- [本次报告](results/report.md)
- [耗时和 CPU 图表](results/stage_cpu_comparison.png)
- [汇总 JSON](results/summary.json)、[阶段 CSV](results/metrics.csv)、[CPU CSV](results/cpu.csv)
- [插桩差异](instrumented/instrumentation.diff)、[源码来源](instrumented/provenance.json)
- [上一轮模型大小、内存与精度报告](../high_fp32_int8_lk_20261008/report.md)

Decode 表示视频读取与解码，模型输出解析计入 Postprocess。LK Flow 包含灰度转换，ByteTrack 统计光流轨迹更新和检测关联调用。六阶段未覆盖的工作保留为 Other。

CPU 按进程用户态与内核态 CPU 时间之和除以墙钟时间统计，允许超过 100%；整机归一化指标再除以逻辑 CPU 数。模型加载、预热和结果序列化不计入运行期。

复现：

```bash
bash docs/test-results/high_stage_cpu_20261010/reproduce.sh /path/to/new/result-directory
```

脚本拒绝覆盖已有结果，使用 `/root/vcpkg` GCC15 工具链构建，使用 `yolo` Conda 环境执行 Python。生成的插桩源码是实验副本，不修改服务端代码和返回格式。正式运行三轮、共 18 个进程；逐帧输出与旧结果比较，并检查阶段采样数、CPU 时间和耗时闭合。
