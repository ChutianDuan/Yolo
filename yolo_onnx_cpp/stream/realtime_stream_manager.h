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

struct RealtimeStreamSnapshot {
    std::string stream_id;
    std::string source;
    std::string status = "starting";
    std::string last_error;
    int width = 0;
    int height = 0;
    double source_fps = 0.0;
    uint64_t decoded_frame_count = 0;
    uint64_t processed_frame_count = 0;
    uint64_t detection_frame_count = 0;
    uint64_t high_res_detection_count = 0;
    uint64_t roi_high_res_detection_count = 0;
    uint64_t low_res_detection_count = 0;
    uint64_t dropped_frame_count = 0;
    uint64_t decoder_queue_drop_count = 0;
    uint64_t processor_coalesced_frame_count = 0;
    uint64_t stale_frame_drop_count = 0;
    // Scheduler Stale/Replaced or rejected asynchronous results; not model failures.
    uint64_t skipped_inference_count = 0;
    uint64_t inference_error_count = 0;
    uint64_t consecutive_inference_error_count = 0;
    uint64_t reconnect_count = 0;
    size_t queue_length = 0;
    size_t max_queue_length = 0;
    uint64_t latest_sequence = 0;
    // Age since local decode of the latest processed frame, measured at query time.
    // Zero before the first result; excludes camera/network/decoder buffering.
    double latest_result_age_ms = 0.0;
    StreamInferenceDiagnosticsByTier async_inference_diagnostics{};
    StreamProcessingDiagnostics processing_diagnostics{};
    uint64_t weak_flow_roi_count = 0;
    uint64_t weak_flow_roi_pixels = 0;
    uint64_t weak_flow_sampled_points = 0;
};

// Manager-lifetime totals; retain these after individual stream records are removed.
struct RealtimeStreamCounters {
    uint64_t decoded_frame_count = 0;
    uint64_t processed_frame_count = 0;
    uint64_t dropped_frame_count = 0;
    uint64_t decoder_queue_drop_count = 0;
    uint64_t processor_coalesced_frame_count = 0;
    uint64_t stale_frame_drop_count = 0;
    uint64_t skipped_inference_count = 0;
    uint64_t inference_error_count = 0;
    StreamInferenceDiagnosticsByTier async_inference_diagnostics{};
    StreamProcessingDiagnostics processing_diagnostics{};
    uint64_t weak_flow_roi_count = 0;
    uint64_t weak_flow_roi_pixels = 0;
    uint64_t weak_flow_sampled_points = 0;
};

struct RealtimeStreamMetrics {
    size_t active_count = 0;
    RealtimeStreamCounters totals;
    // Includes terminal and stopping records, which still occupy admission slots.
    std::vector<RealtimeStreamSnapshot> streams;
};

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
