#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

#include <opencv2/core.hpp>

#include "model/inference_types.h"

namespace yolo {

struct WeakTrackQuality {
    size_t previous_track_count = 0;
    size_t sampled_point_count = 0;
    size_t valid_point_count = 0;
    size_t low_point_track_count = 0;
    size_t invalid_ratio_track_count = 0;
    size_t forward_backward_rejection_count = 0;
    size_t motion_dispersion_rejection_count = 0;
    size_t motion_jump_rejection_count = 0;
    size_t boundary_rejection_count = 0;
    size_t tracked_track_count = 0;
    double valid_point_ratio = 0.0;
    double tracked_track_ratio = 0.0;
    double median_flow_ratio = 0.0;
    double median_forward_backward_error = 0.0;
    float median_dx = 0.0F;
    float median_dy = 0.0F;
    double mean_frame_diff = 0.0;
    float min_score = 1.0F;
    bool has_flow = false;
};

struct TrackFlowQuality {
    int track_id = -1;
    size_t sampled_point_count = 0;
    size_t valid_point_count = 0;
    double valid_point_ratio = 0.0;
    double median_forward_backward_error = 0.0;
    float median_dx = 0.0F;
    float median_dy = 0.0F;
    double displacement_spread_ratio = 0.0;
    double motion_ratio = 0.0;
    double visible_area_ratio = 1.0;
    bool boundary_clipped = false;
    bool accepted = false;
};

struct WeakTrackResult {
    std::vector<TrackedDetection> tracks;
    std::vector<TrackFlowQuality> track_qualities;
    WeakTrackQuality quality;
    // Internal wall time only; absent means this phase was skipped.
    std::array<std::optional<double>, 6> stage_ms{};
    uint64_t roi_count = 0;
    uint64_t roi_pixels = 0;
};

struct FrameMotion {
    int64_t frame_index = 0;
    float dx = 0.0F;
    float dy = 0.0F;
    size_t sampled_point_count = 0;
    size_t valid_point_count = 0;
    double inlier_ratio = 0.0;
    double displacement_spread_ratio = 0.0;
    bool valid = false;
};

WeakTrackResult weakTrackWithOpticalFlow(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    const std::vector<TrackedDetection>& previous_tracks,
    int image_width,
    int image_height
);

// Internal streaming overload: previous_pyramid belongs to previous_gray; outputs must not alias.
WeakTrackResult weakTrackWithOpticalFlow(
    const cv::Mat& previous_gray, const cv::Mat& current_gray,
    const std::vector<TrackedDetection>& previous_tracks, int image_width, int image_height,
    const std::vector<cv::Mat>& previous_pyramid, std::vector<cv::Mat>& current_pyramid
);

FrameMotion frameMotionForCurrentFrame(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    int64_t frame_index,
    const WeakTrackQuality& quality,
    const std::vector<TrackedDetection>& previous_tracks
);

std::vector<Detection> motionCompensatedDetections(
    const std::vector<Detection>& detections,
    int64_t detection_frame_index,
    int64_t current_frame_index,
    const std::vector<FrameMotion>& frame_motions,
    int image_width,
    int image_height
);

bool isSevereWeakQualityDrop(const WeakTrackQuality& quality);
bool isComplexWeakQuality(const WeakTrackQuality& quality);
bool isStableWeakQuality(const WeakTrackQuality& quality);

}  // namespace yolo
