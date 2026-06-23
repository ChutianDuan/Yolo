#pragma once

#include <cstdint>
#include <vector>

namespace yolo {

struct LetterBoxInfo {
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float pad_w = 0.0F;
    float pad_h = 0.0F;
};

struct TensorInput {
    std::vector<float> values;
    std::vector<int64_t> shape;
    int image_width = 0;
    int image_height = 0;
    LetterBoxInfo letterbox;
};

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
    std::vector<std::vector<int64_t>> output_shapes;
    std::vector<Detection> detections;
    double onnx_inference_ms = 0.0;
    double postprocess_ms = 0.0;
};

}  // namespace yolo
