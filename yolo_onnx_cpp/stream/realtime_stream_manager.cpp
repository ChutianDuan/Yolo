#include "stream/realtime_stream_manager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "image/image_processing.h"
#include "model/inference_scheduler.h"
#include "metrics/stream_logging.h"
#include "model/yolo_engine.h"
#include "stream/detection_cadence.h"
#include "stream/frame_deadline.h"
#include "stream/high_res_roi.h"
#include "stream/stream_metrics.h"
#include "stream/stream_processor.h"
#include "stream/stream_inference_replay.h"
#include "stream/single_model_inference_replay.h"
#include "video/optical_flow_tracker.h"

namespace yolo {
namespace {

constexpr size_t kMaxBufferedEvents = 256;
constexpr size_t kMaxSubscribersPerStream = 32;
constexpr size_t kMaxPendingSubscriberEvents = kMaxBufferedEvents * 2;
constexpr auto kReconnectDelay = std::chrono::seconds(1);

bool isValidStreamId(const std::string& stream_id) {
    if (stream_id.empty() || stream_id.size() > 64) {
        return false;
    }
    return std::all_of(stream_id.begin(), stream_id.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
    });
}

bool isLiveSource(const std::string& source) {
    return source.find("://") != std::string::npos
        && source.rfind("file://", 0) != 0;
}

std::string redactedSource(std::string source) {
    const size_t scheme_end = source.find("://");
    if (scheme_end == std::string::npos) {
        return source;
    }
    const size_t credentials_end = source.find('@', scheme_end + 3);
    if (credentials_end == std::string::npos) {
        return source;
    }
    source.replace(scheme_end + 3, credentials_end - scheme_end - 3, "***");
    return source;
}

struct EventSubscriberState {
    std::mutex mutex;
    std::deque<RealtimeFrameEvent> pending;
    bool delivering = false;
    bool active = true;
};

struct EventSubscriber {
    uint64_t id = 0;
    RealtimeEventCallback callback;
    std::shared_ptr<EventSubscriberState> state =
        std::make_shared<EventSubscriberState>();
};

enum class SubscriberQueueResult { Queued, Deliver, Inactive };

SubscriberQueueResult queueSubscriber(
    const EventSubscriber& subscriber,
    const RealtimeFrameEvent& event
) {
    std::lock_guard<std::mutex> lock(subscriber.state->mutex);
    if (!subscriber.state->active) {
        return SubscriberQueueResult::Inactive;
    }
    if (subscriber.state->pending.size() >= kMaxPendingSubscriberEvents) {
        subscriber.state->active = false;
        subscriber.state->pending.clear();
        subscriber.state->delivering = false;
        return SubscriberQueueResult::Inactive;
    }
    subscriber.state->pending.push_back(event);
    if (subscriber.state->delivering) {
        return SubscriberQueueResult::Queued;
    }
    subscriber.state->delivering = true;
    return SubscriberQueueResult::Deliver;
}

bool drainSubscriber(const EventSubscriber& subscriber) {
    while (true) {
        RealtimeFrameEvent event;
        {
            std::lock_guard<std::mutex> lock(subscriber.state->mutex);
            if (!subscriber.state->active) {
                return false;
            }
            if (subscriber.state->pending.empty()) {
                subscriber.state->delivering = false;
                return true;
            }
            event = std::move(subscriber.state->pending.front());
            subscriber.state->pending.pop_front();
        }

        bool keep = false;
        try {
            keep = subscriber.callback(event);
        } catch (...) {
            keep = false;
        }
        if (!keep) {
            std::lock_guard<std::mutex> lock(subscriber.state->mutex);
            subscriber.state->active = false;
            subscriber.state->pending.clear();
            subscriber.state->delivering = false;
            return false;
        }
    }
}

}  // namespace

class RealtimeStreamManager::Impl {
public:
    Impl(
        std::shared_ptr<YoloEngine> high_res_engine,
        std::shared_ptr<YoloEngine> low_res_engine,
        std::shared_ptr<InferenceScheduler> high_res_scheduler,
        std::shared_ptr<InferenceScheduler> low_res_scheduler,
        AppConfig config
    ) : high_res_engine_(std::move(high_res_engine)),
        low_res_engine_(std::move(low_res_engine)),
        high_res_scheduler_(std::move(high_res_scheduler)),
        low_res_scheduler_(std::move(low_res_scheduler)),
        config_(std::move(config)) {
        if (high_res_engine_ == nullptr) {
            throw std::invalid_argument("RealtimeStreamManager requires a high-res engine");
        }
    }

