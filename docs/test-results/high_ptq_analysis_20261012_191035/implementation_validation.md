# 实现验证记录

本实验只执行PTQ校准、推理和误差分析，没有训练或权重更新。

## 小型回归

```bash
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=8 CUDA_VISIBLE_DEVICES=4,5 \
conda run --no-capture-output -n yolo python -m unittest \
  model.test_ptq_analysis model.test_quantization
```

2026-10-12执行，13项测试通过。覆盖缓存/公共量化路径一致性、共享分支排除、公共PTQ接口在预处理后的排除行为、INT8数量及FP32权重检查、未知节点拒绝、已有输出及完成目录恢复拒绝、非有限输出拒绝、流式误差统计、类别/IoU检测匹配、去重后的精度统计与现有评测器在所有IoU点和crowd场景下逐项一致，以及现有量化接口回归。原有QAT单元测试仅用小型随机结构检查接口兼容性，没有对本实验模型做QAT或训练。

## 正式模型一致性

- opset12转13及预处理前后：3张检查图最大输出绝对差均为0，见`parity.json`。
- 缓存量化与公共`quantize_static`：全部initializer逐元素相同，3张检查图输出逐元素相同，见`cache_parity.json`。
- 初次公共校准使用默认线程数，与8线程缓存校准产生约1.1e-8的激活scale差异，一致性检查主动中止，未进入消融。将公共校准也限定为8线程后重新检查并通过；通过局部调用上下文控制线程，没有修改全局安装环境。
- 中止前的数据清单、原始/转换模型及校准统计通过源模型、数据YAML、图片与标签SHA-256核对后恢复使用。

## 运行边界

只使用`yolo` Conda环境和本地模型、图片、标签及视频。未下载模型或数据，未修改部署模型、服务配置、数据集和已有历史结果。诊断使用关闭图优化的ORT CPU会话，正式精度及延迟使用OpenVINO CPU，二者的数值误差可能不同，最终判断以OpenVINO实测为准。
