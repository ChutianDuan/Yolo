# 本地四路检测预算修复复测（2026-09-14）

这是实施记录第46节的短时诊断，不是生产RTSP/稳态质量验收。仅改cadence/live High+Low调用及既有回归，不改生产YAML、模型/阈值、队列2、300ms和原4/0.5/25FPS门槛。

| 组 | output FPS范围 | low/high FPS范围 | 输出龄P95 ms范围 | 证据 |
| --- | --- | --- | --- | --- |
| latency/1 关闭绑核8/8 | 29.029～29.729 | low3.966～3.999 / high0.267～0.433 | 88.735～99.051 | [报告](diag46_four_latency_unpinned/acceptance/acceptance.md)、[native/源证据](diag46_four_latency_unpinned/diagnostic.json)、[原始指标](diag46_four_latency_unpinned/timing_metrics.json) |
| latency/1 关闭绑核16/8 | 28.530～29.763 | low3.900～4.000（实际<4） / high0.333～0.767 | 92.220～99.387 | [报告](diag46_four_latency_unpinned_high16/acceptance/acceptance.md)、[native/源证据](diag46_four_latency_unpinned_high16/diagnostic.json)、[原始指标](diag46_four_latency_unpinned_high16/timing_metrics.json) |

两组原总体均FAIL。4.000是四舍五入，实际最大分别3.999429979/3.999523659，门槛不降低。8/8 low480登记完成全接受，16/8 low478完成/473接受/5超龄；high分别133/44/89与130/68/62。相对第45节同配置low接受改善，但8/8 high61→44且queue均值102.702→144.101ms，保留代价；不能将low改善当作完整验收。

实际C++ runtime2026.1.0-000--，两tier CPU/f32、native streams/应用池1/1，pinningNO。源为四段既有720p30 BDD视频，每段180帧6秒MJPEG循环，配置与第45节对应组cmp相同，含启动且与源同机。每组31验收快照、32metrics样本/62全局series无回退，错误零；服务/驱动退出0不表示验收通过，collector退出2原样保留。短测RSS含初始化，不证明8h稳定性。

使用第45节[原native驱动](../multistream_local_20260913/diagnostic_driver_native_20260913.py)，历史驱动/报告不改。需要已有vcpkg OpenVINO构建、视频/两个模型、yolo环境与空闲8080；脚本独占/只清理自有子服务，不安装依赖、不读生产凭据。

```bash
diag_work="$(mktemp -d /tmp/yolo-local-cadence-XXXXXX)"
diag_stamp="$(date -u +%Y%m%d_%H%M%S)"
conda run -n yolo python docs/test-results/multistream_local_20260913/diagnostic_driver_native_20260913.py \
  --repo "$PWD" --work-dir "$diag_work" \
  --output-dir "docs/test-results/multistream_local_20260914/rerun_cadence_$diag_stamp" \
  --count 4 --requests 1 --mode latency --cpu-pinning false \
  --high-threads 8 --low-threads 8 --seconds 30
```

16/8只改 --high-threads 16并换新output目录。脚本拒绝已有输出，不覆盖证据。下一项queued-latest唯一ticket/原子拒绝in-flight更新；真实RTSP/长测/独立质量/NUMA与完整计划仍未完成。

## 第47节：queued-latest 同参数复测

新增弱任务唯一ticket和出队同锁失效，只更新queued输入；同步context/capture/ROI/preprocess成功才生效，不改旧promise、队列位置或urgent分类。两后端完整25/25（56.48/61.84s），scheduler各10次通过。源码/语义和剩余范围见实施记录第47节。

四路原16/8 latency/1关闭绑核、相同YAML/源/模型/阈值/队列/300ms/FPS门槛运行30s：[原FAIL报告](diag47_four_latency_unpinned_high16/acceptance/acceptance.md)、[native/源证据](diag47_four_latency_unpinned_high16/diagnostic.json)、[原始metrics](diag47_four_latency_unpinned_high16/timing_metrics.json)。