    ~Impl() {
        std::vector<std::shared_ptr<StreamContext>> contexts;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [stream_id, context] : streams_) {
                (void)stream_id;
                contexts.push_back(std::move(context));
            }
            streams_.clear();
        }
        for (const auto& context : contexts) {
            stopContext(context);
        }
    }

    bool create(
        const std::string& stream_id,
        const std::string& source,
        std::string& error_message
    ) {
        if (!isValidStreamId(stream_id)) {
            error_message =
                "stream_id must be 1-64 characters using letters, digits, '.', '_' or '-'";
            return false;
        }
        if (source.empty()) {
            error_message = "source cannot be empty";
            return false;
        }

        auto context = std::make_shared<StreamContext>();
        context->stream_id = stream_id;
        context->source = source;
        context->public_source = redactedSource(source);
        context->live_source = isLiveSource(source);

        // Publish fully assigned thread handles before another caller can stop the stream.
        std::unique_lock<std::mutex> lock(mutex_);
        if (streams_.find(stream_id) != streams_.end()) {
            error_message = "stream_id already exists";
            return false;
        }
        if (streams_.size() >= static_cast<size_t>(config_.max_streams)) {
            error_message = "maximum stream count reached";
            return false;
        }
        streams_.emplace(stream_id, context);

        try {
            context->decoder = std::thread(&Impl::runWorker, this, context, true);
            context->processor = std::thread(&Impl::runWorker, this, context, false);
        } catch (const std::exception& e) {
            error_message = e.what();
            context->removal_in_progress = true;
            lock.unlock();
            stopContext(context);
            lock.lock();
            const auto snapshot = snapshotOf(context);
            addStreamCounters(retired_counters_, snapshot);
            streams_.erase(stream_id);
            lock.unlock();
            logStreamEvent(StreamLogEvent::StartFailed, snapshot);
            return false;
        }
        lock.unlock();
        logStreamEvent(StreamLogEvent::Created, snapshotOf(context));
        return true;
    }

    bool stop(const std::string& stream_id) {
        std::shared_ptr<StreamContext> context;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = streams_.find(stream_id);
            if (found == streams_.end() || found->second->removal_in_progress) {
                return false;
            }
            context = found->second;
            // Synchronous removal cannot join its own callback/worker thread.
            const auto caller = std::this_thread::get_id();
            if (context->decoder.get_id() == caller || context->processor.get_id() == caller) {
                return false;
            }
            context->removal_in_progress = true;
        }
        stopContext(context);
        const auto snapshot = snapshotOf(context);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Keep the record visible until both threads exit, then transfer exactly once.
            addStreamCounters(retired_counters_, snapshot);
            streams_.erase(stream_id);
        }
        logStreamEvent(StreamLogEvent::Removed, snapshot);
        return true;
    }

    bool get(const std::string& stream_id, RealtimeStreamSnapshot& snapshot) const {
        const auto context = find(stream_id);
        if (context == nullptr) {
            return false;
        }
        snapshot = snapshotOf(context);
        return true;
    }

    std::vector<RealtimeStreamSnapshot> list() const {
        return metrics().streams;
    }

    RealtimeStreamMetrics metrics() const {
        std::vector<RealtimeStreamSnapshot> snapshots;
        RealtimeStreamCounters retired;
        {
            // Match retirement's lock order: registry first, then individual stream.
            std::lock_guard<std::mutex> lock(mutex_);
            retired = retired_counters_;
            snapshots.reserve(streams_.size());
            for (const auto& [stream_id, context] : streams_) {
                (void)stream_id;
                snapshots.push_back(snapshotOf(context));
            }
        }
        return summarizeStreamMetrics(retired, std::move(snapshots));
    }

    bool subscribe(const std::string& stream_id, RealtimeEventCallback callback) {
        return subscribeImpl(stream_id, std::nullopt, std::move(callback)).status
            == RealtimeSubscribeStatus::Subscribed;
    }

    RealtimeSubscribeResult subscribeAfter(
        const std::string& stream_id,
        uint64_t last_event_sequence,
        RealtimeEventCallback callback
    ) {
        return subscribeImpl(stream_id, last_event_sequence, std::move(callback));
    }

