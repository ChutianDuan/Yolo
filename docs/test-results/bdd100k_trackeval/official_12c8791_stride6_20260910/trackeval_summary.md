# BDD100K TrackEval 官方指标

- TrackEval commit：`12c8791b303e0a0b50f753af204249e622d0281a`
- 视频数：3
- 模式数：5
- 汇总口径：TrackEval `cls_comb_det_av`，数值单位为百分比。

| run | HOTA | DetA | AssA | LocA | MOTA | IDF1 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| full_high | 58.577 | 55.314 | 62.993 | 83.649 | 56.160 | 66.876 |
| high_dynamic_flow | 36.735 | 29.924 | 46.544 | 74.105 | -1.147 | 41.894 |
| high_fixed_flow | 34.887 | 28.612 | 43.918 | 74.009 | -3.171 | 40.713 |
| low_dynamic_flow | 51.429 | 45.308 | 59.598 | 79.277 | 40.351 | 62.927 |
| integrated_high_low_flow | 46.148 | 39.260 | 55.074 | 83.395 | 3.586 | 54.173 |

## 口径限制

- 导出保留 BDD100K crowd 和 distractor 真值，由 TrackEval 官方 BDD100K adapter 执行忽略规则。
- C++ 的 person/bike/motor 分别映射回 pedestrian/bicycle/motorcycle；模型不能区分 pedestrian 与 rider。
- 这里只评估既有 C++ 响应保留下来的轨迹，不包含当前阈值以下候选，也不是业务摄像机验收结果。