high1.233～1.433FPS四流通过、登记接受68→161/超龄62→3，scheduler stale28→0；low3.966277～3.999607仍<4，原总体FAIL。high/low成功更新801/693不算有效检测；CPU30.425→34.895%host上升，RSS含启动不证明8h稳定。32样本/原62async+2update共64全局series无回退，采集/清理正常但不等于验收通过。

复现用上面第45节原native驱动、新work/output目录，改 --high-threads 16，保持 --requests 1 --mode latency --cpu-pinning false --low-threads 8 --seconds 30。拒绝覆盖本目录/历史报告；下一项预算余量与重复/稳态/真实源质量复核、单模型非阻塞，生产YAML不改。


## 第48节：显式同流预热与低模预算余量

新增[预算驱动](diagnostic_driver_budget_20260914.py)，沿用actual runtime/源指纹与自有服务清理，默认预算4/预热0；仅本轮测试配置允许预算5/预热10。生产YAML、模型、阈值及第42～47节驱动/报告不改。验收最低low仍4/high0.5/output25 FPS、P95≤300ms、CPU≤85%host、公平≤10%、RSS≤5%。

先保存[工具兼容首红](diag48_four_warmup10_low4/acceptance/acceptance.md)、[诊断/正常connecting状态](diag48_four_warmup10_low4/diagnostic.json)与[原始指标](diag48_four_warmup10_low4/timing_metrics.json)：预热误拒connecting导致零正式样本，不能用作性能基线。新增连接状态回归先红后绿，仅修复正常状态兼容，不放宽终态/错误/回退/最终running及前进要求。原失败目录不覆盖。

| 组 | output FPS范围 | low/high实际FPS范围 | 本地结果龄P95 ms范围 | CPU%host / RSS增长% / 公平差异% | 原总体及证据 |
| --- | --- | --- | --- | --- | --- |
| 4FPS预算、预热10s后60s | 27.581～29.365 | low3.933058～3.999720 / high1.167～1.400 | 93.735～171.243 | 34.821 / 1.382 / 6.073 | **FAIL**：[报告](diag48_four_warmup10_low4_retry/acceptance/acceptance.md)、[native/源](diag48_four_warmup10_low4_retry/diagnostic.json)、[metrics](diag48_four_warmup10_low4_retry/timing_metrics.json) |
| 5FPS预算、相同预热/后窗 | 26.981～29.498 | low4.766～5.000 / high1.267～1.433 | 83.188～156.526 | 38.098 / 1.619 / 8.531 | **PASS（仅本地短窗）**：[报告](diag48_four_warmup10_low5/acceptance/acceptance.md)、[native/源](diag48_four_warmup10_low5/diagnostic.json)、[metrics](diag48_four_warmup10_low5/timing_metrics.json) |
| 5FPS预算、独立同配置重复 | 27.831～29.531 | low4.850～5.016 / high1.167～1.483 | 76.477～160.678 | 37.438 / 0.547 / 5.756 | **PASS（仅本地短窗）**：[报告](diag48_four_warmup10_low5_repeat/acceptance/acceptance.md)、[native/源](diag48_four_warmup10_low5_repeat/diagnostic.json)、[metrics](diag48_four_warmup10_low5_repeat/timing_metrics.json) |

每组同一批四流预热10s后采样60s，正式报告FPS/CPU/RSS仅取后窗；原始metrics全局累计包括创建、预热、采样和清理，不能把其均值冒充仅60s稳态。原64条全局series单调和退出/清理证据见各组文件，短窗PASS不是实际RTSP、8h稳定性或业务质量验收。工具预热默认0仍schema4，显式预热schema5；35项Python及两套相关重复回归通过，详细变更/验证/边界见实施记录第48节。

复现须已有vcpkg OpenVINO构建、两个模型、四段既有视频、yolo环境与空闲8080，不安装依赖/下载模型/读生产凭据。使用全新目录，脚本拒绝覆盖原证据：

```bash
diag_work="$(mktemp -d /tmp/yolo-local-budget-XXXXXX)"
diag_stamp="$(date -u +%Y%m%d_%H%M%S)"
conda run -n yolo python docs/test-results/multistream_local_20260914/diagnostic_driver_budget_20260914.py \
  --repo "$PWD" --work-dir "$diag_work" \
  --output-dir "docs/test-results/multistream_local_20260914/rerun_budget_$diag_stamp" \
  --count 4 --requests 1 --mode latency --cpu-pinning false \
  --high-threads 16 --low-threads 8 --low-detect-fps 5 --warmup-seconds 10 --seconds 60
```

