#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "config/app_config.h"
#include "model/inference_types.h"
#include "model/yolo_engine.h"

namespace yolo {

struct VideoFrameTracks {
    int64_t frame_index = 0;
    double timestamp_ms = 0.0;
    bool is_detection_frame = false;
    int64_t corrected_from_frame_index = -1;
    int64_t correction_latency_frames = 0;
    std::string tracks_source = "empty";
    std::vector<TrackedDetection> tracks;
};

struct VideoInferResult {
    std::string tracking_status = "active";
    double fps = 0.0;
    double source_fps = 0.0;
    float target_detect_fps = 0.0F;
    double effective_detect_fps = 0.0;
    double frame_stride = 0.0;
    std::string stride_mode;
    bool onnx_async = true;
    int base_frame_stride = 1;
    int min_frame_stride_used = 1;
    int max_frame_stride_used = 1;
    int final_frame_stride = 1;
    int width = 0;
    int height = 0;
    int64_t frame_count = 0;
    int64_t source_frame_count = 0;
    int64_t processed_frame_count = 0;
    int64_t display_frame_count = 0;
    int64_t detected_frame_count = 0;
    int64_t async_infer_request_count = 0;
    int64_t async_correction_count = 0;
    int64_t async_corrected_frame_count = 0;
    int64_t forced_detection_count = 0;
    int64_t scheduled_detection_count = 0;
    int64_t skipped_detection_count = 0;
    int64_t weak_tracked_frame_count = 0;
    int64_t interpolated_frame_count = 0;
    int64_t empty_frame_count = 0;
    double total_elapsed_ms = 0.0;
    double onnx_inference_ms = 0.0;
    double onnx_postprocess_ms = 0.0;
    double optical_flow_ms = 0.0;
    double tracking_postprocess_ms = 0.0;
    std::vector<std::vector<int64_t>> output_shapes;
    std::vector<VideoFrameTracks> frames;
};

class VideoInferError final : public std::runtime_error {
public:
    VideoInferError(std::string message, bool bad_request);

    bool badRequest() const;

private:
    bool bad_request_ = false;
};

VideoInferResult inferVideoFile(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    const std::filesystem::path& video_path
);

}  // namespace yolo
