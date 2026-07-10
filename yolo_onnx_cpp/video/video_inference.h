#pragma once

#include <cstddef>
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

struct HighLowDiagnostics {
    int64_t stable_track_count = 0;
    int64_t provisional_track_count = 0;
    int64_t max_stable_track_count = 0;
    int64_t max_provisional_track_count = 0;
    int64_t provisional_created_count = 0;
    int64_t provisional_promoted_count = 0;
    int64_t provisional_expired_count = 0;
    int64_t provisional_deduplicated_count = 0;
    int64_t stable_stable_duplicate_count = 0;
    int64_t stable_provisional_duplicate_count = 0;
    int64_t provisional_provisional_duplicate_count = 0;
    int64_t output_suppressed_duplicate_count = 0;
    int64_t low_res_geometry_rejection_count = 0;
    int64_t low_res_class_conflict_count = 0;
    int64_t flow_track_count = 0;
    int64_t flow_rejected_track_count = 0;
    int64_t flow_low_point_rejection_count = 0;
    int64_t flow_invalid_ratio_rejection_count = 0;
    int64_t flow_forward_backward_rejection_count = 0;
    int64_t flow_motion_dispersion_rejection_count = 0;
    int64_t flow_motion_jump_rejection_count = 0;
    int64_t flow_boundary_rejection_count = 0;
    int64_t direct_flow_update_count = 0;
    int64_t global_flow_update_count = 0;
    int64_t flow_age_output_suppression_count = 0;
    int64_t flow_age_expired_count = 0;
    int64_t exiting_track_suppression_count = 0;
    int64_t urgent_low_res_detection_count = 0;
    int64_t urgent_high_res_detection_count = 0;
    int64_t urgent_flow_quality_count = 0;
    int64_t urgent_track_change_count = 0;
    int64_t urgent_duplicate_count = 0;
    int64_t urgent_flow_age_count = 0;
    int64_t urgent_geometry_count = 0;
    int64_t urgent_class_conflict_count = 0;
};

struct VideoInferResult {
    std::string tracking_status = "active";
    double fps = 0.0;
    double source_fps = 0.0;
    float target_detect_fps = 0.0F;
    double effective_detect_fps = 0.0;
    double frame_stride = 0.0;
    std::string stride_mode;
    bool model_async = true;
    // Legacy alias kept for the existing JSON schema and tests.
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
    double model_inference_ms = 0.0;
    double model_postprocess_ms = 0.0;
    // Legacy aliases kept for the existing JSON schema and tests.
    double onnx_inference_ms = 0.0;
    double onnx_postprocess_ms = 0.0;
    double decode_ms = 0.0;
    double preprocess_ms = 0.0;
    double infer_ms = 0.0;
    double postprocess_ms = 0.0;
    double tracker_ms = 0.0;
    double queue_wait_ms = 0.0;
    double end_to_end_ms = 0.0;
    size_t queue_length = 0;
    size_t max_queue_length = 0;
    int64_t dropped_frame_count = 0;
    double optical_flow_ms = 0.0;
    double tracking_postprocess_ms = 0.0;
    StageTimingSamples timing_samples;
    PerformanceMetrics metrics;
    HighLowDiagnostics high_low_diagnostics;
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

VideoInferResult inferVideoFileHighLow(
    const std::shared_ptr<YoloEngine>& high_res_engine,
    const std::shared_ptr<YoloEngine>& low_res_engine,
    const AppConfig& config,
    const std::filesystem::path& video_path
);

}  // namespace yolo