4FPS基线只改 --low-detect-fps 4；每次另存新output目录，独占自有服务串行。下一项更长稳态/真实目标源/独立业务质量与单模型非阻塞边界；不据本地短窗冻结生产参数，整个计划保持部分完成。

## 第49节：唯一640模型的live异步回放

新增[单模型驱动](diagnostic_driver_single_20260914.py)，两个case使用同一唯一best_640x384.onnx/640×384放high scheduler slot，low模型为空。仅测试YAML选择 --model-async false/true，不改生产YAML、模型/阈值或历史驱动。单模collector high实际接受检测≥4 FPS、low≥0（不存在）；output≥25 FPS/P95≤300ms/CPU≤85%host/公平≤10%/RSS≤5%保持，不能把此PASS当成High+Low双模型4/0.5验收。

live单模型新增1 future/弱任务ticket、steady预算、仅queued可更新来源，32灰度历史+1前缀基态，原ByteTracker/光流源帧精确重算，收到/完整回放后双deadline，失败/超龄回滚。持久720p灰度每流约29MiB/四流约116MiB（仅gray pixels，实际RSS还含模型/当前帧/OpenCV等），成本不隐藏。离线/async=false/无scheduler保留原同步，另一模式状态恢复显式拒绝。

| 组 | output FPS范围 | 单模型实际接受检测FPS范围 | 结果龄P95 ms范围 | CPU%host / RSS后窗增长% / 公平差异% | 原总体及证据 |
| --- | --- | --- | --- | --- | --- |
| 原同步async=false、detect5 | 17.364～23.464 | 5.999～6.066 | 132.563～164.075 | 16.670 / -0.470 / 25.994 | **FAIL**：[报告](diag49_four_single_sync5/acceptance/acceptance.md)、[native/源](diag49_four_single_sync5/diagnostic.json)、[metrics](diag49_four_single_sync5/timing_metrics.json) |
| 首次异步整窗回放 | 9.499～11.265 | 0.266626～0.499925 | 334.276～387.129 | 17.701 / -1.594 / 15.680 | **FAIL**：[报告](diag49_four_single_async5/acceptance/acceptance.md)、[native/源](diag49_four_single_async5/diagnostic.json)、[metrics](diag49_four_single_async5/timing_metrics.json) |
| 正确检查点修复后异步 | 29.563～29.763 | 5.066～5.166 | 60.805～111.789 | 18.808 / -3.588 / 0.672 | **PASS（仅单模本地短窗）**：[报告](diag49_four_single_checkpoint5/acceptance/acceptance.md)、[native/源](diag49_four_single_checkpoint5/diagnostic.json)、[metrics](diag49_four_single_checkpoint5/timing_metrics.json) |
| 同配置检查点独立重复 | 29.630～29.996 | 5.033～5.199 | 59.445～84.380 | 17.919 / 0.381 / 1.222 | **PASS（仅单模本地短窗）**：[报告](diag49_four_single_checkpoint5_repeat/acceptance/acceptance.md)、[native/源](diag49_four_single_checkpoint5_repeat/diagnostic.json)、[metrics](diag49_four_single_checkpoint5_repeat/timing_metrics.json) |

各case四路720p30/180帧6s循环，预热10s后30s，正式报告FPS/CPU/RSS不含预热；原始metrics全生命周期累计含创建/预热/采样/清理。actual linked runtime/唯一engine/CPU属性、四源SHA与清理见各组文件。结果仅有界本地证据，不等价RTSP/8h稳定性或业务质量。两套vcpkg完整构建与CTest25/25（58.29/64.96s），精确状态回归各10次（3.00/3.09s）、真实scheduler元数据更新/ready拒绝及驱动py_compile通过，详细变更/风险/下一项见实施记录第49节。

