#pragma once

#include <string>
#include <vector>

namespace yolo {

struct AppConfig {
    std::string model_path = "./deploy/best.onnx";
    std::string model_backend = "auto";
    std::string openvino_device = "CPU";
    int input_width = 1280;
    int input_height = 736;
    float conf_threshold = 0.25F;
    float iou_threshold = 0.45F;
    std::string low_res_model_path;
    int low_res_input_width = 0;
    int low_res_input_height = 0;
    float low_res_conf_threshold = -1.0F;
    float low_res_iou_threshold = -1.0F;
    int num_classes = 0;
    std::vector<std::string> class_names;
    int thread_num = 4;
    bool use_letterbox = true;
    float video_detect_fps = 4.0F;
    std::string video_stride_mode = "dynamic";
    bool video_model_async = true;
    // Legacy alias accepted for old configs and callers.
    bool video_onnx_async = true;
    int client_max_body_mb = 256;
};

AppConfig loadAppConfig(const std::string& config_path);
bool hasLowResModelConfig(const AppConfig& config);
AppConfig makeLowResAppConfig(const AppConfig& config);

}  // namespace yolo
