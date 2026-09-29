# 2026-09-28 节点0线程对照

[旧16/16基线](diag69_threads16_16_before_audit.json)：原FAIL，仅公平越界；13正式快照/25持久化样本/111累计、1490线程范围观察、退流全0。原窗口于2026-09-27 23:45 UTC结束，与新候选有约80分钟间隔，后续补同日基线回测。

实施范围、每组完成结果与剩余边界见[实施记录第69节](../../reports/多路实时监测实施记录_20260905.md)。新16/8、8/8与16/16回测仍待完成，不能提前判断候选通过。

[16/8审计](diag69_threads16_8_audit.json)：原FAIL，四路low不足4FPS；累计/亲和性/退流核验通过。[四组只读入口](verify_node0_threads_20260928.py)已语法检查，8/8和回测未结束前不执行完整汇总。

[8/8审计](diag69_threads8_8_audit.json)：原FAIL，四路low不足4FPS，CPU26.494%host；累计/亲和性/退流通过。同日16/16回测已启动，完整四组汇总待完成。

## 第69节完整收尾

[同日16/16回测](diag69_threads16_16_after_audit.json)、[四组汇总](diag69_thread_matrix_summary.json)：四组原性能均FAIL，13正式快照/25持久化样本/111累计和退流均核验。只有配置的高/低线程不同；实际低模8线程时native streams4，而应用池仍2，HT均YES。跨会话时间间隔保留，上述“待完成”条目为当时历史状态。

只读复核独立运行与冻结JSON逐字段一致：

```bash
conda run -n yolo python docs/test-results/multistream_local_20260928/verify_node0_threads_20260928.py
```

下一项保持8/8，仅把低模应用池2改为4做有界对照。未改生产代码或模型，不能以低CPU/短窗代替检测刷新或长期验收。

## 第70节低模请求池对照

[只读四组入口](verify_node0_pool_20260928.py)、[B1池4](diag70_pool4_b1_audit.json)：原FAIL，仅一路high0.483265不足0.5；低模恢复4.183～4.649FPS，累计/亲和性/退流核验通过。A1复用第69节8/8池2，A2与B2待完成；不隐去时间间隔。

[A2池2审计](diag70_pool2_a2_audit.json)：原FAIL再次为四路low不足4；累计/线程范围/退流通过，RSS+4.829%仅属短窗。B2池4复测待完成。

## 第70节四组终态

[B2池4](diag70_pool4_b2_audit.json)、[完整池对照](diag70_pool_matrix_summary.json)：A1/B1/A2/B2为FAIL/FAIL/FAIL/PASS；B2单次PASS不覆盖B1高模FAIL。四组13/25样本、111累计、线程范围/退流与指纹审计通过，独立复跑逐字段一致。实际native1/4保持，仅低模应用池和对应hint不同。上文待完成描述为历史时点。

```bash
conda run -n yolo python docs/test-results/multistream_local_20260928/verify_node0_pool_20260928.py
```

下一项保持low8/池4，仅high8→16争取高模余量，尚不验收双worker或八路长期。

## 第71节高模线程对照

[C1高模16](diag71_high16_c1_audit.json)：原FAIL为两路low不足4，高模1.083～1.466；累计/亲和性/退流通过。[只读四窗口审计](verify_high_threads_20260928.py)语法通过；旧高模8窗口与本轮相隔约五小时，已补当前控制，C2及完整审计待完成。

[当前high8控制](diag71_high8_control_audit.json)：原FAIL仅RSS+5.270756%，各刷新门槛通过；累计/线程范围/退流核验通过，C2待完成。

## 第71节终态

[C2](diag71_high16_c2_audit.json)仍原FAIL两路low不足4；[完整汇总](diag71_high_thread_summary.json)独立复跑一致，实际runtime仅高模线程不同。high16两轮低模失败、当前high8控制RSS失败均保留；所有累计/亲和性/清理核验通过，8080释放。下一项high12中间值三窗重复，旧源码证据不变。

## 第72节high12中间值

[三窗审计入口](verify_high12_20260928.py)语法通过；driver仅增high12选项，SHA256 `e35048f779c8c7534956524ad3f4615599f1e8c80e2e4f25378ab4c5d86704c6`。12→8→12均使用新驱动、同门槛/模型，A1启动，结果待核验。

[A1 high12审计](diag72_high12_a1_audit.json)：原FAIL两路low不足4及RSS+8.586%；运行时线程12/8/native1/4、累计/亲和性/退流核验通过。high8控制和A2待完成。

[high8控制](diag72_high8_control_audit.json)本次原PASS，但high最小0.516614余量小；累计/亲和性/清理通过。旧高模/RSS失败不覆盖，high12 A2待完成。

## 第72节终态

[A2 high12](diag72_high12_a2_audit.json)原FAIL，一路low3.949529829<4；[三组汇总](diag72_high12_summary.json)为FAIL/PASS/FAIL，独立复跑一致。仅配置及实际高模线程不同，其余输入/模型/源码/阈值一致，111累计/线程范围/退流核验通过。high12未形成稳定候选，high8单次PASS不覆盖旧失败。上文“待完成”为历史进度。

```bash
conda run -n yolo python docs/test-results/multistream_local_20260928/verify_high12_20260928.py
```

驱动high12选项语法、帮助和真实执行验证通过；下一项分析已有逐流阶段、过期和调度证据，再确定局部候选。未修改生产C++/模型/配置，不将本地短窗等同长期或生产验收。

## 第73节逐流损失核算

[分析入口](analyze_stream_losses_20260928.py)、[六窗口冻结结果](diag73_stream_loss_summary.json)、[五项离线检查](test_stream_loss_analysis_20260928.py)。复用约51秒近似后窗，不覆盖正式60秒原验收；low完成均超4FPS、平均排队不足0.1ms，过期以回放前为主。已应用/已发布及端点差额分别保留，守恒检查和独立复跑通过。初版错误等同两类计数的断言已纠正，原数据不变。

```bash
conda run -n yolo python docs/test-results/multistream_local_20260928/analyze_stream_losses_20260928.py
conda run -n yolo python docs/test-results/multistream_local_20260928/test_stream_loss_analysis_20260928.py
```

下一项high8/低池4固定，仅low8→16验证完成/过期变化。当前未形成稳定节点候选。

## 第74节低模线程对照

[两窗审计](verify_low_threads_20260928.py)复用逐流损失核算，语法通过；固定high8/低池4，仅low16/8变化，先候选再当前控制。结果待核验，旧实验不变。

[low16/池4候选](diag74_low16_pool4_audit.json)：原FAIL四路high均不足0.5，low恢复4.933～4.999；native实际保持4，旧池2/native2不适用。累计/亲和性/退流通过，当前控制待完成。

## 第74节终态

[low8控制审计](diag74_low8_control_audit.json)原FAIL，一路high0.499940789<0.5；[完整两窗](diag74_low_thread_summary.json)独立复跑一致。仅配置及实际低模线程不同、native均4。low16减少低模过期到3并恢复low刷新，但高模四路仍FAIL；两组累计/线程范围/退流核验通过。下一项固定low16/池4，仅high8→12。

```bash
conda run -n yolo python docs/test-results/multistream_local_20260928/verify_low_threads_20260928.py
```

## 第75节high8/12、固定low16

[四窗审计](verify_balanced_threads_20260928.py)语法通过；复用第74节high8/low16前基线，B1 high12已启动，随后high8控制与B2。模型/源码/原门槛保持，终态待核验。