复现需要已有规定vcpkg OpenVINO构建、模型/视频、yolo环境和空闲8080。每次新work/output目录，脚本拒绝覆盖，仅清理自有服务，不下载安装依赖/权重、不读生产凭据：

```bash
diag_work="$(mktemp -d /tmp/yolo-local-single-XXXXXX)"
diag_stamp="$(date -u +%Y%m%d_%H%M%S)"
conda run -n yolo python docs/test-results/multistream_local_20260914/diagnostic_driver_single_20260914.py \
  --repo "$PWD" --work-dir "$diag_work" \
  --output-dir "docs/test-results/multistream_local_20260914/rerun_single_$diag_stamp" \
  --count 4 --requests 1 --mode latency --cpu-pinning false \
  --high-threads 8 --low-threads 8 --detect-fps 5 --model-async true --warmup-seconds 10 --seconds 30
```

原同步对照仅改 --model-async false并另存新output。保持独占串行；下一项依原实测优化回放成本/重复/更长稳态与真实目标源/独立业务质量，整个计划仍部分完成。

## 第51节：固定profile 1/2/4/6/8路容量点

[独立驱动](diagnostic_driver_matrix_20260914.py)、[临时只读审计](verify_matrix_20260914.py)、[冻结汇总](matrix_summary_20260914.json)。同16/8 CPU/f32、latency/1、pinningNO、low预算5，原low4/high0.5/output25/年龄300ms/CPU85/RSS5/公平10门槛；每组预热10s后30s。完整结果与命令见[实施记录第51节](../../reports/多路实时监测实施记录_20260905.md)。

| 路数 | 原结果 | low实际FPS | output FPS | 证据 |
| --- | --- | --- | --- | --- |
| 1 | PASS（仅本地短窗） | 4.999 | 28.696 | [报告](diag51_one_fixed/acceptance/acceptance.md)、[runtime/源](diag51_one_fixed/diagnostic.json)、[原metrics](diag51_one_fixed/timing_metrics.json) |
| 2 | PASS（仅本地短窗） | 4.999～5.033 | 28.529～29.129 | [报告](diag51_two_fixed/acceptance/acceptance.md)、[runtime/源](diag51_two_fixed/diagnostic.json)、[原metrics](diag51_two_fixed/timing_metrics.json) |
| 4 | PASS（仅本地短窗） | 4.899～5.033 | 27.730～28.830 | [报告](diag51_four_fixed/acceptance/acceptance.md)、[runtime/源](diag51_four_fixed/diagnostic.json)、[原metrics](diag51_four_fixed/timing_metrics.json) |
| 6 | FAIL（仅本地短窗） | 2.766～3.166 | 23.831～27.197 | [报告](diag51_six_fixed/acceptance/acceptance.md)、[runtime/源](diag51_six_fixed/diagnostic.json)、[原metrics](diag51_six_fixed/timing_metrics.json) |
| 8 | FAIL（仅本地短窗） | 1.900～2.166 | 25.297～27.963 | [报告](diag51_eight_fixed/acceptance/acceptance.md)、[runtime/源](diag51_eight_fixed/diagnostic.json)、[原metrics](diag51_eight_fixed/timing_metrics.json) |

五组31正式快照/41metrics，64条全局series不回退，源模型指纹/配置唯一变化/主机affinity/原阈值审计通过；源码编译与当前验收40/40通过。没有改运行C++/模型/YAML/历史产物，未重复构建/全量CTest（第50节两套最终25/25保留）。

6/8独立连接但复用四段内容，不是不同真实摄像头。六路部分output/公平也FAIL；八路output通过但low全FAIL。active/registered0、自有driver/server退出0、8080最终空，不虚报DELETE后worker立即空：六路high1在途，八路4排队+1在途，shutdown之后退出。服务日志/诊断配置仍为本地debug证据，不宣称已做生产日志清洗。

从仓库根执行 `conda run -n yolo python docs/test-results/multistream_local_20260914/verify_matrix_20260914.py` 可只读复核冻结五组；新跑务必另选全新输出目录。一组profile的五点不是完整线程/NUMA矩阵、真实RTSP/长稳态/8h或业务质量验收。

## 第52节：停流queued ticket取消

