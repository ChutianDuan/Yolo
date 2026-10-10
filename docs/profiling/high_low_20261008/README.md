# High/Low 火焰图入门：一次真实的短视频分析

本次分析的是文件视频入口 `yolo::inferVideoFileHighLow`，不是多路实时流服务。主要观察方向是模型计算和光流。没有修改业务代码，也没有做性能优化。

## 先打开图

在浏览器中打开 SVG，可以悬停看函数与计数、点击矩形放大，点击左上角 Reset Zoom 返回全图，右上角 Search 搜索函数。某些 Markdown 图片预览不会执行 SVG 内的交互脚本，需要直接用浏览器打开文件。

1. [同步：主线程](sync-main.svg) — 先认识推理与光流调用链。
2. [异步：主线程](async-main.svg) — 观察主线程还在做哪些工作。
3. [异步：高分辨率工作线程](async-worker.svg) — 认识计算和等待。
4. [同步：全部线程](sync-all.svg) — 理解空闲线程为什么会把等待区域撑宽。

**这些图是 GDB 线程栈快照火焰图，宽度单位是 snapshots，包含运行与等待。不是 perf CPU 火焰图，也不是严格的墙钟时间分布。** 环境没有 PATH 中可用的 perf，且 `perf_event_paranoid=4`。本次使用现有 GDB，不修改系统设置。

## 怎么读

- **从下往上看调用关系**：下方是调用者，上方是被调用者。图顶端是那次快照当前停留的位置。
- **横向宽度看占比**：某个框越宽，包含它的栈快照越多。本次不是函数调用次数，也不是 CPU 耗时。
- **左右位置不是时间顺序**：相同调用栈合并并排序，不能据此判断先运行哪个函数。
- **高度是栈深度**：高不代表慢。火焰颜色默认帮助区分方块，不代表温度或严重程度。
- **父框含子框**：父框宽度包含子调用；不能把同一条链每一层的百分比加起来。框上方没有子框的水平部分对应停在该函数自身的快照。

例如父函数占 40%，它的子函数占 30%，不能说两者总共占 70%。子函数已经包含在父函数的 40% 中。Search 同时匹配父子函数时，图中的匹配率会合并重叠宽度。