private:
    RealtimeSubscribeResult subscribeImpl(
        const std::string& stream_id,
        std::optional<uint64_t> last_event_sequence,
        RealtimeEventCallback callback
    ) {
        const auto context = find(stream_id);
        if (context == nullptr || !callback) {
            return {};
        }

        EventSubscriber subscriber;
        subscriber.callback = std::move(callback);
        bool registered = false;
        bool replay_pending = false;
        RealtimeSubscribeResult result;
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            result.latest_sequence = context->next_sequence - 1;
            result.earliest_available_sequence = context->events.empty()
                ? context->next_sequence
                : context->events.front().sequence;

            if (last_event_sequence.has_value()) {
                if (*last_event_sequence > result.latest_sequence) {
                    result.status = RealtimeSubscribeStatus::CursorAhead;
                    return result;
                }
                if (!context->events.empty()
                    && *last_event_sequence
                        < result.earliest_available_sequence - 1) {
                    result.status = RealtimeSubscribeStatus::HistoryExpired;
                    return result;
                }
                for (const auto& event : context->events) {
                    if (event.sequence > *last_event_sequence) {
                        subscriber.state->pending.push_back(event);
                    }
                }
                subscriber.state->delivering = !subscriber.state->pending.empty();
                replay_pending = subscriber.state->delivering;
            }

            const bool closing = context->stop_requested
                || isTerminalRealtimeStatus(context->status);
            // Cursor subscribers must stay attached until the terminal event is buffered.
            const bool terminal = closing && (!last_event_sequence.has_value()
                || (!context->events.empty() && context->events.back().terminal));
            if (terminal && subscriber.state->pending.empty()) {
                result.status = RealtimeSubscribeStatus::StreamUnavailable;
                return result;
            }
            if (!terminal) {
                if (context->subscribers.size() >= kMaxSubscribersPerStream) {
                    result.status = RealtimeSubscribeStatus::StreamUnavailable;
                    return result;
                }
                subscriber.id = context->next_subscriber_id++;
                context->subscribers.push_back(subscriber);
                registered = true;
            }
            result.status = RealtimeSubscribeStatus::Subscribed;
        }

        if (replay_pending && !drainSubscriber(subscriber) && registered) {
            std::lock_guard<std::mutex> lock(context->mutex);
            context->subscribers.erase(
                std::remove_if(
                    context->subscribers.begin(),
                    context->subscribers.end(),
                    [&subscriber](const EventSubscriber& current) {
                        return current.id == subscriber.id;
                    }
                ),
                context->subscribers.end()
            );
        }
        return result;
    }

    struct CapturedFrame {
        int64_t frame_index = 0;
        double timestamp_ms = 0.0;
        std::chrono::steady_clock::time_point captured_at;
        cv::Mat frame;
    };

    struct StreamContext {
        std::string stream_id;
        std::string source;
        std::string public_source;
        bool live_source = false;
        // Protected by the registry mutex; exactly one caller owns removal/join.
        bool removal_in_progress = false;

        mutable std::mutex mutex;
        std::condition_variable frame_ready;
        std::deque<CapturedFrame> frames;
        std::deque<RealtimeFrameEvent> events;
        std::vector<EventSubscriber> subscribers;
        std::thread decoder;
        std::thread processor;
        bool stop_requested = false;
        bool decoder_finished = false;

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
        uint64_t skipped_inference_count = 0;
        uint64_t inference_error_count = 0;
        uint64_t consecutive_inference_error_count = 0;
        uint64_t reconnect_count = 0;
        StreamInferenceDiagnosticsByTier async_inference_diagnostics{};
        StreamProcessingDiagnostics processing_diagnostics{};
        uint64_t weak_flow_roi_count = 0;
        uint64_t weak_flow_roi_pixels = 0;
        uint64_t weak_flow_sampled_points = 0;
        // Processing-thread owned; weak identity survives replay teardown without retaining tensors.
        std::array<ScheduledInferenceTicket, 2> inference_tickets{};
        size_t max_queue_length = 0;
        uint64_t next_sequence = 1;
        uint64_t next_subscriber_id = 1;
        std::chrono::steady_clock::time_point latest_result_captured_at;
    };

    class ProcessingTimer {
    public:
        ProcessingTimer(StreamContext& context, StreamProcessingStage stage)
            : context_(context), stage_(stage), started_(std::chrono::steady_clock::now()) {}
        ~ProcessingTimer() { finish(); }
        ProcessingTimer(const ProcessingTimer&) = delete;
        ProcessingTimer& operator=(const ProcessingTimer&) = delete;

        void finish() {
            if (!active_) {
                return;
            }
            const double elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started_
            ).count();
            std::lock_guard<std::mutex> lock(context_.mutex);
            context_.processing_diagnostics[static_cast<size_t>(stage_)].observe(elapsed_ms);
            active_ = false;
        }

    private:
        StreamContext& context_;
        StreamProcessingStage stage_;
        std::chrono::steady_clock::time_point started_;
        bool active_ = true;
    };

    static void observePreparation(StreamContext& context, const PreparedStreamFrame& frame) {
        constexpr size_t first = static_cast<size_t>(StreamProcessingStage::Gray);
        constexpr size_t weak_first = static_cast<size_t>(StreamProcessingStage::FrameDiff);
        std::lock_guard<std::mutex> lock(context.mutex);
        for (size_t i = 0; i < frame.preparation_ms.size(); ++i) {
            if (frame.preparation_ms[i]) {
                context.processing_diagnostics[first + i].observe(*frame.preparation_ms[i]);
            }
        }
        for (size_t i = 0; i < frame.weak.stage_ms.size(); ++i) {
            if (frame.weak.stage_ms[i]) {
                context.processing_diagnostics[weak_first + i].observe(*frame.weak.stage_ms[i]);
            }
        }
        context.weak_flow_roi_count += frame.weak.roi_count;
        context.weak_flow_roi_pixels += frame.weak.roi_pixels;
        context.weak_flow_sampled_points += frame.weak.quality.sampled_point_count;
    }


    std::shared_ptr<StreamContext> find(const std::string& stream_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = streams_.find(stream_id);
        return found == streams_.end() ? nullptr : found->second;
    }

    static RealtimeStreamSnapshot snapshotOf(
        const std::shared_ptr<StreamContext>& context
    ) {
        std::lock_guard<std::mutex> lock(context->mutex);
        RealtimeStreamSnapshot snapshot;
        snapshot.stream_id = context->stream_id;
        snapshot.source = context->public_source;
        snapshot.status = context->status;
        snapshot.last_error = context->last_error;
        snapshot.width = context->width;
        snapshot.height = context->height;
        snapshot.source_fps = context->source_fps;
        snapshot.decoded_frame_count = context->decoded_frame_count;
        snapshot.processed_frame_count = context->processed_frame_count;
        snapshot.detection_frame_count = context->detection_frame_count;
        snapshot.high_res_detection_count = context->high_res_detection_count;
        snapshot.roi_high_res_detection_count =
            context->roi_high_res_detection_count;
        snapshot.low_res_detection_count = context->low_res_detection_count;
        snapshot.dropped_frame_count = context->dropped_frame_count;
        snapshot.decoder_queue_drop_count = context->decoder_queue_drop_count;
        snapshot.processor_coalesced_frame_count =
            context->processor_coalesced_frame_count;
        snapshot.stale_frame_drop_count = context->stale_frame_drop_count;
        snapshot.skipped_inference_count = context->skipped_inference_count;
        snapshot.inference_error_count = context->inference_error_count;
        snapshot.consecutive_inference_error_count =
            context->consecutive_inference_error_count;
        snapshot.reconnect_count = context->reconnect_count;
        snapshot.queue_length = context->frames.size();
        snapshot.max_queue_length = context->max_queue_length;
        snapshot.latest_sequence = context->next_sequence - 1;
        snapshot.async_inference_diagnostics = context->async_inference_diagnostics;
        snapshot.processing_diagnostics = context->processing_diagnostics;
        snapshot.weak_flow_roi_count = context->weak_flow_roi_count;
        snapshot.weak_flow_roi_pixels = context->weak_flow_roi_pixels;
        snapshot.weak_flow_sampled_points = context->weak_flow_sampled_points;
        if (context->processed_frame_count > 0) {
            snapshot.latest_result_age_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - context->latest_result_captured_at
            ).count();
        }
        return snapshot;
    }

    bool setStatus(
        const std::shared_ptr<StreamContext>& context,
        std::string status,
        std::string error = {}
    ) {
        std::lock_guard<std::mutex> lock(context->mutex);
        if (context->stop_requested) {
            return false;
        }
        context->status = std::move(status);
        context->last_error = std::move(error);
        return true;
    }

    bool expireIfNeeded(
        const std::shared_ptr<StreamContext>& context,
        std::chrono::steady_clock::time_point started_at
    ) {
        if (config_.max_stream_duration_seconds == 0
            || std::chrono::steady_clock::now() - started_at
                < std::chrono::seconds(config_.max_stream_duration_seconds)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            if (context->stop_requested) {
                return true;
            }
            context->stop_requested = true;
            context->status = "expired";
            context->last_error = "maximum stream duration reached";
            context->frames.clear();
        }
        context->frame_ready.notify_all();
        logStreamEvent(StreamLogEvent::LifetimeExpired, snapshotOf(context));
        return true;
    }

    bool waitForReconnect(const std::shared_ptr<StreamContext>& context) {
        std::unique_lock<std::mutex> lock(context->mutex);
        return context->frame_ready.wait_for(
            lock,
            kReconnectDelay,
            [&context]() { return context->stop_requested; }
        );
    }

    void failWorker(const std::shared_ptr<StreamContext>& context, bool decoder) noexcept {
        bool report = false;
        try {
            std::lock_guard<std::mutex> lock(context->mutex);
            // Cancellation or an earlier terminal cause wins over late worker failures.
            if (!context->stop_requested && !isTerminalRealtimeStatus(context->status)) {
                context->stop_requested = true;
                context->frames.clear();
                context->status = "failed";
                report = true;
                // Backend exception text may contain credentials; use a fixed reason.
                context->last_error = decoder ? "decoder worker failed" : "processor worker failed";
            }
        } catch (...) {
            // Best effort if even failure reporting cannot allocate.
        }
        context->frame_ready.notify_all();
        if (report) {
            try {
                logStreamEvent(decoder ? StreamLogEvent::DecoderFailed : StreamLogEvent::ProcessorFailed,
                               snapshotOf(context));
            } catch (...) {
                // A snapshot allocation failure must not escape the thread boundary.
            }
        }
    }

    void runWorker(const std::shared_ptr<StreamContext>& context, bool decoder) noexcept {
        try {
            if (decoder) {
                decodeLoop(context);
            } else {
                processLoop(context);
            }
        } catch (...) {
            failWorker(context, decoder);
        }
        try {
            if (decoder) {
                markDecoderFinished(context);
            } else {
                finishProcessing(context);
            }
        } catch (...) {
            // Terminal notification is best effort; never terminate other streams.
            failWorker(context, decoder);
        }
    }

    void decodeLoop(const std::shared_ptr<StreamContext>& context) {
        const auto stream_started = std::chrono::steady_clock::now();
        int64_t frame_index = 0;
        uint64_t consecutive_open_failures = 0;

        while (true) {
            if (expireIfNeeded(context, stream_started)) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                if (context->stop_requested) {
                    return;
                }
                context->status = context->reconnect_count == 0
                    ? "connecting"
                    : "reconnecting";
            }

            cv::VideoCapture capture;
            bool opened = false;
            if (context->live_source) {
                const std::vector<int> parameters{
                    cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
                    cv::CAP_PROP_READ_TIMEOUT_MSEC, 2000,
                };
                opened = capture.open(context->source, cv::CAP_ANY, parameters);
            } else {
                opened = capture.open(context->source);
            }
            if (!opened) {
                if (!setStatus(context, context->live_source ? "reconnecting" : "failed",
                               "failed to open video source")) {
                    return;
                }
                if (shouldLogRepeatedFailure(++consecutive_open_failures)) {
                    logStreamEvent(StreamLogEvent::SourceUnavailable, snapshotOf(context));
                }
                if (!context->live_source || waitForReconnect(context)) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(context->mutex);
                    ++context->reconnect_count;
                }
                continue;
            }

            consecutive_open_failures = 0;
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                if (context->stop_requested) {
                    return;
                }
                context->status = "running";
                context->last_error.clear();
                context->source_fps = std::max(0.0, capture.get(cv::CAP_PROP_FPS));
                context->width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
                context->height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
            }

            logStreamEvent(StreamLogEvent::SourceConnected, snapshotOf(context));
            cv::Mat frame;
            while (capture.read(frame)) {
                const auto captured_at = std::chrono::steady_clock::now();
                const double capture_timestamp_ms = capture.get(cv::CAP_PROP_POS_MSEC);
                double timestamp_ms = capture_timestamp_ms;
                if (!(timestamp_ms > 0.0)) {
                    const double fps = capture.get(cv::CAP_PROP_FPS);
                    timestamp_ms = fps > 0.0
                        ? static_cast<double>(frame_index) * 1000.0 / fps
                        : std::chrono::duration<double, std::milli>(
                            captured_at - stream_started
                        ).count();
                }

                if (expireIfNeeded(context, stream_started)) {
                    return;
                }
                {
                    std::unique_lock<std::mutex> lock(context->mutex);
                    const size_t depth =
                        static_cast<size_t>(config_.per_stream_queue_depth);
                    if (!context->live_source) {
                        context->frame_ready.wait(lock, [&context, depth]() {
                            return context->stop_requested
                                || context->frames.size() < depth;
                        });
                    }
                    if (context->stop_requested) {
                        return;
                    }
                    while (context->live_source && context->frames.size() >= depth) {
                        context->frames.pop_front();
                        ++context->decoder_queue_drop_count;
                        ++context->dropped_frame_count;
                    }
                    context->frames.push_back(CapturedFrame{
                        frame_index,
                        timestamp_ms,
                        captured_at,
                        frame.clone()
                    });
                    ++context->decoded_frame_count;
                    context->width = frame.cols;
                    context->height = frame.rows;
                    context->max_queue_length = std::max(
                        context->max_queue_length,
                        context->frames.size()
                    );
                }
                context->frame_ready.notify_all();
                ++frame_index;
            }

            capture.release();
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                if (context->stop_requested) {
                    return;
                }
                if (context->live_source) {
                    ++context->reconnect_count;
                    context->status = "reconnecting";
                    context->last_error = "video source disconnected";
                }
            }
            if (!context->live_source) {
                return;
            }
            logStreamEvent(StreamLogEvent::SourceDisconnected, snapshotOf(context));
            if (waitForReconnect(context)) {
                return;
            }
        }
    }

    static void markDecoderFinished(const std::shared_ptr<StreamContext>& context) {
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            context->decoder_finished = true;
        }
        context->frame_ready.notify_all();
    }

    CapturedFrame takeNextFrame(
        const std::shared_ptr<StreamContext>& context,
        bool& finished
    ) {
        std::unique_lock<std::mutex> lock(context->mutex);
        context->frame_ready.wait(lock, [&context]() {
            return context->stop_requested
                || context->decoder_finished
                || !context->frames.empty();
        });
        if (context->stop_requested) {
            context->frames.clear();
            finished = true;
            return {};
        }
        if (context->frames.empty()) {
            finished = context->decoder_finished;
            return {};
        }

        CapturedFrame frame;
        if (context->live_source) {
            frame = std::move(context->frames.back());
            const size_t coalesced = context->frames.size() - 1;
            context->processor_coalesced_frame_count += coalesced;
            context->dropped_frame_count += coalesced;
            context->frames.clear();
        } else {
            frame = std::move(context->frames.front());
            context->frames.pop_front();
        }
        finished = false;
        lock.unlock();
        context->frame_ready.notify_all();
        return frame;
    }

    bool discardExpiredFrame(
        const std::shared_ptr<StreamContext>& context,
        const CapturedFrame& frame
    ) {
        if (!isFrameExpired(context->live_source, config_.max_result_age_ms,
                            frame.captured_at, std::chrono::steady_clock::now())) {
            return false;
        }
        std::lock_guard<std::mutex> lock(context->mutex);
        ++context->stale_frame_drop_count;
        ++context->dropped_frame_count;
        return true;
    }

    TensorInput prepareModel(
        const CapturedFrame& frame, bool high_res, const cv::Rect* high_res_crop,
        double* preprocess_ms
    ) {
        const AppConfig model_config = high_res
            ? makeHighResAppConfig(config_)
            : makeLowResAppConfig(config_);
        const cv::Mat model_frame = high_res_crop != nullptr
            ? frame.frame(*high_res_crop)
            : frame.frame;
        const auto preprocess_started = std::chrono::steady_clock::now();
        auto input = preprocessImageMat(model_frame, model_config);
        if (preprocess_ms != nullptr) {
            *preprocess_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - preprocess_started
            ).count();
        }
        if (!input.has_value()) {
            throw std::runtime_error("failed to preprocess realtime frame");
        }

        return std::move(input.value());
    }

    std::future<ScheduledInferenceResult> submitModel(
        const CapturedFrame& frame, bool high_res, bool urgent,
        const std::string& stream_id, const cv::Rect* high_res_crop,
        double* preprocess_ms = nullptr, ScheduledInferenceTicket* ticket = nullptr
    ) {
        auto input = prepareModel(frame, high_res, high_res_crop, preprocess_ms);
        InferenceContext context;
        context.stream_id = stream_id;
        context.frame_index = frame.frame_index;
        context.timestamp_ms = frame.timestamp_ms;

        const auto& scheduler = high_res
            ? high_res_scheduler_
            : low_res_scheduler_;
        const auto& engine = high_res
            ? high_res_engine_
            : low_res_engine_;
        if (engine == nullptr) {
            throw std::runtime_error("requested realtime model is unavailable");
        }
        if (scheduler != nullptr) {
            if (ticket != nullptr) {
                auto submitted = scheduler->submitTracked(std::move(input), std::move(context), urgent);
                *ticket = std::move(submitted.ticket);
                return std::move(submitted.result);
            }
            return scheduler->submit(std::move(input), std::move(context), urgent);
        }
        ScheduledInferenceResult result;
        result.result = engine->infer(input, std::move(context));
        result.status = ScheduledInferenceStatus::Completed;
        std::promise<ScheduledInferenceResult> promise;
        auto future = promise.get_future();
        promise.set_value(std::move(result));
        return future;
    }

    ScheduledInferenceResult runModel(
        const CapturedFrame& frame, bool high_res, bool urgent,
        const std::string& stream_id, const cv::Rect* high_res_crop
    ) {
        return submitModel(frame, high_res, urgent, stream_id, high_res_crop).get();
    }

    bool recordInferenceFailure(
        const std::shared_ptr<StreamContext>& context,
        const InferenceContext& inference_context,
        bool high_res,
        const std::string& message
    ) {
        bool circuit_open = false;
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            if (context->stop_requested) {
                return true;
            }
            context->last_error = message;
            ++context->inference_error_count;
            ++context->consecutive_inference_error_count;
            circuit_open = context->consecutive_inference_error_count
                >= static_cast<uint64_t>(config_.stream_max_consecutive_errors);
            if (circuit_open) {
                context->status = "failed";
                context->stop_requested = true;
                context->frames.clear();
            }
        }
        const auto failure = snapshotOf(context);
        if (shouldLogRepeatedFailure(failure.consecutive_inference_error_count)) {
            logStreamEvent(StreamLogEvent::InferenceFailed, failure,
                           inference_context, high_res ? "high" : "low");
        }
        if (circuit_open) {
            context->frame_ready.notify_all();
            logStreamEvent(StreamLogEvent::CircuitOpened, failure,
                           inference_context, high_res ? "high" : "low");
        }
        return circuit_open;
    }

    void processAsyncHighLow(const std::shared_ptr<StreamContext>& context) {
        StreamProcessor processor(true);
        StreamInferenceReplay replay(context->stream_id, config_.max_result_age_ms);
        DetectionCadence cadence(config_.video_detect_fps, config_.video_high_detect_fps, true);
        std::optional<std::chrono::steady_clock::time_point> cadence_started;
        bool force_high_res = true;
        bool pending_authority_refresh = false;
        int64_t last_high_submit_frame_index = -1;
        int64_t high_res_detection_count = 0;

        while (true) {
            bool finished = false;
            CapturedFrame frame = takeNextFrame(context, finished);
            if (finished) {
                break;
            }
            if (frame.frame.empty() || discardExpiredFrame(context, frame)) {
                continue;
            }
            ProcessingTimer frame_timer(*context, StreamProcessingStage::FrameWork);

            ProcessingTimer poll_timer(*context, StreamProcessingStage::Poll);
            auto polled = replay.poll();
            poll_timer.finish();
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                if (context->stop_requested) {
                    break;
                }
                context->skipped_inference_count += polled.skipped_count;
                for (size_t index = 0; index < polled.diagnostics.size(); ++index) {
                    context->async_inference_diagnostics[index].merge(polled.diagnostics[index]);
                }
                if (polled.completed_count > 0) {
                    context->consecutive_inference_error_count = 0;
                    context->last_error.clear();
                }
            }
            bool circuit_open = false;
            for (const auto& failure : polled.failed) {
                if (recordInferenceFailure(context, failure.context,
                                           failure.high_res, failure.message)) {
                    circuit_open = true;
                    break;
                }
            }
            if (circuit_open) {
                break;
            }
            if (polled.tracker) {
                processor.restoreAuthority(std::move(*polled.tracker));
            }
            for (const auto& applied : polled.applied) {
                if (applied.high_res) {
                    ++high_res_detection_count;
                    force_high_res = false;
                    pending_authority_refresh = false;
                }
            }

            ProcessingTimer prepare_timer(*context, StreamProcessingStage::Prepare);
            auto prepared = processor.prepareFrame(frame.frame, frame.frame_index, true);
            prepare_timer.finish();
            observePreparation(*context, prepared);
            if (isSevereWeakQualityDrop(prepared.weak.quality)) {
                force_high_res = true;
            }
            pending_authority_refresh = processor.authority().consumeHighResRefreshRequest()
                || pending_authority_refresh;
            force_high_res = force_high_res || pending_authority_refresh;
            if (discardExpiredFrame(context, frame)) {
                continue;
            }

            AuthorityReplayFrame historical;
            historical.frame_index = frame.frame_index;
            historical.image_width = frame.frame.cols;
            historical.image_height = frame.frame.rows;
            historical.motion = prepared.motion;
            historical.direct_motions = prepared.direct_motions;
            historical.allow_global_motion = !isSevereWeakQualityDrop(prepared.weak.quality);
            replay.append(std::move(historical));

            const InferenceContext inference_context{
                context->stream_id, frame.frame_index, frame.timestamp_ms
            };
            for (const bool refresh_high : {true, false}) {
                auto& scheduler = refresh_high ? high_res_scheduler_ : low_res_scheduler_;
                if (!replay.queued(refresh_high, *scheduler)) {
                    continue; // Do not preprocess a replacement for an in-flight task.
                }
                const auto refresh_roi = refresh_high
                    ? selectHighResRoi(config_, frame.frame.size(), force_high_res,
                                       high_res_detection_count)
                    : std::optional<HighResRoiSelection>{};
                try {
                    double preprocess_ms = 0.0;
                    auto input = prepareModel(frame, refresh_high,
                        refresh_roi ? &refresh_roi->crop : nullptr, &preprocess_ms);
                    replay.updateQueued(*scheduler, std::move(input), inference_context,
                                        frame.captured_at, refresh_high, refresh_roi, preprocess_ms);
                } catch (const std::exception& error) {
                    if (recordInferenceFailure(context, inference_context,
                                               refresh_high, error.what())) {
                        circuit_open = true;
                        break;
                    }
                }
            }
            if (circuit_open) {
                break;
            }

            if (!cadence_started) {
                cadence_started = frame.captured_at;
            }
            // Model budgets use the live wall clock, not a decoder's nominal FPS/PTS.
            const double cadence_ms = std::chrono::duration<double, std::milli>(
                frame.captured_at - *cadence_started
            ).count();
            const bool urgent = force_high_res && !replay.busy(true)
                && (last_high_submit_frame_index < 0
                    || frame.frame_index - last_high_submit_frame_index >= kMinHighResCadenceFrames);
            const DetectionTier tier = cadence.select(
                cadence_ms, urgent, !replay.busy(true), !replay.busy(false)
            );
            const bool high_res = tier == DetectionTier::High;
            if (tier != DetectionTier::None) {
                const auto roi = high_res
                    ? selectHighResRoi(config_, frame.frame.size(), force_high_res,
                                       high_res_detection_count)
                    : std::optional<HighResRoiSelection>{};
                try {
                    double preprocess_ms = 0.0;
                    ScheduledInferenceTicket ticket;
                    auto future = submitModel(
                        frame, high_res, urgent, context->stream_id,
                        roi ? &roi->crop : nullptr, &preprocess_ms, &ticket
                    );
                    context->inference_tickets[high_res ? 0 : 1] = ticket;
                    replay.submit(std::move(future), inference_context, frame.captured_at,
                                  high_res, roi, preprocess_ms, std::move(ticket));
                    if (high_res) {
                        last_high_submit_frame_index = frame.frame_index;
                    }
                } catch (const std::exception& error) {
                    if (recordInferenceFailure(context, inference_context,
                                               high_res, error.what())) {
                        break;
                    }
                }
            }

            RealtimeFrameEvent event;
            event.frame_index = frame.frame_index;
            event.timestamp_ms = frame.timestamp_ms;
            event.tracks = processor.applyFlow(prepared);
            processor.finishFrame(std::move(prepared));
            for (auto& applied : polled.applied) {
                event.inference_updates.push_back({
                    std::move(applied.context), applied.high_res ? "high" : "low",
                    applied.high_res_roi
                });
                event.high_res_roi = event.high_res_roi || applied.high_res_roi;
                if (applied.high_res || !event.detection_frame) {
                    event.model_tier = applied.high_res ? "high" : "low";
                }
                event.detection_frame = true;
            }
            ProcessingTimer publish_timer(*context, StreamProcessingStage::Publish);
            publish(context, std::move(event), frame.captured_at);
        }
    }

    void processAsyncSingleModel(const std::shared_ptr<StreamContext>& context) {
        StreamProcessor processor(false);
        SingleModelInferenceReplay replay(context->stream_id, config_.max_result_age_ms);
        DetectionCadence cadence(config_.video_detect_fps, config_.video_high_detect_fps, false);
        std::optional<std::chrono::steady_clock::time_point> cadence_started;
        bool force_high_res = true;

        while (true) {
            bool finished = false;
            CapturedFrame frame = takeNextFrame(context, finished);
            if (finished) {
                break;
            }
            if (frame.frame.empty() || discardExpiredFrame(context, frame)) {
                continue;
            }
            ProcessingTimer frame_timer(*context, StreamProcessingStage::FrameWork);
            ProcessingTimer poll_timer(*context, StreamProcessingStage::Poll);
            auto polled = replay.poll();
            poll_timer.finish();
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                if (context->stop_requested) {
                    break;
                }
                context->skipped_inference_count += polled.skipped_count;
                context->async_inference_diagnostics[0].merge(polled.diagnostics);
                if (polled.completed_count > 0 && polled.failed.empty()) {
                    context->consecutive_inference_error_count = 0;
                    context->last_error.clear();
                }
            }
            bool circuit_open = false;
            for (const auto& failure : polled.failed) {
                if (recordInferenceFailure(context, failure.context, true, failure.message)) {
                    circuit_open = true;
                    break;
                }
            }
            if (circuit_open) {
                break;
            }
            if (polled.tracker) {
                processor.restoreSingleModel(std::move(*polled.tracker), std::move(polled.tracks));
                force_high_res = false;
            }
            ProcessingTimer prepare_timer(*context, StreamProcessingStage::Prepare);
            auto prepared = processor.prepareFrame(frame.frame, frame.frame_index, false);
            prepare_timer.finish();
            observePreparation(*context, prepared);
            force_high_res = force_high_res || isSevereWeakQualityDrop(prepared.weak.quality);
            if (discardExpiredFrame(context, frame)) {
                continue;
            }
            if (replay.busy()) {
                replay.append(frame.frame_index, prepared.gray);
            }
            const InferenceContext inference_context{
                context->stream_id, frame.frame_index, frame.timestamp_ms
            };
            try {
                if (replay.queued(*high_res_scheduler_)) {
                    double preprocess_ms = 0.0;
                    auto input = prepareModel(frame, true, nullptr, &preprocess_ms);
                    replay.updateQueued(*high_res_scheduler_, std::move(input), inference_context,
                                        frame.captured_at, preprocess_ms,
                                        processor.singleModelState(), prepared.gray);
                }
                if (!cadence_started) {
                    cadence_started = frame.captured_at;
                }
                const double cadence_ms = std::chrono::duration<double, std::milli>(
                    frame.captured_at - *cadence_started
                ).count();
                if (cadence.select(cadence_ms, force_high_res, !replay.busy(), false)
                    == DetectionTier::High) {
                    replay.resetBase(processor.singleModelState());
                    replay.append(frame.frame_index, prepared.gray);
                    double preprocess_ms = 0.0;
                    ScheduledInferenceTicket ticket;
                    auto future = submitModel(frame, true, force_high_res, context->stream_id,
                                              nullptr, &preprocess_ms, &ticket);
                    context->inference_tickets[0] = ticket;
                    replay.submit(std::move(future), inference_context, frame.captured_at,
                                  preprocess_ms, std::move(ticket));
                }
            } catch (const std::exception& error) {
                if (recordInferenceFailure(context, inference_context, true, error.what())) {
                    break;
                }
            }
            RealtimeFrameEvent event;
            event.frame_index = frame.frame_index;
            event.timestamp_ms = frame.timestamp_ms;
            event.tracks = processor.applyFlow(prepared);
            processor.finishFrame(std::move(prepared));
            for (auto& applied : polled.applied) {
                event.inference_updates.push_back({std::move(applied.context), "high", false});
                event.detection_frame = true;
                event.model_tier = "high";
            }
            ProcessingTimer publish_timer(*context, StreamProcessingStage::Publish);
            publish(context, std::move(event), frame.captured_at);
        }
    }

    void processLoop(const std::shared_ptr<StreamContext>& context) {
        const bool high_low = low_res_engine_ != nullptr;
        if (context->live_source && !high_low && config_.video_model_async
            && high_res_scheduler_ != nullptr) {
            processAsyncSingleModel(context);
            return;
        }
        if (context->live_source && high_low && config_.video_model_async
            && high_res_scheduler_ != nullptr && low_res_scheduler_ != nullptr) {
            processAsyncHighLow(context);
            return;
        }
        StreamProcessor processor(high_low);
        auto& authority_tracker = processor.authority();
        bool pending_authority_refresh = false;
        int64_t last_high_res_frame_index = -1;
        int64_t high_res_detection_count = 0;
        DetectionCadence cadence(
            config_.video_detect_fps, config_.video_high_detect_fps,
            low_res_engine_ != nullptr
        );
        bool force_high_res = true;

        while (true) {
            bool finished = false;
            CapturedFrame frame = takeNextFrame(context, finished);
            if (finished) {
                break;
            }
            if (frame.frame.empty() || discardExpiredFrame(context, frame)) {
                continue;
            }
            ProcessingTimer frame_timer(*context, StreamProcessingStage::FrameWork);

            ProcessingTimer prepare_timer(*context, StreamProcessingStage::Prepare);
            auto prepared = processor.prepareFrame(frame.frame, frame.frame_index, high_low);
            prepare_timer.finish();
            observePreparation(*context, prepared);
            if (isSevereWeakQualityDrop(prepared.weak.quality)) {
                force_high_res = true;
            }

            if (discardExpiredFrame(context, frame)) {
                continue;
            }
            if (high_low) {
                pending_authority_refresh = authority_tracker.consumeHighResRefreshRequest()
                    || pending_authority_refresh;
                if (pending_authority_refresh
                    && (last_high_res_frame_index < 0
                        || frame.frame_index - last_high_res_frame_index >= kMinHighResCadenceFrames)) {
                    force_high_res = true;
                }
            }
            const auto apply_flow = [&]() -> const std::vector<TrackedDetection>& {
                return processor.applyFlow(prepared);
            };
            const DetectionTier tier = cadence.select(frame.timestamp_ms, force_high_res);
            bool detection_frame = false;
            const bool used_high_res = tier == DetectionTier::High;
            const auto roi_selection = used_high_res && high_low
                ? selectHighResRoi(
                    config_, frame.frame.size(), force_high_res,
                    high_res_detection_count
                )
                : std::optional<HighResRoiSelection>{};
            std::string model_tier = "flow";
            std::vector<TrackedDetection> tracks;
            if (tier != DetectionTier::None) {
                try {
                    ScheduledInferenceResult scheduled = runModel(
                        frame, used_high_res, force_high_res, context->stream_id,
                        roi_selection.has_value() ? &roi_selection->crop : nullptr
                    );
                    {
                        std::lock_guard<std::mutex> lock(context->mutex);
                        if (context->stop_requested) {
                            break;
                        }
                    }
                    if (scheduled.status == ScheduledInferenceStatus::Completed) {
                        const bool expired = discardExpiredFrame(context, frame);
                        if (!expired) {
                            if (roi_selection.has_value()) {
                                scheduled.result.detections = translateHighResRoiDetections(
                                    scheduled.result.detections, roi_selection->crop,
                                    frame.frame.size()
                                );
                            }
                            tracks = processor.applyDetections(
                                scheduled.result.detections, frame.frame_index,
                                used_high_res, prepared.projected_tracks,
                                roi_selection.has_value()
                                    ? &roi_selection->authority_region : nullptr
                            );
                            if (high_low && used_high_res) {
                                ++high_res_detection_count;
                                last_high_res_frame_index = frame.frame_index;
                                pending_authority_refresh = false;
                            }
                            detection_frame = true;
                            model_tier = used_high_res ? "high" : "low";
                            force_high_res = false;
                        }
                        {
                            std::lock_guard<std::mutex> lock(context->mutex);
                            if (!context->stop_requested) {
                                context->consecutive_inference_error_count = 0;
                                context->last_error.clear();
                            }
                        }
                        if (expired) {
                            continue;
                        }
                    } else if (scheduled.status == ScheduledInferenceStatus::Stale
                               || scheduled.status == ScheduledInferenceStatus::Replaced) {
                        {
                            std::lock_guard<std::mutex> lock(context->mutex);
                            ++context->skipped_inference_count;
                        }
                        if (discardExpiredFrame(context, frame)) {
                            continue;
                        }
                        tracks = apply_flow();
                    } else {
                        throw std::runtime_error(
                            scheduled.error_message.empty()
                                ? "scheduled inference did not complete"
                                : scheduled.error_message
                        );
                    }
                } catch (const std::exception& e) {
                    const InferenceContext inference_context{
                        context->stream_id, frame.frame_index, frame.timestamp_ms
                    };
                    if (recordInferenceFailure(context, inference_context,
                                               used_high_res, e.what())) {
                        break;
                    }
                    if (discardExpiredFrame(context, frame)) {
                        continue;
                    }
                    tracks = apply_flow();
                }
            } else {
                tracks = apply_flow();
            }

            processor.finishFrame(std::move(prepared));
            RealtimeFrameEvent event;
            event.frame_index = frame.frame_index;
            event.timestamp_ms = frame.timestamp_ms;
            event.detection_frame = detection_frame;
            event.high_res_roi = detection_frame && roi_selection.has_value();
            event.model_tier = std::move(model_tier);
            event.tracks = std::move(tracks);

            ProcessingTimer publish_timer(*context, StreamProcessingStage::Publish);
            publish(context, std::move(event), frame.captured_at);
        }

    }

    void finishProcessing(const std::shared_ptr<StreamContext>& context) {
        // No further submissions from this worker, including EOF/error/circuit-break exits.
        if (high_res_scheduler_) {
            high_res_scheduler_->cancelQueued(context->inference_tickets[0]);
        }
        if (low_res_scheduler_) {
            low_res_scheduler_->cancelQueued(context->inference_tickets[1]);
        }
        context->inference_tickets = {};
        std::string final_status;
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            if (context->status == "failed" || context->status == "expired") {
                final_status = context->status;
            } else {
                final_status = context->stop_requested ? "stopped" : "completed";
            }
            context->status = final_status;
        }
        try {
            logStreamEvent(StreamLogEvent::Finished, snapshotOf(context));
        } catch (...) {
            // Try terminal delivery even if allocating a log snapshot failed.
        }
        RealtimeFrameEvent terminal;
        terminal.terminal = true;
        terminal.status = final_status;
        terminal.model_tier = "terminal";
        publish(context, std::move(terminal));
    }

    void publish(
        const std::shared_ptr<StreamContext>& context,
        RealtimeFrameEvent event,
        std::chrono::steady_clock::time_point captured_at = {}
    ) {
        std::vector<EventSubscriber> subscribers;
        std::vector<EventSubscriber> delivery_owners;
        std::vector<uint64_t> failed_subscribers;
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            if (!event.terminal) {
                if (context->stop_requested) {
                    return;
                }
                const auto now = std::chrono::steady_clock::now();
                if (isFrameExpired(context->live_source, config_.max_result_age_ms,
                                   captured_at, now)) {
                    ++context->stale_frame_drop_count;
                    ++context->dropped_frame_count;
                    return;
                }
                event.result_age_ms =
                    std::chrono::duration<double, std::milli>(now - captured_at).count();
                ++context->processed_frame_count;
                if (!event.inference_updates.empty()) {
                    // Count accepted source-frame corrections, not artificial output frames.
                    for (const auto& update : event.inference_updates) {
                        ++context->detection_frame_count;
                        if (update.model_tier == "high") {
                            ++context->high_res_detection_count;
                            if (update.high_res_roi) {
                                ++context->roi_high_res_detection_count;
                            }
                        } else {
                            ++context->low_res_detection_count;
                        }
                    }
                } else if (event.detection_frame) {
                    ++context->detection_frame_count;
                    if (event.model_tier == "high") {
                        ++context->high_res_detection_count;
                        if (event.high_res_roi) {
                            ++context->roi_high_res_detection_count;
                        }
                    } else {
                        ++context->low_res_detection_count;
                    }
                }
                context->latest_result_captured_at = captured_at;
            }
            event.sequence = context->next_sequence++;
            context->events.push_back(event);
            while (context->events.size() > kMaxBufferedEvents) {
                context->events.pop_front();
            }
            subscribers = context->subscribers;
            for (const auto& subscriber : subscribers) {
                const SubscriberQueueResult queued = queueSubscriber(subscriber, event);
                if (queued == SubscriberQueueResult::Deliver) {
                    delivery_owners.push_back(subscriber);
                } else if (queued == SubscriberQueueResult::Inactive) {
                    failed_subscribers.push_back(subscriber.id);
                }
            }
        }

        for (const auto& subscriber : delivery_owners) {
            if (!drainSubscriber(subscriber)) {
                failed_subscribers.push_back(subscriber.id);
            }
        }
        if (failed_subscribers.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(context->mutex);
        context->subscribers.erase(
            std::remove_if(
                context->subscribers.begin(),
                context->subscribers.end(),
                [&failed_subscribers](const EventSubscriber& subscriber) {
                    return std::find(
                        failed_subscribers.begin(),
                        failed_subscribers.end(),
                        subscriber.id
                    ) != failed_subscribers.end();
                }
            ),
            context->subscribers.end()
        );
    }

    static void stopContext(const std::shared_ptr<StreamContext>& context) {
        bool log_stop = false;
        {
            std::lock_guard<std::mutex> lock(context->mutex);
            log_stop = !context->stop_requested && !isTerminalRealtimeStatus(context->status);
            context->stop_requested = true;
            if (!isTerminalRealtimeStatus(context->status)) {
                context->status = "stopping";
            }
            context->frames.clear();
        }
        context->frame_ready.notify_all();
        if (log_stop) {
            logStreamEvent(StreamLogEvent::StopRequested, snapshotOf(context));
        }

        if (context->decoder.joinable()
            && context->decoder.get_id() != std::this_thread::get_id()) {
            context->decoder.join();
        }
        if (context->processor.joinable()
            && context->processor.get_id() != std::this_thread::get_id()) {
            context->processor.join();
        }
    }

    std::shared_ptr<YoloEngine> high_res_engine_;
    std::shared_ptr<YoloEngine> low_res_engine_;
    std::shared_ptr<InferenceScheduler> high_res_scheduler_;
    std::shared_ptr<InferenceScheduler> low_res_scheduler_;
    AppConfig config_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<StreamContext>> streams_;
    RealtimeStreamCounters retired_counters_;
};

