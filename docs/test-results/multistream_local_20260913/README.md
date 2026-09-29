# 本地 MJPEG 多路实际推理诊断（2026-09-13）

这是原计划 Stage 0 的短时瓶颈诊断，不是生产 RTSP 验收。四段已有原始 BDD 视频均 1280×720，每段预编码 180 帧后循环，以约 30 FPS 发送；采样包含流启动，无稳定态 warmup，源与服务同机。服务实际使用既有 OpenVINO 高/低模型，输入 1280×736 / 640×384，模型线程 8/8，队列 2，最大请求/结果年龄 300 ms。

| 运行 | output FPS | low/high FPS | 结果帧龄 P95 ms | 原始报告 |
| --- | --- | --- | --- | --- |
| 四路 throughput/auto（实际池 4/4） | 全部 0 | 全部 0/0 | 无结果 | [报告](diag42_four_auto/acceptance/acceptance.md) |
| 单路 latency/1 | 15.731 | 3.566/2.066 | 411.807 | [报告](diag42_single_latency/acceptance/acceptance.md) |
| 四路 latency/1 | 0.033 / 0.033 / 0.300 / 1.933 | 详见报告 | 10797.665～29102.096 | [报告](diag42_four_latency/acceptance/acceptance.md) |

三组原门槛均 FAIL；健康、采集、清理、指纹和队列界限通过不能代替目标输出/新鲜度。源码在 [diagnostic_driver.py](diagnostic_driver.py)，每组 diagnostic.json 保存视频 SHA-256、socket write 频率及退出码，acceptance.json 保留全部采样。服务完整日志只作本地调试，不是脱敏生产日志。短测 RSS 增长包含初始化，不能判为长期泄漏。

## 复现

从仓库根目录运行；需要指定 vcpkg OpenVINO 构建、既有四段视频/两个模型、yolo 环境的 OpenCV、Linux ss 以及空闲 8080。不安装依赖，不读取生产凭据；脚本生成测试专用 Token，只控制自己创建的子服务。

```bash
diag_work="$(mktemp -d /tmp/yolo-local-diag-XXXXXX)"
diag_stamp="$(date -u +%Y%m%d_%H%M%S)"
conda run -n yolo python docs/test-results/multistream_local_20260913/diagnostic_driver.py \
  --repo "$PWD" --work-dir "$diag_work" \
  --output-dir "docs/test-results/multistream_local_20260913/rerun_four_auto_$diag_stamp" \
  --count 4 --requests 0 --mode throughput --seconds 30
```

单路对照改为 `--count 1 --requests 1 --mode latency`；四路 latency 对照改为 `--count 4 --requests 1 --mode latency`，同时换新输出目录。拒绝已有输出目录，不覆盖当前报告。每轮采集器总体 FAIL/INCOMPLETE 会原样保留；驱动结束的返回码 0 仅表示夹具正常完成/退出，不表示验收通过。

## 后续重点

第 42 节基线中实时 manager 在 scheduler.submit 后阻塞 future.get，导致结果饥饿；第 43 节已实现 live High+Low 非阻塞及有界源历史回放，预算改用 steady 墙钟。当前优先测量排队/模型/回放与应用/丢弃时延，达成原模型 FPS 门槛；单模型异步及 queued-latest/in-flight 更新仍待补，不放宽年龄门槛或将旧检测直接贴到最新帧。

## 第 43 节：非阻塞 High+Low 升级复测

两组保持第 42 节视频、模型、输入、线程、300 ms 和原 FPS 门槛，每组 30 秒，仍包含启动而非稳态长测。新算法在模型 future 等待期间继续 flow，接收有效结果后按源历史回放。严格使用总体 FAIL，输出变流畅不等于检测质量/频率已达标。

| 复测 | output FPS 范围 | low/high FPS | 帧龄 P95 ms 范围 | 报告 |
| --- | --- | --- | --- | --- |
| 四路 throughput/auto 异步 | 29.429～29.895 | low 2.166～2.500，high 全 0 | 79.892～92.186 | [报告](diag43_four_auto_async/acceptance/acceptance.md) |
| 四路 latency/1 异步 | 29.229～29.663 | low 2.766～3.400，high 0.167～0.433 | 93.084～99.683 | [报告](diag43_four_latency_async_retry/acceptance/acceptance.md) |

