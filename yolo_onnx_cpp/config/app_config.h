#pragma once

#include <optional>
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
    // Class-ID order; empty or -1 entries use the model's scalar threshold.
    std::vector<float> class_conf_thresholds;
    float iou_threshold = 0.45F;
    std::string low_res_model_path;
    int low_res_input_width = 0;
    int low_res_input_height = 0;
    float low_res_conf_threshold = -1.0F;
    // Empty inherits class_conf_thresholds; -1 uses the low model's scalar threshold.
    std::vector<float> low_res_class_conf_thresholds;
    float low_res_iou_threshold = -1.0F;
    int num_classes = 0;
    std::vector<std::string> class_names;
    // Legacy shared thread setting. Scoped settings fall back to this value.
    int thread_num = 4;
    int server_io_threads = 0;
    int opencv_threads = 0;
    int high_model_threads = 0;
    int low_model_threads = 0;
    // Zero lets OpenVINO choose its optimal number of requests.
    int infer_request_count = 1;
    // Zero inherits infer_request_count for the low-resolution engine.
    int low_res_infer_request_count = 0;
    std::string openvino_performance_mode = "latency";
    // Omitted keeps the plugin default; explicit false lets the OS schedule CPU threads.
    std::optional<bool> openvino_cpu_pinning;
    int max_streams = 4;
    int per_stream_queue_depth = 2;
    int max_request_age_ms = 300;
    // Local decode-to-result deadline for live streams; zero disables dropping.
    int max_result_age_ms = 300;
    int max_stream_duration_seconds = 0;
    int stream_max_consecutive_errors = 5;
    int video_job_threads = 2;
    int video_job_queue_depth = 4;
    bool use_letterbox = true;
    float video_detect_fps = 4.0F;
    float video_high_detect_fps = 1.0F;
    // Optional normalized crop for scheduled high-resolution refreshes.
    bool high_res_roi_enabled = false;
    float high_res_roi_x = 0.0F;
    float high_res_roi_y = 0.0F;
    float high_res_roi_width = 1.0F;
    float high_res_roi_height = 1.0F;
    // Run one full-frame high-resolution refresh after this many high-model calls.
    int high_res_roi_full_frame_interval = 4;
    std::string video_stride_mode = "dynamic";
    bool video_model_async = true;
    // Legacy alias accepted for old configs and callers.
    bool video_onnx_async = true;
    int client_max_body_mb = 256;
    int client_max_memory_body_mb = 16;
    std::string api_bearer_token_env;
    float api_rate_limit_requests_per_second = 0.0F;
    int api_rate_limit_burst = 20;
    std::string tls_certificate_path;
    std::string tls_private_key_path;
};

AppConfig loadAppConfig(const std::string& config_path);
int serverIoThreadCount(const AppConfig& config);
bool hasLowResModelConfig(const AppConfig& config);
AppConfig makeHighResAppConfig(const AppConfig& config);
AppConfig makeLowResAppConfig(const AppConfig& config);

}  // namespace yolo
