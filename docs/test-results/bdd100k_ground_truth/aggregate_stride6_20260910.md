# BDD100K 人工真值三视频汇总

- 样本：`0000f77c-6257be58`、`00268999-cb063914`、`012fdff1-9d1d0d1d`
- C++ 输出：2026-07-10 保存的同批次五模式 `/infer_video` 完整帧响应
- 真值：`datasets/bdd100k_tracking_video/mot_labels.csv`
- 对齐：BDD 5 FPS 标注帧 `n` 映射到 C++ 原视频帧 `n * 6`
- 匹配：class-aware IoU >= 0.5，crowd 默认忽略
- 覆盖：604 个标注帧、4099 个真值框；两段源视频各缺最后 1 个目标原视频帧，共排除 2 个尾部标注帧

## Pooled 指标

| run | GT | pred | TP | P | R | F1 | video-macro mAP50 | MOTA | IDF1 | IDSW | fragments |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 4099 | 3930 | 3121 | 0.7941 | 0.7614 | 0.7774 | 0.5467 | 0.5494 | 0.6643 | 60 | 102 |
| high_dynamic_flow | 4099 | 3595 | 1775 | 0.4937 | 0.4330 | 0.4614 | 0.1654 | -0.0185 | 0.4172 | 31 | 80 |
| high_fixed_flow | 4099 | 3568 | 1719 | 0.4818 | 0.4194 | 0.4484 | 0.1671 | -0.0412 | 0.4049 | 39 | 84 |
| low_dynamic_flow | 4099 | 3555 | 2607 | 0.7333 | 0.6360 | 0.6812 | 0.3757 | 0.3928 | 0.6258 | 49 | 117 |
| integrated_high_low_flow | 4099 | 6507 | 3300 | 0.5071 | 0.8051 | 0.6223 | 0.5392 | 0.0059 | 0.5342 | 69 | 87 |

P/R/F1、MOTA 和 IDF1 由三份 JSON 的计数池化；`video-macro mAP50` 是三段视频各自 mAP50 的算术平均，不冒充把全部预测重新排序后计算的全局 AP。

## 单视频证据

- [0000f77c-6257be58](0000f77c-6257be58_stride6_20260910/bdd100k_ground_truth_evaluation.md)
- [00268999-cb063914](00268999-cb063914_stride6_20260910/bdd100k_ground_truth_evaluation.md)
- [012fdff1-9d1d0d1d](012fdff1-9d1d0d1d_stride6_20260910/bdd100k_ground_truth_evaluation.md)

## 结论边界

- `integrated_high_low_flow` 在这三段上召回最高（0.8051），但预测数和假阳性明显增加，precision、MOTA、IDF1 均低于 `full_high`；不能仅凭原伪标签 F1 决定部署模式。
- `low_dynamic_flow` 的 precision/IDF1 明显好于另两种单高模稀疏检测 + flow，但召回仍低于集成模式。
- 三段只有 car 和少量 truck 真值，不能据此标定 8 类阈值；traffic light/sign 不在 BDD MOT 标注范围，已排除而非计为假阳性。
- AP50 只能在 C++ 响应已保留的分数范围内排序，无法恢复模型/NMS/跟踪器早先删除的候选；这批结果不构成低于当前阈值的 PR 扫描。
- MOTA/IDF1 是本工具的可复现实现，不是官方 TrackEval 输出；HOTA 尚未报告。生产验收仍需业务机位人工标注、官方指标复核和开启 ROI 后的新推理结果。
