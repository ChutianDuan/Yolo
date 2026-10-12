# PTQ INT8 / QAT INT8

用于对比同一份本地 YOLO 检测权重的 PTQ 与 QAT。脚本使用现有 `yolo` Conda 环境，不需要新增依赖，不下载权重，也不修改部署模型。输出目录必须不存在。

| 项目 | PTQ | QAT |
| --- | --- | --- |
| 脚本 | `model/ptq_int8.py` | `model/qat_int8.py` |
| 权重更新 | 不更新 | 使用原生 YOLO 检测损失微调 |
| 量化参数 | 校准图像的 MinMax 统计 | 校准初始化后，在训练中观察范围，并按 epoch 冻结 |
| 训练计算 | 无 | FP32 + 伪量化、梯度更新，关闭 AMP |
| 导出 | `ptq_int8.onnx` | `qat_int8.onnx`，保留训练中的量化参数 |

两个脚本使用相同的量化范围：Conv 输入 U8、权重按输出通道对称 S8；卷积输出、bias、模型输入输出保留 FP32，其他算子保持浮点。输出为静态 batch=1、opset 13 的 QDQ ONNX，供现有 OpenVINO / ONNX Runtime 流程使用。QAT 不在导出后再次运行 PTQ；权重 QDQ 会折叠为实际 INT8 initializer，而不是把浮点权重连同伪量化保存成所谓 INT8 模型。

QDQ 及直接运行 QAT 导出模型的说明见 [ONNX Runtime 官方量化文档](https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html)。算子能否被后端融合成 INT8 内核仍需实际测量，文件中的 QDQ 不保证整条网络都以整数计算。

## 运行

从仓库根目录运行。`--pt` 必须是已经训练好的本地检测权重，类别顺序与 `--data` 一致。以下路径需要替换；两次使用相同的权重、数据、校准 split、图像数量与输入尺寸。

```bash
conda run -n yolo python model/ptq_int8.py \
  --pt /path/to/local/best.pt \
  --data /path/to/local/data.yaml \
  --imgsz 384 640 \
  --calib-split train --calib-limit 200 \
  --output-dir /path/to/new-ptq-output

CUDA_VISIBLE_DEVICES=4,5 conda run -n yolo python model/qat_int8.py \
  --pt /path/to/local/best.pt \
  --data /path/to/local/data.yaml \
  --imgsz 384 640 \
  --calib-split train --calib-limit 200 \
  --epochs 10 --freeze-observer-epoch 3 \
  --batch 8 --lr 0.0001 --device auto \
  --output-dir /path/to/new-qat-output
```

`--imgsz` 顺序是高、宽；高分辨率模型使用 `736 1280`。校准图片按路径排序选取，并写入清单。默认从训练集校准，验证或测试数据留给后续评测；定向选择校准集时，可在 YAML 中使用图片路径列表。

QAT 的 `--freeze-observer-epoch` 从 0 计数；示例在第 4 个 epoch 起冻结 observer，伪量化仍开启。`--device auto` 使用第一张可见 GPU，无 CUDA 时回退 CPU；可显式使用 `cpu` 或一个可见 GPU 编号，目前不支持多卡。降低 batch 可减少显存占用。

QAT 在微调前折叠 Conv/BN，保留 YOLO26 的 one2many / one2one 训练分支；使用当前模型权重验证、选择 best，不对量化参数做 EMA。训练使用 Ultralytics 的单值最长边 `imgsz` 和矩形 batch，导出使用给定的固定高、宽；二者不保证每个训练 batch 都具有导出尺寸。训练与校准都以 YAML 所在目录解析相对 `path`，缺少本地 train/val 路径时直接报错，不调用数据集自动下载。训练流程可能生成数据集标签缓存。

## 产物与对比

```text
PTQ 输出目录                 QAT 输出目录
  fp32.onnx                    fp32.onnx
  ptq_int8.onnx                 qat_int8.onnx
  classes.json                 classes.json
  calibration_images.txt       calibration_images.txt
  quantization.json            quantization.json
                               train/weights/best.pt
                               train/weights/last.pt
```

