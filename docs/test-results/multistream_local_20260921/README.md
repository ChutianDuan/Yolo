# 第58节：六路 prepare 子阶段实测（2026-09-21）

承接[实施记录第58节](../../reports/多路实时监测实施记录_20260905.md)。原第57节二进制A/B、失败报告及证据不改。本目录独占串行执行 [diag58_six_prepare_60s](diag58_six_prepare_60s/acceptance/acceptance.md)；[服务/runtime/退流](diag58_six_prepare_60s/diagnostic.json)、[全部原始指标/内存样本](diag58_six_prepare_60s/timing_metrics.json)、[固定只读审计](verify_prepare_20260921.py)和[冻结汇总](prepare_summary_20260921.json)可复核。

与第57节B组相同YAML、源/模型起止指纹、actual OpenVINO CPU/f32运行时、threads16/16、throughput、池/native streams1/2、pinningNO、OpenCV4、六路720p30（六连接/四段复用）、low预算5及原4/0.5/25 FPS、300ms、85%CPU、5%RSS、10%公平、队列2门槛；预热10秒后正式60秒（不是8小时）。正式61快照/全生命周期68组指标及匿名内存样本，90条选定全局累计单调。

| 指标 | 实测 | 原门槛 |
|---|---:|---|
| 输出 FPS | 20.232～25.164 | 5条低于25，FAIL |
| low / high 检测 FPS | 3.350～4.133 / 0.433～0.617 | 4条low、4条high失败 |
| P95 / CPU / 正式RSS | 117.701～230.449ms / 57.511% / +0.345% | 此窗PASS |
| 公平差异 | 19.603% | 超过10%，FAIL |

全生命周期累计9434次prepare，均值30.676ms；gray 2.049ms/9434次，weak_flow 27.044ms/8596次，motion 4.360ms/8596次，projection 0.011ms/8596次。按全部stage累计墙钟sum占prepare分别6.680%、80.328%、12.951%、0.033%；分母为所有处理帧、不是60秒正式窗或每路CPU利用率。条件阶段跳过时不增加次数，准备帧parent包含子阶段与少量边界开销。结论仅定位弱光流大头，不证明其中是角点、金字塔、双向LK还是质量统计，也不证明性能已改善。

驱动/服务退出0、collector2记录真实FAIL，最终8080端口空。两后端完整构建、最终Release/OpenVINO CTest各25/25（59.52/63.75秒），OpenVINO首轮24/25中视频对比超时180秒；该项独占复跑5.65秒通过、第二次完整25/25，原超时仍保留。没有部署、正式训练、真实RTSP/8小时或业务质量结果；下一项细分弱光流内部并独立重复稳态，而不是更改门槛。

仓库根只读复核：

```bash
conda run -n yolo python docs/test-results/multistream_local_20260921/verify_prepare_20260921.py
```

## 第59节：弱光流六子阶段（2026-09-21）

承接[实施记录第59节](../../reports/多路实时监测实施记录_20260905.md)，第58节证据不改。[六路原验收报告](diag59_six_weak_60s/acceptance/acceptance.md)、[服务/runtime/退流](diag59_six_weak_60s/diagnostic.json)、[原始指标/内存采样](diag59_six_weak_60s/timing_metrics.json)、[固定只读审计](verify_weak_20260921.py)及[冻结汇总](weak_summary_20260921.json)保留同一组原始证据。仍为同YAML、四段内容复用的六条本地MJPEG连接、OpenVINO CPU/f32、threads16/16、throughput、池/native streams1/2、pinningNO、OpenCV4、low预算5和原门槛；非真实六摄像机RTSP。

预热10秒后正式60.004秒，61快照、69组全生命周期指标/内存样本；108条选定全局累计单调，服务与所涉源码实测前后SHA一致，collector2真实保留原报告FAIL，服务/驱动退出0，最终8080空。输出25.132～28.365、low4.766～5.000、high0.983～1.217 FPS，P95 108.020～192.982ms、CPU 60.145%、正式RSS +2.181%均过原门槛；公平11.398%超过10%，总体验收FAIL。

全生命周期11205次弱光流累计墙钟：全帧差值0.928ms/4.293%，ROI角点5.380ms/24.897%，金字塔2.704ms/12.513%，前向LK6.961ms/32.210%，反向LK5.531ms/25.595%，质量0.089ms/0.414%；比例分母为弱光流累计sum，少量差额为计时边界。并发墙钟不是纯CPU，窗口间差异不是优化效果；下一步对LK/ROI做受控候选及独立精度/重复稳态核验，不据单次短窗冻结配置。

```bash
conda run -n yolo python docs/test-results/multistream_local_20260921/verify_weak_20260921.py
```

## 第60节：ROI/角点工作量与独立复测（2026-09-21）

承接[实施记录第60节](../../reports/多路实时监测实施记录_20260905.md)，保留第58/59节证据。[首轮原报告](diag60_six_roi_load_60s/acceptance/acceptance.md)及[复轮原报告](diag60_repeat_60s/acceptance/acceptance.md)并列；各自的 `diagnostic.json`、`timing_metrics.json`、[只读审计](verify_roi_20260921.py)和[首轮冻结汇总](roi_first_summary_20260921.json)/[复轮冻结汇总](roi_repeat_summary_20260921.json)可复核。两轮同SHA二进制、相同源/模型/config/native/主机和原门槛，仍是四段内容复用的本地六路MJPEG，不是真实六摄像机RTSP。