CPU 火焰图也用同样的形状，但计数来自 on-CPU 采样，其宽度近似表示 CPU 上的消耗。它与本次包含等待的快照图有不同的统计口径。[Brendan Gregg 的火焰图说明](https://www.brendangregg.com/flamegraphs.html)、[CPU 火焰图说明](https://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html)。

## 按这三条路径练习

### 1. 在同步图搜索 `YoloEngine`

可以沿着这条链向上读，部分中间层省略：

```text
main
  inferVideoFileHighLow
    runModel
      YoloEngine::infer
        inferOnnxRuntime
          OrtApis::Run / onnxruntime::InferenceSession::Run
            Conv / MlasConv / MlasGemm...
```

这说明业务函数把工作交给了 ONNX Runtime；卷积和矩阵计算是实际执行模型运算的路径。看到 `YoloEngine::infer` 很宽时，应该继续看其子函数，不能直接认定这个业务包装函数自身很慢。

同步主线程共 88 次快照，其中 34 次具有可完整回溯到 `YoloEngine::infer` 的调用链（38.6%）。另外 26 次停在 `MlasGemmFloatKernelAvx512F`，但其上游回溯断在 `??`（29.5%）。**不能只拿前面的 38.6% 当作全部模型占比，也不能补造缺失的调用链。** MLAS 矩阵内核属于模型计算方向，但这 26 次无法从记录恢复具体 High/Low 调用者。

High 和 Low 共用 `runModel` 与 `YoloEngine::infer`，只按函数名折叠时会合并。原始 GDB 记录中，可回溯的同步模型栈分别对应 High 调用点 16 次、Low 调用点 18 次；缺失上游的 26 次不能分配给某一模型。因而本图不能用来得出 High/Low 各自完整的耗时比例。

### 2. 搜索 `weakTrackWithOpticalFlow` 和 `goodFeaturesToTrack`

```text
inferVideoFileHighLow
  StreamProcessor::prepareFrame
    weakTrackWithOpticalFlow / weakTrackWithOpticalFlowImpl
      goodFeaturesToTrack
      buildOpticalFlowPyramid
      calcOpticalFlowPyrLK
```

同步图有 23/88 次包含 `weakTrackWithOpticalFlowImpl`（26.1%）；其中 14 次包含角点检测，7 次包含 LK 光流，2 次包含金字塔构建。它们描述同一条父子路径，不能再和父框占比相加。

这让“光流”这个总计时有了具体内容：角点提取、金字塔、LK 传播及相关并行执行。之后要优化，应先用 CPU 采样确认这些子阶段实际消耗，再决定是否调整相关算法参数；本次不更改参数。

### 3. 打开异步工作线程图，搜索 `futex`

通过线程 ID 24 识别高分辨率工作线程，保留该线程全部 65 次快照，包括调用链不完整的快照。其中 40 次停在：

```text
AsyncInferWorker::run
  condition_variable::wait
    __pthread_cond_wait
      futex_wait_cancelable
```

也就是 40/65 = 61.5% 的快照中，它在等新任务。这不是“61.5% CPU 用在锁上”，也没有证明锁竞争。主线程仍可同时进行低分辨率推理、解码和光流。

本项目这个**文件推理入口**只把 High 放入 `AsyncInferWorker`；Low 的 `runModel` 仍在主线程。不要把这个结论直接套到另有调度策略的实时流入口。

全部线程图同样包含线程池中的空闲线程。同步采集每轮有 70 个线程，88 轮共 6160 条线程栈，和主线程图的 88 条不是同一个分母。不能拿两张图的百分比直接比较。

## 用未挂调试器的日志核对

两次基线在采集结束后顺序运行，输入均为现有本地视频的前 60 帧。下表是每种模式各一次的观测，不是稳定性能基准。

| 指标 | 同步 | 异步 |
| --- | ---: | ---: |
| 总 elapsed | 2612.288 ms | 1755.596 ms |
| 模型推理累计 | 1522.859 ms | 1739.118 ms |
| 光流累计 | 678.754 ms | 553.551 ms |
| 预处理累计 | 41.037 ms | 50.213 ms |
| tracker 累计 | 1.197 ms | 0.765 ms |
| 模型推理样本数 | 10 | 11 |
| 高分辨率异步请求数 | 0 | 2 |

同步模式中，模型推理约占总 elapsed 的 58.3%，光流约占 26.0%。日志中的 `timing_ratio.infer=0.679` 则是相对已统计阶段总量的比例，分母不同，不与 58.3% 混用。

异步模式中，模型、光流等累计时间可以超过总 elapsed：不同线程的阶段可能重叠。异步模型累计 1739 ms、光流累计 554 ms，但整体 elapsed 1756 ms，符合并行重叠的可能性。两次运行的检测调度和模型调用数也不同，不能仅用这两个总时间声称异步稳定提速多少。

原始基线见 [同步日志](baseline-sync.log) 和 [异步日志](baseline-async.log)。这些阶段日志是此次判断“模型与光流应优先观察”的证据；快照图进一步解释调用路径。

## 下一步：学习标准 perf CPU 采样

下面命令需要在已有可用 perf、且允许对自己进程采样的环境执行。它们是复现示例，本次没有执行。`cpu-clock:u` 采集用户态 CPU 时钟事件，DWARF 回溯对优化构建通常比默认的 frame-pointer 回溯更合适。[perf record 手册](https://man7.org/linux/man-pages/man1/perf-record.1.html)。

本次临时 High/Low-only 可执行文件位于 `/tmp/high-low-flame-20261008/profile_runner`。下面的 perf 命令会覆盖整个进程，包括模型加载和短视频准备；如要评估推理瓶颈，应在预热后单独采集推理区间，再解释占比。

```bash
profile_dir=$(mktemp -d /tmp/high-low-perf.XXXXXX)
flamegraph_dir=$(mktemp -d /tmp/flamegraph-tools.XXXXXX)
git clone --depth 1 https://github.com/brendangregg/FlameGraph.git "$flamegraph_dir"

CUDA_VISIBLE_DEVICES=4,5 YOLO_COMPARE_MAX_FRAMES=60 YOLO_COMPARE_ASYNC=0 \
  perf record -e cpu-clock:u -F 99 --call-graph dwarf,16384 \
  -o "$profile_dir/perf.data" -- /tmp/high-low-flame-20261008/profile_runner

perf script -i "$profile_dir/perf.data" > "$profile_dir/perf.script"
perl "$flamegraph_dir/stackcollapse-perf.pl" "$profile_dir/perf.script" \
  > "$profile_dir/cpu.folded"
perl "$flamegraph_dir/flamegraph.pl" --title 'High/Low CPU' \
  "$profile_dir/cpu.folded" > "$profile_dir/cpu.svg"
```

先做一次短采集检查符号是否清楚、有没有 `??` 或断栈，再扩大采样。不要把“框很宽”直接等同于“这段代码有 bug”。模型计算本来就可能是合理的主要成本。[FlameGraph 官方工具](https://github.com/brendangregg/FlameGraph)。

## 采集边界与文件

- 源码：HEAD `8415e1a146c058a8f451994f41fac630d7864475`，加工作区已有的 `video_inference_detail.cpp` 修改。独立构建使用当前源码，保留用户改动。
- 工具链：项目 `vcpkg-gcc15-release` preset，在新的 `/tmp` 构建目录覆盖为 `RelWithDebInfo`，只构建相关目标；未覆盖仓库 `build/`。
- 后端/模型：ONNX Runtime CPU；High 1280×736，Low 640×384；模型线程配置为 4。没有下载模型或数据。
- 输入：本地 `0000f77c-6257be58.mov` 前 60 帧，临时 MJPEG 片段，1280×720，约 30.149 FPS。文件读取不是实时流丢帧测试。
- 临时 runner 复用比较测试的输入与配置辅助函数；并未执行服务 `main` 中的全部初始化。OpenCV 线程池可与实际部署配置不同，不能把本次线程数视为服务固定线程数。
- GDB 从进入 `inferVideoFileHighLow` 到返回采集，排除片段准备及模型加载；每轮继续运行约 30 ms，再停住进程抓取线程栈，最多回溯 40 层。停止/恢复开销及调度扰动会改变异步时序；调试器内计时不用于性能结论。
- 同步 88 轮，异步 65 轮，均完整到函数返回；[同步元数据](sync-capture.json)、[异步元数据](async-capture.json)。样本较少，未采到 tracker 并不证明其零成本。
- SVG 由 upstream `flamegraph.pl` 渲染，旁边保留 `.folded` 输入。临时 runner、采样脚本和完整 GDB/JSON 记录在 `/tmp/high-low-flame-20261008/`，可能随系统清理消失。

完成验证：独立目标构建成功；现有比较测试的 6 帧 smoke test 通过；两个 High/Low-only 基线正常退出；两次 GDB 采集覆盖到函数返回并正常退出。业务代码未修改，因此没有运行全仓测试。