新增[独立单/双模HTTP退流驱动](diagnostic_driver_retirement_20260914.py)。实现同锁弱唯一ticket排队取消、live单/双模worker终态前清理、scheduler_cancelled_total；已进入模型调用不强制中断。两后端最终完整构建/25项通过（56.88/60.93s）、scheduler各10次（6.40/7.46s），driver编译通过。

| 实际八流30s组 | 成功取消 | post-DELETE队列 | 原性能结果 | 证据 |
| --- | --- | --- | --- | --- |
| High+Low16/8 latency/1 | high6/low5 | 两tier0 | FAIL | [报告](diag52_eight_dual_retirement/acceptance/acceptance.md)、[退流/runtime](diag52_eight_dual_retirement/diagnostic.json)、[原指标](diag52_eight_dual_retirement/timing_metrics.json) |
| 唯一640单模8线程 latency/1 | high角色5 | 0 | FAIL（检测最低4，low不存在） | [报告](diag52_eight_single_retirement/acceptance/acceptance.md)、[退流/runtime](diag52_eight_single_retirement/diagnostic.json)、[原指标](diag52_eight_single_retirement/timing_metrics.json) |

两组预热10s后30s、31正式快照/41metrics、66/64条全局累计无回退、指纹稳定，自有driver/server退出0、collector2，8080最终空。取消不虚算模型完成；八连接仍复用四段视频，单/双模型工作量不同，不作为性能/质量收益或真实RTSP/8h证据。历史第51节原64series和停流尾部不修改。

具体代码/计数守恒/复现命令与剩余项见[实施记录第52节](../../reports/多路实时监测实施记录_20260905.md)，新跑必须用全新输出目录；本地debug日志不等价生产第三方日志治理。

## 第53节：六路模型/OpenCV线程对照

[独立线程驱动](diagnostic_driver_threads_20260914.py)、[固定四组只读复核](verify_threads_20260914.py)、[冻结汇总](threads_summary_20260914.json)。六路720p30、四段内容复用，原low4/high0.5/output25/P95 300ms/CPU85/RSS5/公平10/队列2门槛不改，预热10s后30s。CPU/f32、latency、原生streams1/1、应用池1/1、pinningNO；按high/low/OpenCV依次单变量变化：

| 线程组 | output FPS | low/high实际FPS | CPU%host / 公平% | 原结果与证据 |
| --- | --- | --- | --- | --- |
| 16/8/4 | 21.764～24.896 | 1.766～2.133 / 0.433～0.700 | 49.214 / 12.584 | FAIL：[报告](diag53_six_baseline16_8/acceptance/acceptance.md)、[runtime/退流](diag53_six_baseline16_8/diagnostic.json)、[metrics](diag53_six_baseline16_8/timing_metrics.json) |
| 16/16/4 | 19.664～25.697 | 3.066～3.799 / 0.667～0.933 | 60.580 / 23.476 | FAIL：[报告](diag53_six_low16/acceptance/acceptance.md)、[runtime/退流](diag53_six_low16/diagnostic.json)、[metrics](diag53_six_low16/timing_metrics.json) |
| 8/16/4 | 22.731～26.964 | 3.366～4.066 / 0～0.100 | 49.962 / 15.698 | FAIL：[报告](diag53_six_high8_low16/acceptance/acceptance.md)、[runtime/退流](diag53_six_high8_low16/diagnostic.json)、[metrics](diag53_six_high8_low16/timing_metrics.json) |
| 8/16/1 | 18.794～26.192 | 3.032～4.265 / 0.033～0.133 | 45.725 / 28.244 | FAIL：[报告](diag53_six_cv1_high8_low16/acceptance/acceptance.md)、[runtime/退流](diag53_six_cv1_high8_low16/diagnostic.json)、[metrics](diag53_six_cv1_high8_low16/timing_metrics.json) |

baseline另有RSS5.518% FAIL，cv1一条P95=1067.093ms FAIL；舍入不改变原JSON判断。低模增线程infer76.614→46.982ms但吞吐/公平恶化，高模8线程累计153完成143超龄，不能按infer单项冻结参数。阶段累计含预热/清理，不冒充正式窗；第51节历史六路不是本轮对照。

