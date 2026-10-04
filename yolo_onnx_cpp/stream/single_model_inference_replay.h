#pragma once

#include <algorithm>
#include <chrono>
#include <deque>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "stream/stream_inference_replay.h"
#include "stream/stream_processor.h"
#include "tracking/byte_tracker.h"

namespace yolo {

struct SingleModelInferencePoll {
    std::optional<ByteTracker> tracker;
    std::vector<TrackedDetection> tracks;
    std::vector<AppliedStreamInference> applied;
    std::vector<FailedStreamInference> failed;
    size_t skipped_count = 0;
    size_t completed_count = 0;
    StreamInferenceDiagnostics diagnostics;
};

// Processing-thread confined; one future and a fixed-size immutable gray history.
// Recompute flow from corrected tracks, not motions measured for different targets.
class SingleModelInferenceReplay {
public:
    static constexpr size_t kMaxReplayFrames = 32;

    SingleModelInferenceReplay(std::string stream_id, int max_result_age_ms)
        : stream_id_(std::move(stream_id)), max_result_age_ms_(max_result_age_ms) {}

    bool busy() const { return pending_.has_value(); }
    size_t frameCount() const { return history_.size(); }

    void resetBase(SingleModelStreamState state) {
        if (busy()) {
            throw std::logic_error("cannot replace single-model state with pending inference");
        }
        history_.clear();
        base_ = std::move(state);
    }

    void append(int64_t frame_index, cv::Mat gray) {
        if (gray.empty() || gray.type() != CV_8UC1
            || (!history_.empty() && frame_index <= history_.back().frame_index)) {
            throw std::logic_error("invalid single-model replay frame");
        }
        history_.push_back({frame_index, std::move(gray), std::nullopt});
        while (history_.size() > kMaxReplayFrames) {
            applyFrame(history_.front(), base_);
            history_.pop_front();
        }
    }

    void submit(std::future<ScheduledInferenceResult> result, InferenceContext context,
                std::chrono::steady_clock::time_point captured_at,
                double preprocess_ms = -1.0, ScheduledInferenceTicket ticket = {}) {
        if (!result.valid() || busy() || context.stream_id != stream_id_) {
            throw std::logic_error("invalid single-model inference slot");
        }
        pending_.emplace(Pending{std::move(result), std::move(context), captured_at,
                                 preprocess_ms, std::move(ticket)});
    }

    bool queued(const InferenceScheduler& scheduler) const {
        return pending_ && scheduler.isQueued(pending_->ticket);
    }

    bool updateQueued(InferenceScheduler& scheduler, TensorInput input,
                      InferenceContext context,
                      std::chrono::steady_clock::time_point captured_at,
                      double preprocess_ms, SingleModelStreamState state, cv::Mat gray) {
        if (!pending_ || context.stream_id != stream_id_
            || captured_at < pending_->captured_at
            || gray.empty() || gray.type() != CV_8UC1
            || !scheduler.updateQueued(pending_->ticket, std::move(input), context)) {
            return false;
        }
        pending_->context = std::move(context);
        pending_->captured_at = captured_at;
        pending_->preprocess_ms = preprocess_ms;
        // Only a successful queued replacement invalidates the old source prefix.
        history_.clear();
        base_ = std::move(state);
        append(pending_->context.frame_index, std::move(gray));
        return true;
    }

