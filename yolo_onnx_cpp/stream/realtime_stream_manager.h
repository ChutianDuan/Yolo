#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config/app_config.h"
#include "model/inference_types.h"
#include "stream/inference_diagnostics.h"

namespace yolo {

class InferenceScheduler;
class YoloEngine;

struct RealtimeInferenceUpdate {
    InferenceContext context;
    std::string model_tier;
    bool high_res_roi = false;
};

struct RealtimeFrameEvent {
    uint64_t sequence = 0;
    int64_t frame_index = 0;
    double timestamp_ms = 0.0;
    double result_age_ms = 0.0;
    bool detection_frame = false;
    bool high_res_roi = false;
    std::string model_tier = "flow";
    bool terminal = false;
    std::string status = "running";
    std::vector<TrackedDetection> tracks;
    // Only asynchronous corrections; frame_index/timestamp above remain the output frame.
    std::vector<RealtimeInferenceUpdate> inference_updates;
};

// 累计解码、处理和丢帧数；丢帧原因用于诊断细分，不应与 dropped_frame_count 再相加。
struct RealtimeFrameCounters {
    uint64_t decoded_frame_count = 0;
    uint64_t processed_frame_count = 0;
    uint64_t dropped_frame_count = 0;
    uint64_t decoder_queue_drop_count = 0;
    uint64_t processor_coalesced_frame_count = 0;
    uint64_t stale_frame_drop_count = 0;
};

// 累计跳过与失败推理数；过期、替换或异步结果被拒绝属于跳过，不是模型失败。
struct RealtimeInferenceCounters {
    uint64_t skipped_inference_count = 0;
    uint64_t inference_error_count = 0;
};

// 弱光流累计工作量：ROI 数、ROI 像素数和采样点数，用于判断负载来源。
struct RealtimeWeakFlowCounters {
    uint64_t weak_flow_roi_count = 0;
    uint64_t weak_flow_roi_pixels = 0;
    uint64_t weak_flow_sampled_points = 0;
};

// 可合并的累计统计；用于单流生命周期和管理器生命周期累计。
// 管理器在移除单流后保留累计值；队列长度、最新结果等瞬时状态不参与累计。
struct RealtimeStreamCounters {
    RealtimeFrameCounters frames;
    RealtimeInferenceCounters inference;
    StreamInferenceDiagnosticsByTier async_inference_diagnostics{};
    StreamProcessingDiagnostics processing_diagnostics{};
    RealtimeWeakFlowCounters weak_flow;
};

// 当前流生命周期内的检测次数，ROI 高分辨率次数是高分辨率次数的子集。
struct RealtimeDetectionCounters {
    uint64_t detection_frame_count = 0;
    uint64_t high_res_detection_count = 0;
    uint64_t roi_high_res_detection_count = 0;
    uint64_t low_res_detection_count = 0;
};

// 查询时的解码帧队列长度与当前流生命周期内的峰值。
struct RealtimeQueueStats {
    size_t queue_length = 0;
    size_t max_queue_length = 0;
};

struct RealtimeLatestResult {
    uint64_t latest_sequence = 0;
    // 查询时距最新已处理帧本地解码的毫秒数；首个结果前为零。
    // 不包含摄像头、网络或解码器自身缓冲时间。
    double latest_result_age_ms = 0.0;
};

// 单流状态快照：身份、当前状态与累计统计分开组织。
struct RealtimeStreamSnapshot {
    std::string stream_id;
    std::string source;
    std::string status = "starting";
    std::string last_error;
    int width = 0;
    int height = 0;
    double source_fps = 0.0;
    RealtimeStreamCounters counters;
    RealtimeDetectionCounters detections;
    // 连续模型错误数会在成功后重置，区别于累计 inference_error_count。
    uint64_t consecutive_inference_error_count = 0;
    uint64_t reconnect_count = 0;
    RealtimeQueueStats queue;
    RealtimeLatestResult latest;
};

struct RealtimeStreamMetrics {
    size_t active_count = 0;
    RealtimeStreamCounters totals;
    // Includes terminal and stopping records, which still occupy admission slots.
    std::vector<RealtimeStreamSnapshot> streams;
};

// 回调返回 false 时取消该订阅；每个订阅的事件按序交付。
using RealtimeEventCallback = std::function<bool(const RealtimeFrameEvent&)>;

enum class RealtimeSubscribeStatus {
    Subscribed,
    StreamUnavailable,
    HistoryExpired,
    CursorAhead,
};

struct RealtimeSubscribeResult {
    RealtimeSubscribeStatus status = RealtimeSubscribeStatus::StreamUnavailable;
    uint64_t earliest_available_sequence = 0;
    uint64_t latest_sequence = 0;
};

// 管理每条流的解码、处理线程和事件历史，多条流复用共享模型调度器。
class RealtimeStreamManager final {
public:
    RealtimeStreamManager(
        std::shared_ptr<YoloEngine> high_res_engine,
        std::shared_ptr<YoloEngine> low_res_engine,
        std::shared_ptr<InferenceScheduler> high_res_scheduler,
        std::shared_ptr<InferenceScheduler> low_res_scheduler,
        AppConfig config
    );
    ~RealtimeStreamManager();

    RealtimeStreamManager(const RealtimeStreamManager&) = delete;
    RealtimeStreamManager& operator=(const RealtimeStreamManager&) = delete;

    bool create(
        const std::string& stream_id,
        const std::string& source,
        std::string& error_message
    );
    bool stop(const std::string& stream_id);
    bool get(const std::string& stream_id, RealtimeStreamSnapshot& snapshot) const;
    std::vector<RealtimeStreamSnapshot> list() const;
    RealtimeStreamMetrics metrics() const;
    bool subscribe(const std::string& stream_id, RealtimeEventCallback callback);
    // 从游标之后补发历史再接收新事件；历史已淘汰或游标超前时返回明确状态。
    RealtimeSubscribeResult subscribeAfter(
        const std::string& stream_id,
        uint64_t last_event_sequence,
        RealtimeEventCallback callback
    );

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace yolo