四组31快照/41metrics/66全局series单调、起止指纹稳定、单变量/原生属性/源/主机/阈值审计通过，post-DELETE active/registered及队列/在途0，成功取消high/low=4/5、4/3、5/3、4/3。自有driver/server0、collector2，最终8080空。新脚本编译、验收40/40（0.545s）通过；未改C++，未重跑构建/全量CTest，最新运行代码完整证据仍第52节。

只读运行 `conda run -n yolo python docs/test-results/multistream_local_20260914/verify_threads_20260914.py`；新跑使用[实施记录第53节](../../reports/多路实时监测实施记录_20260905.md)的完整命令和全新目录，不覆盖历史产物。本轮新增测试驱动和文档，不改生产YAML/模型/依赖；四组性能FAIL不是审核拒绝（恢复后已实际执行），也不等价完整NUMA、真实RTSP、8h或质量验收。

## 第54节：六路独立request/原生streams对照

[独立高低模池驱动](diagnostic_driver_requests_20260914.py)、[固定四组只读复核](verify_requests_20260914.py)、[冻结汇总](requests_summary_20260914.json)。六独立连接复用四段720p30，threads16/16/pinningNO/CPU/f32不变，预热10s后30s，原low4/high0.5/output25/P95 300ms/CPU85/RSS5/公平10/队列2门槛不变。

| 组 | 应用池high/low / 原生streams / OpenCV | output FPS | low实际FPS | 原结果与证据 |
| --- | --- | --- | --- | --- |
| latency_h1_l1 | 1/1 / 1/1 / 4 | 21.263～25.596 | 3.366～3.933 | FAIL：[报告](diag54_six_latency_h1_l1/acceptance/acceptance.md)、[runtime/退流](diag54_six_latency_h1_l1/diagnostic.json)、[metrics](diag54_six_latency_h1_l1/timing_metrics.json) |
| latency_h1_l2 | 1/2 / 1/1 / 4 | 21.329～26.762 | 3.099～3.833 | FAIL：[报告](diag54_six_latency_h1_l2/acceptance/acceptance.md)、[runtime/退流](diag54_six_latency_h1_l2/diagnostic.json)、[metrics](diag54_six_latency_h1_l2/timing_metrics.json) |
| throughput_h1_l2 | 1/2 / 1/2 / 4 | 21.230～25.862 | 3.633～4.499 | FAIL：[报告](diag54_six_throughput_h1_l2/acceptance/acceptance.md)、[runtime/退流](diag54_six_throughput_h1_l2/diagnostic.json)、[metrics](diag54_six_throughput_h1_l2/timing_metrics.json) |
| throughput_h1_l2_cv2 | 1/2 / 1/2 / 2 | 19.064～23.364 | 3.633～4.533 | FAIL：[报告](diag54_six_throughput_h1_l2_cv2/acceptance/acceptance.md)、[runtime/退流](diag54_six_throughput_h1_l2_cv2/diagnostic.json)、[metrics](diag54_six_throughput_h1_l2_cv2/timing_metrics.json) |

逐次只改低模池、hint、OpenCV一行。应用低模池2但latency原生stream1，infer44.677→89.530ms且刷新未改善；throughput才实际stream2，完成871→1000/接受828→931但逐流output/low及公平仍FAIL。OpenCV2降低CPU但所有output<25，不采用任何组为生产默认。阶段累计含创建/预热/清理，不能冒充正式窗。

四组31正式快照/41metrics/66series单调、源/模型/配置起止指纹及主机/八门槛核对通过，post-DELETE active/registered/两tier队列/在途0，取消high/low=5/3、4/2、5/2、3/1，不计模型完成。driver/server0、collector2、8080最终空；新脚本编译、验收40/40（0.446s）通过。未改C++/YAML/模型/依赖或历史产物，未重复构建/全量CTest，最新完整仍第52节25/25。

只读复核 `conda run -n yolo python docs/test-results/multistream_local_20260914/verify_requests_20260914.py`；完整复现与未完成项见[实施记录第54节](../../reports/多路实时监测实施记录_20260905.md)，新跑全新目录、独占串行，不下载安装或使用生产凭据。本地短窗/四段复用及debug日志不替代完整NUMA、真实RTSP/8h、独立业务质量或第三方日志治理。