output≥25、P95≤300、output 公平性≤10% 局部门槛通过；每路 low≥4/high≥0.5 仍未通过。CPU 仅服务占 host 64 逻辑核总容量；RSS 含启动，不支持长期稳定性结论。事件的当前帧不被旧检测重新编号，异步修正使用可选 inference_updates 保留源上下文。完整代码/计数/剩余要求见实施记录第 43 节。

初次 latency 复测在服务启动前端口探针报 Errno 98，空目录 diag43_four_latency_async 保留，不视作性能数据；后续只读检查无当前监听/TIME_WAIT，具体归因未证实。原驱动保持不变，新 [复测驱动](diagnostic_driver_async_20260913.py) 仅对探针设置 SO_REUSEADDR，仍确认独占 PID、遇外部服务拒绝。retry 使用新目录，正常退出。

复现使用上面的命令及新输出目录；默认组原驱动，latency 对照把脚本换为 diagnostic_driver_async_20260913.py 并使用 --count 4 --requests 1 --mode latency --seconds 30。若省略独立输出目录或覆盖既有结果，脚本会拒绝执行。

## 第 44 节：阶段耗时与 A/B/C 同源实测

使用新 [timing 驱动](diagnostic_driver_timing_20260913.py)，原第 42/43 节驱动保持不变。每组仍 30 s、既有四段 720p30 MJPEG/模型、线程 8/8、队列 2、300 ms 及原 FPS 门槛；新增 1 s 的实际 /metrics 采集，32 个样本含清理后的全局终值，采集错误为零。采样含启动，不是 RTSP/稳态长测。

| 组 | output FPS 范围 | low/high FPS | 帧龄 P95 ms 范围 | 证据 |
| --- | --- | --- | --- | --- |
| A latency/1 | 29.063～29.763 | low 3.133～3.500，high 0.233～0.367 | 92.341～96.971 | [报告](diag44_four_latency_timing/acceptance/acceptance.md)、[原始指标](diag44_four_latency_timing/timing_metrics.json) |
| B throughput/2 | 29.096～29.963 | low 1.733～2.300，high 全 0 | 43.426～127.307 | [报告](diag44_four_throughput2_timing/acceptance/acceptance.md)、[原始指标](diag44_four_throughput2_timing/timing_metrics.json) |
| C throughput/auto（池 4/4） | 29.297～29.863 | low 2.400～2.733，high 全 0 | 93.629～103.253 | [报告](diag44_four_auto_timing/acceptance/acceptance.md)、[原始指标](diag44_four_auto_timing/timing_metrics.json) |

三组总体 FAIL：输出/帧龄/输出公平性通过不能代替 low≥4/high≥0.5。已收取的 high 完成/接受/超龄为 A 88/37/51、B 76/0/76、C 155/0/155；B/C high 平均 infer 562.120/614.369 ms，A 高模平均 queue 98.663+infer 211.603 ms，scheduler 另有 A/B high stale 59/78。均值只包含上下文有效且在非取消循环登记的已收取 Completed，排队 Stale 不在分母；停止时少数 backend 完成未收取/未登记不伪计 accepted。

指标是固定大小 count/sum/max，/metrics 用秒，mean=sum/count；没有模型 P95 证据。三组 62 条全局 series 在完整时间线及移除后无回退。A/B low 分别有 7/1 个结果在回放结束后越过 300 ms 被拒绝；原最终门槛仍生效。RSS 含启动增长，不能判长期泄漏或稳定性。完整计时语义、源码/回归、计数差异与下一项见实施记录第 44 节。

复现从仓库根运行，新 work/output 路径，不覆盖上述目录：

```bash
diag_work="$(mktemp -d /tmp/yolo-local-timing-XXXXXX)"
diag_stamp="$(date -u +%Y%m%d_%H%M%S)"
conda run -n yolo python docs/test-results/multistream_local_20260913/diagnostic_driver_timing_20260913.py \
  --repo "$PWD" --work-dir "$diag_work" \
  --output-dir "docs/test-results/multistream_local_20260913/rerun_timing_$diag_stamp" \
  --count 4 --requests 1 --mode latency --seconds 30
```

