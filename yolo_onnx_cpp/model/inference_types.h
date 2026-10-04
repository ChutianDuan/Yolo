#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "metrics/performance_metrics.h"

namespace yolo {

// 随请求与结果传递的源帧标识，用于异步结果归属和时序检查。
struct InferenceContext {
    std::string stream_id;
    int64_t frame_index = -1;
    double timestamp_ms = -1.0;
};

// 原图到模型输入的缩放与左/上填充量；解码时执行逆变换。
struct LetterBoxInfo {
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float pad_w = 0.0F;
    float pad_h = 0.0F;
};

// 持有连续的 RGB、NCHW 浮点数据，并保留还原原图坐标所需的信息。
struct TensorInput {
    std::vector<float> values;
    std::vector<int64_t> shape;
    int image_width = 0;
    int image_height = 0;
    LetterBoxInfo letterbox;
};

// 检测框使用原图像素坐标，(x1, y1) 为左上角，(x2, y2) 为右下角。
struct Detection {
    int class_id = -1;
    float score = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
};

struct TrackedDetection {
    int track_id = -1;
    Detection detection;
};

struct InferResult {
    // 模型输出与解码后的原图检测框。
    std::vector<std::vector<int64_t>> output_shapes;
    std::vector<Detection> detections;
    // 单次推理的阶段耗时，单位毫秒；onnx_inference_ms 是兼容别名。
    double model_inference_ms = 0.0;
    // Legacy alias kept for the existing JSON schema and tests.
    double onnx_inference_ms = 0.0;
    double preprocess_ms = 0.0;
    double decode_ms = 0.0;
    double infer_ms = 0.0;
    double postprocess_ms = 0.0;
    double queue_wait_ms = 0.0;
    double end_to_end_ms = 0.0;
    // 原始阶段样本及汇总指标；context 用于异步结果归属检查。
    StageTimingSamples timing_samples;
    PerformanceMetrics metrics;
    InferenceContext context;
};

}  // namespace yolo