## 第55节：处理墙钟计时与六路定位

新增固定frame_work/prepare/poll/publish count/sum/max，三分支同锁累计/移除保留，/metrics增加12全局+每注册流12；/streams/SSE、算法/模型/配置/历史产物不改。[固定单点只读复核](verify_processing_20260914.py)、[冻结汇总](processing_summary_20260914.json)，复用第54节原驱动。

当前构建六流720p30（复用四段），throughput、CPU/f32、threads16/16、池/原生streams1/2、pinningNO、OpenCV配置4，预热10s后30s。原门槛不改：low4.033～4.633/high0.733～0.967均通过，但output19.397～23.697六条全FAIL/公平18.143% FAIL；P95 124.438～254.415ms、CPU60.786%host/RSS-2.677%通过，总体仍FAIL。

| 全生命周期累计stage | count | mean/max ms |
| --- | --- | --- |
| frame_work | 5296 | 42.735 / 381.467 |
| prepare | 5296 | 33.503 / 381.462 |
| poll | 5296 | 0.194 / 7.147 |
| publish | 5294 | 0.021 / 10.249 |

本组未挂SSE客户端，不能外推真实订阅发布耗时。父frame_work包含子stage，prepare占累计约78.397%；count是尝试而非成功输出，max不是P95，墙钟不是纯CPU。逐流最后注册scrape prepare29.381～39.598ms，瞬时在途子count先提交不强行当错误/归零。原async高/低完成252/1064、接受190/1018、超龄62/46保留。

[原报告](diag55_six_throughput_processing/acceptance/acceptance.md)、[runtime/源/退流](diag55_six_throughput_processing/diagnostic.json)、[原始metrics](diag55_six_throughput_processing/timing_metrics.json)。31正式快照/41metrics/78选定全局series单调，模型/配置起止SHA稳定，post-DELETE active/registered/队列/在途0、取消high4/low2，自有driver/server0、collector2、8080最终空。

最终两套构建/完整CTest25/25（66.83/68.73s）、纯指标各10次（0.04/0.04s）、真实manager/HTTP各2次（37.38/44.52s）、验收40/40（0.510s）和新增审计编译通过。所有测试退出后才采集本组，不与历史二进制不同窗比较声称性能提升。

仓库根只读运行 `conda run -n yolo python docs/test-results/multistream_local_20260914/verify_processing_20260914.py`；新跑使用[实施记录第55节](../../reports/多路实时监测实施记录_20260905.md)完整命令与全新目录。下一项细分prepare内部，不猜测纯LK根因；完整NUMA、真实RTSP/长期/独立质量和生产日志等仍未验收。

## 第56节：双向LK灰度金字塔复用

只改光流.cpp及既有stream_processor_test，弱光流/全局motion每段正反向共享两张无导数灰度金字塔（原四次构建→两次），最终LK后即释放，不跨帧缓存；窗口21/层3/特征/质量/接口/生产YAML/模型/依赖不改。实际vcpkg OpenCV4.12.0源码默认核对；12场景原Mat/金字塔正反向坐标/status/有效误差1e-4回归通过，不能替代真实业务质量。

[固定三点只读核对](verify_pyramids_20260914.py)、[冻结汇总](pyramids_summary_20260914.json)。六路720p30复用四段、threads16/16、throughput、CPU/f32、池/原生streams1/2、pinningNO、OpenCV配置4、预热10s后30s，原门槛不改：

| 点 | output FPS | prepare累计mean ms | RSS增长% / 公平% | 原结果与证据 |
| --- | --- | --- | --- | --- |
| 第55节历史原Mat路径 | 19.397～23.697 | 33.503 | -2.677 / 18.143 | FAIL：[报告](diag55_six_throughput_processing/acceptance/acceptance.md) |
| 初版复用 | 22.097～27.363 | 28.701 | 5.621 / 19.245 | FAIL：[报告](diag56_six_throughput_shared_pyramids/acceptance/acceptance.md)、[runtime/退流](diag56_six_throughput_shared_pyramids/diagnostic.json)、[metrics](diag56_six_throughput_shared_pyramids/timing_metrics.json) |
| 最终提前释放 | 20.964～26.063 | 29.825 | 11.102 / 19.565 | FAIL：[报告](diag56_six_throughput_released_pyramids/acceptance/acceptance.md)、[runtime/退流](diag56_six_throughput_released_pyramids/diagnostic.json)、[metrics](diag56_six_throughput_released_pyramids/timing_metrics.json) |

