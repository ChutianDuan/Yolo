"""Render measured experiment data as a Chinese report and compact CSV."""
from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError("Refusing to overwrite report")
    data = json.loads(args.summary.read_text())
    summaries = data["summaries"]
    fp, quant = summaries["fp32"], summaries["int8"]
    accuracy = data["accuracy"]
    rows = []

    def row(label: str, a: float, b: float, unit: str) -> str:
        delta = (b/a-1)*100 if a else float("nan")
        rows.append([label, a, b, unit, delta])
        return f"| {label} | {a:.3f} {unit} | {b:.3f} {unit} | {delta:+.2f}% |"

    stage_rows = []
    for label, key in [("模型推理平均", "infer_ms"), ("预处理平均", "preprocess_ms"), ("后处理平均", "postprocess_ms")]:
        stage_rows.append(row(label, fp["stages"][key]["mean"], quant["stages"][key]["mean"], "ms/调用"))
        stage_rows.append(row(label.replace("平均", "P95"), fp["stages"][key]["p95"], quant["stages"][key]["p95"], "ms/调用"))
    stage_rows.append(row("模型文件大小", data["models"]["fp32"]["size_bytes"]/1e6, data["models"]["int8"]["size_bytes"]/1e6, "MB"))
    for label, key, stat in [("运行期 RSS 平均", "running_mean_mib", "mean"),
                             ("运行期采样 RSS 最大值", "running_peak_mib", "max"),
                             ("预热后 RSS 平均", "after_warmup_mib", "mean"),
                             ("进程生命周期峰值最大值", "process_peak_mib", "max")]:
        stage_rows.append(row(label, fp["memory"][key][stat], quant["memory"][key][stat], "MiB"))
    elapsed_fp = statistics.mean(r["elapsed_ms"] for r in fp["rounds"])/1000
    elapsed_q = statistics.mean(r["elapsed_ms"] for r in quant["rounds"])/1000
    stage_rows.append(row("三段视频整链路总耗时平均", elapsed_fp, elapsed_q, "s/轮"))
    model_speedup = fp["stages"]["infer_ms"]["mean"]/quant["stages"]["infer_ms"]["mean"]
    pipeline_speedup = elapsed_fp/elapsed_q
    round_lines = []
    for left, right in zip(fp["rounds"], quant["rounds"]):
        round_lines.append(f"| {left['round']} | {left['model_calls']} / {right['model_calls']} | {left['infer_mean_ms']:.3f} / {right['infer_mean_ms']:.3f} | {left['elapsed_ms']/1000:.3f} / {right['elapsed_ms']/1000:.3f} | {left['lk_total_ms']/1000:.3f} / {right['lk_total_ms']/1000:.3f} |")
    quality_lines = []
    for label, key, threshold in [("模型单独检测 mAP50（候选下限 0.001）", "detector_ap", "0.001"),
                                  ("模型单独检测召回率（阈值 0.25）", "detector_ap", "0.250"),
                                  ("High＋LK 全部标注帧", "all_labeled_frames", "0.250"),
                                  ("High＋LK 检测标注帧", "detection_labeled_frames", "0.250"),
                                  ("High＋LK 补齐标注帧", "lk_labeled_frames", "0.250")]:
        left = accuracy["fp32"][key]["operating_points"][threshold]["pooled"]
        right = accuracy["int8"][key]["operating_points"][threshold]["pooled"]
        quality_lines.append(f"| {label} | {left['map50']*100:.3f} / {right['map50']*100:.3f} | {left['precision']*100:.3f} / {right['precision']*100:.3f} | {left['recall']*100:.3f} / {right['recall']*100:.3f} | {left['f1']*100:.3f} / {right['f1']*100:.3f} |")
    class_lines = []
    for class_name, left in accuracy["fp32"]["all_labeled_frames"]["operating_points"]["0.250"]["per_class"].items():
        right = accuracy["int8"]["all_labeled_frames"]["operating_points"]["0.250"]["per_class"][class_name]
        class_lines.append(f"| {class_name} | {left['ground_truth']} | {left['precision']*100:.2f} / {right['precision']*100:.2f} | {left['recall']*100:.2f} / {right['recall']*100:.2f} | {(right['recall']-left['recall'])*100:+.2f} |")
    all_fp = accuracy["fp32"]["all_labeled_frames"]["operating_points"]["0.250"]["pooled"]
    all_q = accuracy["int8"]["all_labeled_frames"]["operating_points"]["0.250"]["pooled"]
    detector_fp = accuracy["fp32"]["detector_ap"]["operating_points"]["0.001"]["pooled"]
    detector_q = accuracy["int8"]["detector_ap"]["operating_points"]["0.001"]["pooled"]
    row("模型单独检测 mAP50", detector_fp["map50"]*100, detector_q["map50"]*100, "%")
    row("模型单独检测 mAP50-95", accuracy["fp32"]["detector_map50_95"]*100,
        accuracy["int8"]["detector_map50_95"]*100, "%")
    for label, key in [("High+LK Precision", "precision"), ("High+LK Recall", "recall"), ("High+LK F1", "f1")]:
        row(label, all_fp[key]*100, all_q[key]*100, "%")
    native = fp["native_versions"]
    report = f"""# High FP32＋LK 与 High INT8＋LK 实验报告

生成时间：{data['generated_at']}（Asia/Shanghai）。本次重新执行实验，下面均为本次实测。

## 结论

- 模型推理加速 **{model_speedup:.3f} 倍**；三段视频整链路加速 **{pipeline_speedup:.3f} 倍**。两者不能混用。
- 模型文件由 **{data['models']['fp32']['size_bytes']/1e6:.3f} MB** 缩到 **{data['models']['int8']['size_bytes']/1e6:.3f} MB**。
- 运行期平均 RSS：**{fp['memory']['running_mean_mib']['mean']:.3f} → {quant['memory']['running_mean_mib']['mean']:.3f} MiB**，这是进程实际驻留内存。
- 模型单独检测 mAP50：**{detector_fp['map50']*100:.3f}% → {detector_q['map50']*100:.3f}%**，变化 **{(detector_q['map50']-detector_fp['map50'])*100:+.3f} 个百分点**。
- High＋LK 全部标注帧召回率：**{all_fp['recall']*100:.3f}% → {all_q['recall']*100:.3f}%**，变化 **{(all_q['recall']-all_fp['recall'])*100:+.3f} 个百分点**。

## 实验条件

- 单一 High 模型：输入 `[1,3,736,1280]`；没有 Low 模型、ROI、异步队列或动态检测间隔。
- 两组均使用项目原生 C++ `YoloEngine`、`inferVideoFile`、`StreamProcessor`、ByteTracker 和 LK 光流。检测每 8 帧调用一次（视频约 30 FPS，相当于约 3.75 FPS）；中间帧由 LK 补齐。这里的跟踪器是现有单模型路径的一部分。
- 后端 OpenVINO CPU，8 模型线程，1 request/stream，LATENCY，CPU pinning=false；OpenCV 4 线程。C++ OpenVINO `{native['openvino_version']}`、OpenCV `{native['opencv_version']}`，全部使用 `/root/vcpkg` GCC15 工具链。
- 每个模型、每段视频、每一轮都使用新的独立进程，只加载一个模型；每次先做 10 次模型预热。三轮每段视频按 FP32→INT8、INT8→FP32、FP32→INT8 顺序串行运行。
- 复用已有三段 BDD100K 长尾标注视频：总标注帧 **{data['inputs']['label_frames']}**，八类普通真值框 **{data['inputs']['ground_truth_boxes']}**；标注帧 n 对齐源帧 n×6。视频原文件直接读取，正式实验不转码。
- High FP32 来自当前部署 `best.onnx`，为逐通道量化把 opset 12 转为 13，不重新训练、不重导出权重。三张校验图上转换前后 ORT 输出最大绝对差均为 0。
- INT8 是同一高模型的 Conv-only QDQ PTQ：U8 激活、S8 逐通道权重、MinMax；使用本地 validation split 排序后的 200 张图校准，保留浮点模型输入和输出。
- 两组整链路置信度 0.25、外部 class-aware NMS IoU 0.45；精度匹配 IoU 0.50。模型 AP 单独运行候选下限 0.001，避免 0.25 截断掩盖分数分布变化。

## 六项指标及整链路耗时

阶段统计合并三轮所有 High 调用，每组 **{fp['stages']['infer_ms']['n']} 次**。平均与 P95 均为每次模型调用，未除以全部视频帧。

| 指标 | High FP32＋LK | High INT8＋LK | INT8 相对变化 |
|---|---:|---:|---:|
{chr(10).join(stage_rows)}

计时口径：预处理包含 letterbox、输入缓冲区分配、BGR→RGB、HWC→CHW、归一化；模型时间只覆盖 OpenVINO `infer()`；后处理包含输出 shape 获取、输出解码、坐标还原、外部 NMS。LK、ByteTracker、视频解码、输入 tensor 复制单独存在于整链路时间中，没有混入模型后处理。High 模型实际是 YOLO26 end-to-end TopK 输出 `[1,300,6]`，原图没有 `NonMaxSuppression` 算子，不能把 TopK 输出误写成内置 NMS。

RSS 每 10 ms 读取 `/proc/self/statm`；运行期平均值先求每个独立进程均值，再平均 9 次运行。运行期采样在推理结束、JSON 序列化之前停止，包含模型、输入 tensor、视频解码、LK 金字塔、跟踪和生产入口保留的逐帧结果。生命周期峰值取 Linux `ru_maxrss`，还包括加载/编译/预热和结果序列化，不能视作模型独占内存。MB=10^6 bytes，MiB=2^20 bytes。

两组预处理使用同一份 C++ 实现，生成的输入仍然是 float32；本次没有优化 OpenCV 或像素转换循环。预处理的小幅差异不能当作 INT8 对预处理的直接加速证据。后处理代码同样未变，但量化后的有效框数可能变化。文件大小主要反映权重存储；进程 RSS 还包含浮点输入、运行时工作区、解码和 LK，因此文件缩减比例不能套用到运行内存。

## 三轮重复结果

表内双值均为 FP32 / INT8。整链路时间排除模型加载、编译、预热和 JSON 序列化，包含三段视频读取/解码。

| 轮次 | High 调用次数 | 模型平均 ms/调用 | 三视频总耗时 s | LK 累计 s |
|---|---:|---:|---:|---:|
{chr(10).join(round_lines)}

这是三轮配对观测，没有做统计显著性检验；收益不能直接外推到不同后端、线程数、低分辨率或多路 RTSP 服务。

## 精度

下表双值为 FP32 / INT8，单位 %。整链路结果评估的是跟踪输出框：包括模型检测帧和 LK 补齐帧，不是只测模型输出。

| 评测范围 | mAP50 / 截断 AP50 | Precision | Recall | F1 |
|---|---:|---:|---:|---:|
{chr(10).join(quality_lines)}

只有候选下限 0.001 的模型单独检测行表示完整候选范围的 mAP50；0.25 行及 High＋LK 行的 AP 是阈值截断后的 AP，不能当作完整 mAP。模型单独检测在 IoU 0.50:0.05:0.95 上的平均 mAP 为 **{accuracy['fp32']['detector_map50_95']*100:.3f}% / {accuracy['int8']['detector_map50_95']*100:.3f}%**。采用仓库 AP101 计算与 ignore-region 逻辑，每个 IoU 点都先匹配普通真值，再按该 IoU 阈值忽略未匹配且与同类 crowd / any-class distractor 重叠的预测；它是本项目评测口径，不是官方 COCO 全套评测。

全部 High＋LK 标注帧、固定阈值 0.25 的逐类结果：

| 类别 | 真值数 | Precision FP32 / INT8 % | Recall FP32 / INT8 % | Recall 变化百分点 |
|---|---:|---:|---:|---:|
{chr(10).join(class_lines)}

这是三段定向选择的 BDD100K training 视频，非独立随机 validation 或业务摄像机测试；两组使用完全相同的视频与真值，适合比较量化差异，但不能证明泛化或生产质量。traffic light / traffic sign 没有本轮真值。未进行逐类阈值调优；没有用 FP32 输出代替真值；没有完成官方 HOTA / IDF1 跟踪指标验收。

## 验证与复现

- 独立 C++ target 构建成功；现有量化精度评测器 9 项回归通过。
- 两组六帧 smoke 均产生正常检测，验证 1 次 High 调用＋5 帧 LK，计时与 RSS 样本有效。
- 每轮帧数、High 调用位置和调用次数都由驱动逐项验证一致；原始模型、视频、真值、源码及 runner 的 SHA-256 写入 JSON。
- 量化校准首次尝试因已安装 ORT 的 `CalibMaxIntermediateOutputs=1` 清空数据而失败，未生成 INT8；修正为默认收集行为后在新目录重新校准并成功。失败日志和中间 FP32 文件保留作审计，不参与对比。
- 没有改动部署模型、业务源码、配置、已有实验结果或全局环境；用户已有 `video_inference_detail.cpp` 修改被保留并参与构建。

核心复现文件：[prepare_int8.py](prepare_int8.py)、[CMakeLists.txt](CMakeLists.txt)、[runner.cpp](runner.cpp)、[run_experiment.py](run_experiment.py)、[write_report.py](write_report.py)。调用 CLI 使用 `--help` 查看路径参数，新输出目录必须不存在。构建参数和执行命令见 [reproduce.sh](reproduce.sh)，实际 24 次运行命令见 [measured/commands.json](measured/commands.json)。

数据：[完整摘要与精度 JSON](measured/summary.json)、[量化模型及校准清单](models_u8s8/quantization.json)、[指标 CSV](metrics.csv)、[实验日志](experiment.log)。每次运行的逐阶段样本、逐帧框和 RSS 采样均保存在 `measured/`，便于重新统计。
"""
    args.output.write_text(report, encoding="utf-8")
    with (args.output.parent / "metrics.csv").open("x", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["metric", "fp32", "int8", "unit", "relative_change_percent"])
        writer.writerows(rows)


if __name__ == "__main__":
    main()