B 改 --requests 2 --mode throughput，C 改 --requests 0 --mode throughput，并始终换新 output 目录。读取清理后的全局终值用 jq -r '.samples[-1].prometheus_text' 对相应 timing_metrics.json；stage sum/count 是加权均值，不能把当前输出年龄与模型源年龄混同。服务退出 0/夹具退出 0 与 collector 退出 2（FAIL）各自保留。

## 第 45 节：实际 native 属性与可选 CPU pinning 对照

新增 [native 驱动](diagnostic_driver_native_20260913.py)，历史驱动/报告不改。加入 --cpu-pinning inherit/true/false（默认 inherit 不写 override）、--high-threads/--low-threads 4/8/16（默认8/8）；每组归档实际模型runtime JSONL并核对显式 pinning 已应用。四组各30s、原源/模型/年龄/FPS门槛不改，采样含启动。两后端完整25/25、Qt共用代码构建通过，详见实施记录第45节。

| 组 | low / high FPS 范围 | actual streams high/low | 证据 |
| --- | --- | --- | --- |
| latency/1 默认绑核 8/8 | 3.066～3.566 / 0.133～0.500 | 1/1（应用池1/1） | [报告](diag45_four_latency_inherit/acceptance/acceptance.md)、[native/源证据](diag45_four_latency_inherit/diagnostic.json)、[原始指标](diag45_four_latency_inherit/timing_metrics.json) |
| latency/1 关闭绑核 8/8 | 3.833～3.933 / 0.400～0.567 | 1/1（应用池1/1） | [报告](diag45_four_latency_unpinned/acceptance/acceptance.md)、[native/源证据](diag45_four_latency_unpinned/diagnostic.json)、[原始指标](diag45_four_latency_unpinned/timing_metrics.json) |
| latency/1 关闭绑核 16/8 | 3.633～3.833 / 0.400～0.800 | 1/1（应用池1/1） | [报告](diag45_four_latency_unpinned_high16/acceptance/acceptance.md)、[native/源证据](diag45_four_latency_unpinned_high16/diagnostic.json)、[原始指标](diag45_four_latency_unpinned_high16/timing_metrics.json) |
| throughput/2 关闭绑核 16/8 | 2.366～2.866 / 0.567～0.767 | 2/4（应用池2/2） | [报告](diag45_four_throughput2_unpinned_high16/acceptance/acceptance.md)、[native/源证据](diag45_four_throughput2_unpinned_high16/diagnostic.json)、[原始指标](diag45_four_throughput2_unpinned_high16/timing_metrics.json) |

实际 C++ runtime=2026.1.0-000--，CPU/f32、HT=NO，默认 pinning=YES、显式 false→NO。关闭绑核时 high/low infer均值212.896/54.617→147.438/40.566ms，再仅high16线程后high120.889ms；throughput/2四流high通过但low不足4，四组总体均FAIL。32个metrics样本/62条全局series无回退/采集错误零，短测RSS含启动不能判长期稳定性。下一项queued-latest/in-flight及busy预算，不修改生产配置。

新目录复现，禁止覆盖已有证据：

```bash
diag_work="$(mktemp -d /tmp/yolo-local-native-XXXXXX)"
diag_stamp="$(date -u +%Y%m%d_%H%M%S)"
conda run -n yolo python docs/test-results/multistream_local_20260913/diagnostic_driver_native_20260913.py \
  --repo "$PWD" --work-dir "$diag_work" \
  --output-dir "docs/test-results/multistream_local_20260913/rerun_native_$diag_stamp" \
  --count 4 --requests 1 --mode latency --cpu-pinning false \
  --high-threads 16 --low-threads 8 --seconds 30
```

L1改 --cpu-pinning inherit --high-threads 8，L2改 --high-threads 8，T2改 --requests 2 --mode throughput；每轮换新output目录。夹具/服务退出0不表示验收通过；collector退出2原样保留。

## 第46节后续（2026-09-14）

busy检测预算修复的同参数复测已另存到[新日期目录](../multistream_local_20260914/README.md)。两后端完整25/25、cadence各10次通过；low刷新改善但原总体仍FAIL，高模排队/源年龄代价保留。第42～45节驱动与报告不覆盖，queued-latest/in-flight仍待完成。
