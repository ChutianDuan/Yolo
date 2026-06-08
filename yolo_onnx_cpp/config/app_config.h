#pragma once

#include <string>
#include <vector>

namespace yolo {

struct AppConfig {
    std::string model_path = "./deploy/best.onnx";
    int input_width = 1280;
    int input_height = 736;
    float conf_threshold = 0.25F;
    float iou_threshold = 0.45F;
    int num_classes = 0;
    std::vector<std::string> class_names;
    int thread_num = 4;
    bool use_letterbox = true;
    float video_detect_fps = 4.0F;
    std::string video_stride_mode = "dynamic";
    bool video_onnx_async = true;
    int client_max_body_mb = 256;
};

AppConfig loadAppConfig(const std::string& config_path);

}  // namespace yolo
