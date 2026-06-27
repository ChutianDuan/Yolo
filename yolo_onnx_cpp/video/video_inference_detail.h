#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <opencv2/core.hpp>

#include "config/app_config.h"
#include "model/inference_types.h"
#include "model/yolo_engine.h"
#include "video/optical_flow_tracker.h"
#include "video/video_inference.h"

namespace yolo::video_inference_detail {

struct TrackChangeQuality {
    size_t previous_track_count = 0;
    size_t current_track_count = 0;
    size_t new_track_count = 0;
    size_t lost_track_count = 0;
    size_t velocity_jump_count = 0;
    size_t scale_jump_count = 0;
};

struct DynamicStrideState {
    int base_stride = 1;
    int current_stride = 1;
    int urgent_stride = 1;
    int complex_stride = 1;
    int stable_stride = 1;
    int stable_frame_count = 0;
    int complex_frame_count = 0;
    int64_t last_detection_frame_index = -1;
};

struct AsyncInferResult {
    int64_t frame_index = 0;
    InferResult result;
    std::string error_message;
    bool bad_request = false;
    bool ok = false;
};

class AsyncInferWorker final {
public:
    AsyncInferWorker(std::shared_ptr<YoloEngine> engine, AppConfig config);
    ~AsyncInferWorker();

    AsyncInferWorker(const AsyncInferWorker&) = delete;
    AsyncInferWorker& operator=(const AsyncInferWorker&) = delete;

    bool submit(cv::Mat frame, int64_t frame_index);
    bool tryPopResult(AsyncInferResult& result);
    bool waitPopResult(AsyncInferResult& result);
    bool hasPending() const;

private:
    struct Request {
        int64_t frame_index = 0;
        cv::Mat frame;
    };

    bool hasPendingLocked() const;
    void run();

    std::shared_ptr<YoloEngine> engine_;
    AppConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable request_ready_;
    std::condition_variable result_ready_;
    std::deque<Request> requests_;
    std::deque<AsyncInferResult> results_;
    std::thread worker_;
    bool in_flight_ = false;
    bool stopping_ = false;
};

double elapsedMs(std::chrono::steady_clock::time_point start);

TrackChangeQuality trackChangeQuality(
    const std::vector<TrackedDetection>& previous_tracks,
    const std::vector<TrackedDetection>& current_tracks,
    const std::unordered_map<int, cv::Point2f>& previous_velocities
);

void updateTrackVelocities(
    const std::vector<TrackedDetection>& previous_tracks,
    const std::vector<TrackedDetection>& current_tracks,
    std::unordered_map<int, cv::Point2f>& track_velocities
);

void applyWeakQualityToStride(DynamicStrideState& state, const WeakTrackQuality& quality);

void applyTrackChangeToStride(
    DynamicStrideState& state,
    const TrackChangeQuality& quality
);

std::string invalidVideoFrameMessage(const AppConfig& config);

double finiteOrZero(double value);

double frameTimestampMs(
    int64_t frame_index,
    double source_fps,
    double capture_timestamp_ms
);

int videoFrameStride(double source_fps, float target_fps);

DynamicStrideState makeDynamicStrideState(int base_stride);

bool shouldRunScheduledDetection(
    const DynamicStrideState& stride_state,
    int64_t frame_index,
    int64_t scheduled_request_count
);

void fillVideoSummary(
    VideoInferResult& result,
    const DynamicStrideState& stride_state,
    double source_fps,
    float target_detect_fps,
    const std::string& stride_mode,
    bool onnx_async,
    int base_frame_stride,
    int min_stride_used,
    int max_stride_used,
    int64_t readable_frame_count,
    int64_t processed_frame_count,
    int64_t source_frame_count,
    int64_t async_infer_request_count,
    int64_t async_correction_count,
    int64_t forced_detection_count,
    int64_t scheduled_detection_count,
    int64_t skipped_detection_count,
    int width,
    int height
);

}  // namespace yolo::video_inference_detail