两份 `fp32.onnx` 均来自微调前的同一份源权重。`quantization.json` 保存源权重、数据 YAML 与校准图片内容哈希，以及输入输出 shape、量化卷积数量、参数、依赖版本及 ONNX 文件大小/哈希。QAT 的 `.pt` 保存 FP32 权重和伪量化参数，是训练检查点，不是原生 PyTorch INT8 推理模型；重新加载需要本仓库的 `model.quantization` 模块。

对比时先检查源权重哈希、类别、输入尺寸及校准清单一致，再用同一验证集、预处理、阈值与后处理测量精度。延迟、吞吐与内存需使用相同后端、线程数、请求池、预热次数和输入。QAT 增加了微调训练成本，精度提升需要评测，不能预设一定优于 PTQ。

已有精度评测器可通过 `--help` 查看模型路径等参数：

```bash
conda run -n yolo python yolo_onnx_cpp/tools/evaluate_bdd100k_openvino_quantization.py --help
```

## 小输入验证

```bash
CUDA_VISIBLE_DEVICES=4,5 conda run -n yolo python model/test_quantization.py
conda run -n yolo python model/ptq_int8.py --help
conda run -n yolo python model/qat_int8.py --help
```

测试使用小张量、临时图像和随机初始化的内置 YOLO26 结构，检查梯度、observer 冻结、训练头保留、QDQ 导出及 INT8 权重打包，不下载模型或运行完整训练。

## PTQ 敏感层排查（不训练）

`model/ptq_analysis.py` 针对当前部署高模型（736x1280、YOLO26 TopK 输出）执行逐层诊断、单层/组合排除、独立验证与中文报告。只使用 PTQ，不运行 QAT，不更新源权重或部署配置。所有产物写入新的输出目录，已存在的目录直接拒绝。

```bash
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=8 CUDA_VISIBLE_DEVICES=4,5 \
conda run --no-capture-output -n yolo python model/ptq_analysis.py \
  --source yolo_onnx_cpp/deploy/best.onnx \
  --data model/data/bdd100k_yolo_det/data.yaml \
  --classes yolo_onnx_cpp/deploy/classes.json \
  --manifest docs/test-results/bdd100k_long_tail/gt_3videos_stride6_20260911/manifest.json \
  --output-dir /path/to/new-output

conda run -n yolo python -m unittest model.test_ptq_analysis
```

固定种子42：训练集200张校准、另外32张诊断；验证集256张调优、另外1000张评测；历史三段视频仅用于最终复测。逐层报告分别记录权重误差、累计输入/输出误差及同图Q前/Q后局部误差。TopK前按张量对齐，最终框按类别和IoU匹配，不能直接比较TopK输出行号。

排除候选来自累计/局部误差各前10层和六个末端检测头Conv；先逐层消融，再测试正收益层的累计1/2/4/8层组合。候选共享一次MinMax统计；缓存量化路径使用当前ORT 1.23.2的QDQQuantizer，并在正式分析前与公共quantize_static验证一致性。更换ORT版本后应重新运行小型回归及一致性检查。

最终选择在调优集提高mAP50-95且保留至少80%推理时间节省的候选，最终验证集不参与选择。三组OpenVINO CPU使用8线程、单请求、显式FP32浮点精度，独立进程预热后轮换执行三轮；未满足目标时如实报告。输出包括report.md、summary.json、metrics.csv、layer_errors.json/CSV、ablations.json/CSV、数据哈希清单、模型和对照图。

运行中断时，用同一`--output-dir`加上`--resume`恢复。恢复前核对源模型、数据YAML、图片和标签哈希；已完成的推理任务还会核对模型哈希和任务参数。完成报告的目录拒绝恢复。默认不允许使用已存在的输出目录。

单独PTQ导出时可重复使用`--exclude-node`传入预处理图中Conv的精确节点名。默认不排除任何层。显式排除的Conv保留FP32权重及浮点计算，但共享输入QDQ和上游量化误差仍可能存在，quantization.json中会列出共享输入情况。
