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
#include "tracking/authority_tracker.h"

namespace yolo {

class InferenceScheduler;

struct VideoFrameTracks {
    int64_t frame_index = 0;
    double timestamp_ms = 0.0;
    bool is_detection_frame = false;
    int64_t corrected_from_frame_index = -1;
    int64_t correction_latency_frames = 0;
    std::string tracks_source = "empty";
    std::vector<TrackedDetection> tracks;
};

// 视频源帧率与尺寸；fps 为 source_fps 的兼容字段，不是推理吞吐率。
struct VideoSourceInfo {
    double fps = 0.0;
    double source_fps = 0.0;
    int width = 0;
    int height = 0;
};

// 检测采样策略及本次运行的步长范围；onnx_async 保留为 model_async 的兼容别名。
struct VideoDetectionPolicy {
    float target_detect_fps = 0.0F;
    double effective_detect_fps = 0.0;
    double frame_stride = 0.0;
    std::string stride_mode;
    bool model_async = true;
    bool onnx_async = true;
    int base_frame_stride = 1;
    int min_frame_stride_used = 1;
    int max_frame_stride_used = 1;
    int final_frame_stride = 1;
};

// 本次视频任务的帧计数；输出帧、源帧和处理帧采用各自口径。
struct VideoFrameCounts {
    int64_t frame_count = 0;
    int64_t source_frame_count = 0;
    int64_t processed_frame_count = 0;
    int64_t display_frame_count = 0;
    int64_t weak_tracked_frame_count = 0;
    int64_t interpolated_frame_count = 0;
    int64_t empty_frame_count = 0;
};

// 检测与调度次数；ROI 高分辨率检测是检测的子集，不能与总数直接相加。
struct VideoDetectionCounts {
    int64_t detected_frame_count = 0;
    int64_t roi_high_res_detection_count = 0;
    int64_t forced_detection_count = 0;
    int64_t scheduled_detection_count = 0;
    int64_t skipped_detection_count = 0;
};

// 异步提交与校正统计；校正次数和被校正的输出帧数不是同一口径。
struct VideoAsyncCounts {
    int64_t async_infer_request_count = 0;
    int64_t async_correction_count = 0;
    int64_t async_corrected_frame_count = 0;
};

// 本次任务耗时，单位毫秒；阶段累计值可能重叠或并发，不能直接相加作为总墙钟时间。
struct VideoTiming {
    double total_elapsed_ms = 0.0;
    double model_inference_ms = 0.0;
    double model_postprocess_ms = 0.0;
    // 与 model_* 字段同步的旧命名，保留现有兼容口径。
    double onnx_inference_ms = 0.0;
    double onnx_postprocess_ms = 0.0;
    double decode_ms = 0.0;
    double preprocess_ms = 0.0;
    double infer_ms = 0.0;
    double postprocess_ms = 0.0;
    double tracker_ms = 0.0;
    double queue_wait_ms = 0.0;
    double end_to_end_ms = 0.0;
    double optical_flow_ms = 0.0;
    double tracking_postprocess_ms = 0.0;
};

// 本次任务结束时的队列长度、运行期间的峰值及累计丢帧数。
struct VideoQueueStats {
    size_t queue_length = 0;
    size_t max_queue_length = 0;
    int64_t dropped_frame_count = 0;
};

// 视频推理结果：策略、计数、耗时和输出内容分别组织，JSON 仍沿用原有字段。
struct VideoInferResult {
    std::string tracking_status = "active";
    VideoSourceInfo video_info;
    VideoDetectionPolicy detection_policy;
    VideoFrameCounts frame_counts;
    VideoDetectionCounts detection_counts;
    VideoAsyncCounts async_counts;
    VideoTiming timing;
    VideoQueueStats queue;
    // 原始阶段样本用于汇总分位数；metrics 为生成后的性能汇总。
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
    const std::filesystem::path& video_path,
    std::shared_ptr<InferenceScheduler> scheduler = nullptr,
    std::string stream_id = {}
);

VideoInferResult inferVideoFileHighLow(
    const std::shared_ptr<YoloEngine>& high_res_engine,
    const std::shared_ptr<YoloEngine>& low_res_engine,
    const AppConfig& config,
    const std::filesystem::path& video_path,
    std::shared_ptr<InferenceScheduler> high_res_scheduler = nullptr,
    std::shared_ptr<InferenceScheduler> low_res_scheduler = nullptr,
    std::string stream_id = {}
);

}  // namespace yolo
