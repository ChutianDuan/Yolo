# 2026-09-27 本地多路终态与容量验证

## 四路完整八小时

- 原始目录：[diag66_four_detached_soak_8h](../../../yolo_onnx_cpp/test_outputs/multistream_local_20260924/diag66_four_detached_soak_8h/)。
- [终态审计](diag66_four_8h_final_audit.json)：原性能PASS，5753正式快照/5623持久化样本/111累计，全部退流指标0，原进程退出。
- [完整分段](diag66_four_8h_windows.json)：固定5623条，95个五分钟诊断窗四项速率/公平无越界，重复运行一致；近似分段不能替代正式CPU/P95/RSS门槛。
- 四路原正式RSS-2.433%，独立轨迹峰804520KiB；本地MJPEG循环内容，不能外推生产或覆盖六路原FAIL。
- 实施细节与后续边界见[实施记录第67节](../../reports/多路实时监测实施记录_20260905.md)。

只读复核（仓库根、yolo环境）：

```bash
conda run -n yolo python yolo_onnx_cpp/tools/verify_local_multistream_soak.py yolo_onnx_cpp/test_outputs/multistream_local_20260924/diag66_four_detached_soak_8h
conda run -n yolo python yolo_onnx_cpp/tools/summarize_multistream_soak_windows.py yolo_onnx_cpp/test_outputs/multistream_local_20260924/diag66_four_detached_soak_8h --samples 5623
```

## 八路同profile短窗

[终态审计](diag67_eight_60s_final_audit.json)：60秒预热+60.007秒正式窗，原FAIL（六路low、一路output和公平），13正式快照/25持久化样本/111累计，退流全0、服务退出0/collector退出2。

原始目录：[diag67_eight_capacity_60s](../../../yolo_onnx_cpp/test_outputs/multistream_local_20260927/diag67_eight_capacity_60s/)。与四路长测的两模型/源码/runtime相同，YAML仅max_streams不同；不同时长和日期不是受控加速对照。四段内容复用为八条连接，完整八路长测未完成。

```bash
conda run -n yolo python yolo_onnx_cpp/tools/verify_local_multistream_soak.py yolo_onnx_cpp/test_outputs/multistream_local_20260927/diag67_eight_capacity_60s
```


## 第68节四路CPU亲和性对照

[只读四组审计](verify_affinity_20260927.py)、[全机A1](diag68_all_a1_audit.json)：A1原PASS/退流全0/逐线程CPU范围核验通过；其余组待核验。本轮仅CPU affinity，不设置内存绑定。

[节点0审计](diag68_node0_audit.json)：原FAIL，仅公平11.899%越界；1490条线程观察在指定范围、退流全0，节点1及A2待完成。

[节点1审计](diag68_node1_audit.json)：原FAIL，高模/输出/公平越界；1490条线程范围核验与退流通过，A2待完成。新审计3个离线正反检查通过。

## 第68节四组终态

[A2审计](diag68_all_a2_audit.json)、[完整矩阵](diag68_affinity_matrix_summary.json)：全机A1/节点0/节点1/全机A2原结果PASS/FAIL/FAIL/PASS，四组均13正式快照/25持久化样本/111累计且退流全0。YAML、两模型、输入、源码和binary相同；HT原生属性全机NO、节点组YES，不能作为纯NUMA访存或双worker验证。上述逐组“待完成”描述保留其历史时点，以本终态为准。

独立复跑与冻结JSON逐字段一致：

```bash
conda run -n yolo python docs/test-results/multistream_local_20260927/verify_affinity_20260927.py
```

生产代码/配置未改；仅新增只读审计、四个单组审计和一个矩阵汇总。下一项为单节点8/16线程对照，完整NUMA/六八路长期/生产验收仍未完成。
