# BDD100K 连续性指标汇总（frame stride 6）

本报告聚合 3 段 BDD100K MOT 人工真值、604 个标注帧和 4099 个真值框。检测与身份指标沿用 `aggregate_stride6_20260910.md`；本轮新增连续漏检持续时间和定位抖动。每个标注帧映射到第 6 个 C++ 原视频帧，时间按标注 5 FPS（每帧 200 ms）换算。

| 模式 | fragments | miss events | missed GT | mean miss ms | video-macro p95 ms | max miss ms | jitter samples | pooled mean normalized jitter | video-macro p95 normalized jitter |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 87 | 303 | 978 | 645.5 | 1663.3 | 9600 | 2901 | 0.02595 | 0.07662 |
| high_dynamic_flow | 77 | 322 | 2323 | 1442.9 | 4703.3 | 10400 | 1625 | 0.02649 | 0.08825 |
| high_fixed_flow | 82 | 332 | 2380 | 1433.7 | 4553.3 | 10400 | 1560 | 0.02459 | 0.07932 |
| low_dynamic_flow | 117 | 385 | 1492 | 775.1 | 2383.3 | 7600 | 2379 | 0.02676 | 0.08158 |
| integrated_high_low_flow | 82 | 278 | 799 | 574.8 | 1633.3 | 6800 | 3045 | 0.03350 | 0.10553 |

## 口径

- `miss events` 是真值连续可见期间的连续漏配段；真值自身不连续可见会结束当前段，不把标注可见性缺口算作模型漏检。
- `missed GT` 等于同一模式三段报告中的 false negatives 总和。`mean miss ms` 用全部漏检观测数除以全部事件数后按 5 FPS 换算，是可池化指标。
- 报告没有保存逐事件原始序列，因此 `video-macro p95 ms` 是三段各自 P95 的算术平均，不是全部事件合并后的全局 P95；最大值取三段最大值。
- `fragments` 只在同一真值轨迹连续可见、曾匹配、随后漏配并再次匹配时计数。该口径修正了旧报告把真值可见性断点误算成碎片的问题，因此旧汇总中的 fragments 已被本报告取代；AP50、P/R/F1、MOTA、IDF1 和 IDSW 不受影响。
- `normalized jitter` 是同一预测 ID 连续匹配同一真值轨迹时，相邻标注帧定位中心误差向量的变化量除以当前真值框对角线。均值按全部样本及其未四舍五入前的和池化；P95 同样是三段各自 P95 的算术平均。

## 逐视频证据

- `0000f77c-6257be58_stride6_continuity_20260910/`
- `00268999-cb063914_stride6_continuity_20260910/`
- `012fdff1-9d1d0d1d_stride6_continuity_20260910/`

## 结论边界

集成 High+Low+Flow 在这三段上同时得到最少漏检观测、最短平均连续漏检和最高召回，但其 pooled mean normalized jitter 为 0.03350，高于 full-high 的 0.02595；结合旧汇总中较低的 precision、MOTA 和 IDF1，不能仅凭召回或漏检持续时间决定部署模式。样本仍以 car 为主，且不是业务摄像机；HOTA、官方 TrackEval 复核及真实机位标定仍待完成。