RealtimeStreamManager::RealtimeStreamManager(
    std::shared_ptr<YoloEngine> high_res_engine,
    std::shared_ptr<YoloEngine> low_res_engine,
    std::shared_ptr<InferenceScheduler> high_res_scheduler,
    std::shared_ptr<InferenceScheduler> low_res_scheduler,
    AppConfig config
) : impl_(std::make_unique<Impl>(
        std::move(high_res_engine),
        std::move(low_res_engine),
        std::move(high_res_scheduler),
        std::move(low_res_scheduler),
        std::move(config)
    )) {}

RealtimeStreamManager::~RealtimeStreamManager() = default;

bool RealtimeStreamManager::create(
    const std::string& stream_id,
    const std::string& source,
    std::string& error_message
) {
    return impl_->create(stream_id, source, error_message);
}

bool RealtimeStreamManager::stop(const std::string& stream_id) {
    return impl_->stop(stream_id);
}

bool RealtimeStreamManager::get(
    const std::string& stream_id,
    RealtimeStreamSnapshot& snapshot
) const {
    return impl_->get(stream_id, snapshot);
}

std::vector<RealtimeStreamSnapshot> RealtimeStreamManager::list() const {
    return impl_->list();
}

RealtimeStreamMetrics RealtimeStreamManager::metrics() const {
    return impl_->metrics();
}

bool RealtimeStreamManager::subscribe(
    const std::string& stream_id,
    RealtimeEventCallback callback
) {
    return impl_->subscribe(stream_id, std::move(callback));
}

RealtimeSubscribeResult RealtimeStreamManager::subscribeAfter(
    const std::string& stream_id,
    uint64_t last_event_sequence,
    RealtimeEventCallback callback
) {
    return impl_->subscribeAfter(
        stream_id, last_event_sequence, std::move(callback)
    );
}

}  // namespace yolo