    SingleModelInferencePoll poll(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()
    ) {
        SingleModelInferencePoll output;
        if (!pending_ || pending_->result.wait_for(std::chrono::milliseconds(0))
                             != std::future_status::ready) {
            return output;
        }
        const auto started = std::chrono::steady_clock::now();
        Pending pending = std::move(*pending_);
        pending_.reset();
        ScheduledInferenceResult scheduled;
        try {
            scheduled = pending.result.get();
        } catch (const std::exception& error) {
            output.failed.push_back({pending.context, true, error.what()});
            return output;
        }
        if (scheduled.status == ScheduledInferenceStatus::Stale
            || scheduled.status == ScheduledInferenceStatus::Replaced) {
            ++output.skipped_count;
            return output;
        }
        if (scheduled.status != ScheduledInferenceStatus::Completed) {
            output.failed.push_back({pending.context, true,
                scheduled.error_message.empty() ? "scheduled inference did not complete"
                                                : scheduled.error_message});
            return output;
        }
        const auto& actual = scheduled.result.context;
        if (actual.stream_id != pending.context.stream_id
            || actual.frame_index != pending.context.frame_index
            || actual.timestamp_ms != pending.context.timestamp_ms) {
            output.failed.push_back({pending.context, true, "realtime inference context mismatch"});
            return output;
        }
        ++output.completed_count;
        auto& diagnostics = output.diagnostics;
        ++diagnostics.outcomes.completed_count;
        const auto received_at = now + (std::chrono::steady_clock::now() - started);
        diagnostics.observe(StreamInferenceStage::Preprocess, pending.preprocess_ms);
        diagnostics.observe(StreamInferenceStage::QueueWait, scheduled.result.queue_wait_ms);
        diagnostics.observe(StreamInferenceStage::Execution, scheduled.execution_ms);
        diagnostics.observe(StreamInferenceStage::Infer, scheduled.result.infer_ms);
        diagnostics.observe(StreamInferenceStage::Postprocess,
                            scheduled.result.decode_ms + scheduled.result.postprocess_ms);
        diagnostics.observe(StreamInferenceStage::ResultAge,
            std::chrono::duration<double, std::milli>(received_at - pending.captured_at).count());
        if (scheduled.completed_at != std::chrono::steady_clock::time_point{}) {
            diagnostics.observe(StreamInferenceStage::CompletionPickup,
                std::chrono::duration<double, std::milli>(
                    received_at - scheduled.completed_at).count());
        }
        if (isFrameExpired(true, max_result_age_ms_, pending.captured_at, received_at)) {
            ++diagnostics.outcomes.expired_count;
            ++output.skipped_count;
            return output;
        }
        auto anchor = std::find_if(history_.begin(), history_.end(),
            [&pending](const Frame& frame) { return frame.frame_index == pending.context.frame_index; });
        if (anchor == history_.end()) {
            ++diagnostics.outcomes.evicted_count;
            ++output.skipped_count;
            return output;
        }
        auto previous_detections = std::move(anchor->detections);
        anchor->detections = std::move(scheduled.result.detections);
        State replayed = base_;
        const auto replay_started = std::chrono::steady_clock::now();
        try {
            for (const auto& frame : history_) {
                applyFrame(frame, replayed);
            }
        } catch (const std::exception& error) {
            anchor->detections = std::move(previous_detections);
            output.failed.push_back({pending.context, true, error.what()});
            return output;
        }
        const auto finished = std::chrono::steady_clock::now();
        const auto committed_at = now + (finished - started);
        diagnostics.observe(StreamInferenceStage::Replay,
            std::chrono::duration<double, std::milli>(finished - replay_started).count());
        diagnostics.observe(StreamInferenceStage::CommitAge,
            std::chrono::duration<double, std::milli>(committed_at - pending.captured_at).count());
        if (isFrameExpired(true, max_result_age_ms_, pending.captured_at, committed_at)) {
            anchor->detections = std::move(previous_detections);
            ++diagnostics.outcomes.expired_count;
            ++output.skipped_count;
            return output;
        }
        ++diagnostics.outcomes.applied_count;
        output.tracker = std::move(replayed.tracker);
        output.tracks = std::move(replayed.tracks);
        output.applied.push_back({pending.context, true, false});
        return output;
    }

private:
    struct Frame {
        int64_t frame_index;
        cv::Mat gray;
        std::optional<std::vector<Detection>> detections;
    };
    using State = SingleModelStreamState;
    struct Pending {
        std::future<ScheduledInferenceResult> result;
        InferenceContext context;
        std::chrono::steady_clock::time_point captured_at;
        double preprocess_ms;
        ScheduledInferenceTicket ticket;
    };

    static void applyFrame(const Frame& frame, State& state) {
        if (frame.detections) {
            state.tracks = state.tracker.update(*frame.detections);
        } else {
            WeakTrackResult weak;
            if (!state.gray.empty() && !state.tracks.empty()) {
                weak = weakTrackWithOpticalFlow(
                    state.gray, frame.gray, state.tracks, frame.gray.cols, frame.gray.rows
                );
            }
            state.tracks = state.tracker.updateTracked(weak.tracks);
        }
        state.gray = frame.gray;
    }

    std::string stream_id_;
    int max_result_age_ms_;
    std::optional<Pending> pending_;
    std::deque<Frame> history_;
    State base_;
};

}  // namespace yolo