每轮10秒预热+正式60.004秒、61快照/69组全生命周期指标内存样本；111条选定全局累计单调，退流队列0，服务/driver退出0。首轮原门槛PASS：output25.415～28.215FPS、公平9.923%、正式RSS -4.501%；复轮原门槛FAIL：第6路output24.882FPS、公平10.384%、正式RSS +7.224%。两轮低/高模刷新率、P95和CPU均过门槛；不能以首轮PASS标记六路容量稳定验收。

全生命周期有效ROI数126416/128025、ROI像素量1372787285/1375692353、采样角点数2019373/2047747。第1、5路每ROI约2.9～3.0万像素且ROI角点阶段约9ms；第2、6路约15个ROI/帧、双向LK约15～16.5ms。ROI重叠像素分别计入；跨路工作量和FPS相关性不证明根因，下一步保持跟踪质量做受控候选及独立精度/重复稳态。

```bash
conda run -n yolo python docs/test-results/multistream_local_20260921/verify_roi_20260921.py
conda run -n yolo python docs/test-results/multistream_local_20260921/verify_roi_20260921.py diag60_repeat_60s
```

## 第61节：完全重复角点LK候选被实测否决（2026-09-21）

详见[实施记录第61节](../../reports/多路实时监测实施记录_20260905.md)。受控A1/B1/A2/B2各六路10秒预热+60秒正式窗，原门槛结果依次FAIL/FAIL/PASS/FAIL；候选真实点重复率仅4.668%/4.849%，弱光流与双向LK均值无稳定降低。候选[源码补丁](lk_dedup_candidate_20260921.diff)保留供复核，但运行代码已撤回，最终OpenVINO服务SHA与A组基线相同。另留固定重叠ROI数值回归；其通过不能代替业务真值精度。

| 组 | 原报告 | 冻结审计 | output FPS范围 / 公平差异 |
|---|---|---|---:|
| A1基线 | [FAIL](diag61_A1_baseline_60s/acceptance/acceptance.md) | [JSON](lk_a1_summary_20260921.json) | 24.682～28.748 / 14.145% |
| B1候选 | [FAIL](diag61_B1_dedup_60s/acceptance/acceptance.md) | [JSON](lk_b1_summary_20260921.json) | 24.449～27.448 / 10.929% |
| A2基线 | [PASS](diag61_A2_baseline_60s/acceptance/acceptance.md) | [JSON](lk_a2_summary_20260921.json) | 25.932～28.565 / 9.218% |
| B2候选 | [FAIL](diag61_B2_dedup_60s/acceptance/acceptance.md) | [JSON](lk_b2_summary_20260921.json) | 24.365～28.365 / 14.101% |

每组另有 `diagnostic.json`、`diagnostic-config.yaml` 和 `timing_metrics.json`；基线111/候选112条选定全局序列单调，模型/源/config/runtime/主机/退出均由[固定只读审计](verify_lk_ab_20260921.py)检查。四次窗口不是随机化质量试验或真实RTSP/长测，原第60节证据不变。

```bash
for case in diag61_A1_baseline_60s diag61_B1_dedup_60s diag61_A2_baseline_60s diag61_B2_dedup_60s; do
  conda run -n yolo python docs/test-results/multistream_local_20260921/verify_lk_ab_20260921.py "$case"
done
```

## 第62节：跨帧金字塔缓存（2026-09-21～22）

详见[实施记录第62节](../../reports/多路实时监测实施记录_20260905.md)。已接受帧的灰度/导数金字塔供下一帧LK复用，缓存随灰度提交；无缓存原调用保留作数值对照。完整Release/OpenVINO CTest25/25（55.58/60.29s），保留实现。

| 组 | 原报告 | 冻结审计 | output FPS / 公平% | weak_flow ms / RSS峰KiB |
|---|---|---|---:|---:|
| A1基线 | [FAIL](diag62_A1_baseline_60s/acceptance/acceptance.md) | [JSON](temporal_a1_summary_20260921.json) | 25.148～27.964 / 10.072 | 23.015 / 844096 |
| B1缓存 | [PASS](diag62_B1_cache_60s/acceptance/acceptance.md) | [JSON](temporal_b1_summary_20260921.json) | 26.398～29.165 / 9.486 | 19.284 / 926452 |
| A2基线 | [FAIL](diag62_A2_baseline_60s/acceptance/acceptance.md) | [JSON](temporal_a2_summary_20260921.json) | 24.365～27.914 / 12.716 | 22.817 / 843600 |
| B2缓存 | [PASS](diag62_B2_cache_60s/acceptance/acceptance.md) | [JSON](temporal_b2_summary_20260921.json) | 26.715～28.732 / 7.019 | 19.033 / 918636 |

同第61节profile/源/模型/native设置，四组各10秒预热+60秒正式窗、61快照、69组全生命周期指标/内存、111条选定累计单调；原门槛不变，driver/服务退出0、collector2/0/2/0。各目录另含 `diagnostic.json`、`diagnostic-config.yaml`、`timing_metrics.json`。[只读审计](verify_temporal_pyramids_20260921.py)核对SHA、配置/源/模型/主机、累计/退流及原结果；原驱动为[有界binary驱动](../multistream_local_20260914/diagnostic_driver_binary_20260914.py)。

候选两轮weak_flow均值下降约16%，RSS峰值增加约73～80MiB；数据是不同顺序窗口的观察值，不能证明普适加速、8小时内存稳定或业务真值质量。输入仍为四段内容复用的六条本地MJPEG连接，非六摄像机RTSP。

```bash
for case in diag62_A1_baseline_60s diag62_B1_cache_60s diag62_A2_baseline_60s diag62_B2_cache_60s; do
  conda run -n yolo python docs/test-results/multistream_local_20260921/verify_temporal_pyramids_20260921.py "$case"
done
```