最终low4.199～4.799/high0.700～1.067、P95 110.906～233.010ms、CPU61.072%host通过，但4条output/公平/RSS仍FAIL；提前释放没有证明修复RSS，不冻结部署profile。不同时间窗虽配置/native相同，也不能据差异宣称受控加速或内存唯一根因。处理累计含创建/预热/清理，frame_work包含子stage，count非成功输出、max非P95；未挂SSE，不外推真实发布负载。

最终两套构建/完整25/25（56.51/61.62s）、原Mat数值和状态replay各3次（1.76/1.97s）、验收40/40（0.564s）与审计编译通过。三个点31快照/41metrics/78选定全局累计单调，模型/配置起止SHA、启动源SHA/路径核对，post-DELETE active/registered/队列/在途0，最终取消high4/low1；自有driver/server0、collector2，8080最终空。历史/初版原FAIL不覆盖。

仓库根只读复核 `conda run -n yolo python docs/test-results/multistream_local_20260914/verify_pyramids_20260914.py`；当前代码新跑及完整证据/风险见[实施记录第56节](../../reports/多路实时监测实施记录_20260905.md)，另选全新目录、独占串行。下一项独立原/新二进制受控对照、分配/热身定位，必要时调整/撤回候选；完整NUMA、真实RTSP/长期、业务质量/训练和生产治理仍未完成。

## 第57节：独立单对象 binary 对照

[限定binary驱动](diagnostic_driver_binary_20260914.py)、[固定只读核验](verify_binary_20260914.py)、[构建指纹](binary_build_20260914.json)、[参考源码](original_flow_reference_20260914.cpp)、[冻结汇总](binary_summary_20260914.json)。

原路径A与当前路径B独占串行；23个服务对象仅替换光流对象，22个共用对象、源码及两个binary前后SHA不变。两组使用相同六路720p30四段复用、threads16/16、throughput、池/native streams1/2、CPU/f32、pinningNO、OpenCV4、low预算5，预热10s后正式30s：

| binary | output FPS | low / high FPS | P95 ms | CPU% / 正式窗RSS% / 公平% | 原判定 |
|---|---:|---:|---:|---:|---|
| A原路径 | 22.363～26.596 | 4.366～4.899 / 0.900～1.100 | 100.210～181.752 | 61.494 / -3.752 / 15.915 | FAIL：4条output+公平 |
| B当前复用 | 22.630～27.329 | 4.266～4.866 / 0.800～1.033 | 115.085～191.726 | 60.945 / -7.811 / 17.195 | FAIL：4条output+公平 |

每组31正式快照、40组metrics及40个owned进程内存样本。全生命周期prepare均值30.720/28.246ms，Rss峰839800/844768KiB、匿名内存峰805908/810372KiB、OS线程峰92/97；轨迹包含启动/预热/退流且与metrics非原子，不用于重算正式窗RSS。单次顺序A/B不能证明统计加速、长期RSS或根因，原第56节RSS失败保留；无SSE客户端，不能外推慢消费者成本。

78累计、模型/源/runtime/YAML/主机/原门槛核对，退流后active/registered/队列/在途0，自有服务0、collector2且最终8080空。B驱动退出0；A控制台退出码未保留，但结束manifest和全部断言数据条件已独立核对，不虚记0。py_compile与验收40/40（0.549s）通过；未改运行C++或重跑完整CTest，最新仍为第56节25/25。运行 `conda run -n yolo python docs/test-results/multistream_local_20260914/verify_binary_20260914.py` 可复核冻结结果，完整边界见[实施记录第57节](../../reports/多路实时监测实施记录_20260905.md)。
