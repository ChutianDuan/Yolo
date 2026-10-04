#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "model/inference_scheduler.h"
#include "stream/authority_replay.h"
#include "stream/frame_deadline.h"
#include "stream/high_res_roi.h"
#include "stream/inference_diagnostics.h"

namespace yolo {

struct AppliedStreamInference {
    InferenceContext context;
    bool high_res = false;
    bool high_res_roi = false;
};

struct FailedStreamInference {
    InferenceContext context;
    bool high_res = false;
    std::string message;
};

struct StreamInferencePoll {
    std::vector<AppliedStreamInference> applied;
    std::vector<FailedStreamInference> failed;
    std::optional<AuthorityTracker> tracker;
    size_t skipped_count = 0;
    size_t completed_count = 0;
    StreamInferenceDiagnosticsByTier diagnostics{};
};

// Processing-thread confined. Futures come from the shared scheduler, not std::async.
class StreamInferenceReplay {
public:
    StreamInferenceReplay(std::string stream_id, int max_result_age_ms)
        : stream_id_(std::move(stream_id)), max_result_age_ms_(max_result_age_ms) {}

    bool busy(bool high_res) const {
        return pending_[high_res ? 0 : 1].has_value();
    }

    size_t frameCount() const { return history_.size(); }

    void append(AuthorityReplayFrame frame) {
        if (!history_.empty() && frame.frame_index <= history_.back().frame_index) {
            throw std::logic_error("replay frames must be strictly ordered");
        }
        history_.push_back(std::move(frame));
        trimReplayBuffer(history_, base_tracker_, kMaxReplayFrames);
    }

    void submit(
        std::future<ScheduledInferenceResult> result,
        InferenceContext context,
        std::chrono::steady_clock::time_point captured_at,
        bool high_res,
        std::optional<HighResRoiSelection> roi = std::nullopt,
        double preprocess_ms = -1.0,
        ScheduledInferenceTicket ticket = {}
    ) {
        if (!result.valid() || busy(high_res) || context.stream_id != stream_id_) {
            throw std::logic_error("invalid realtime inference slot");
        }
        pending_[high_res ? 0 : 1].emplace(Pending{
            std::move(result), std::move(context), captured_at, std::move(roi), preprocess_ms,
            std::move(ticket)
        });
    }

    bool queued(bool high_res, const InferenceScheduler& scheduler) const {
        const auto& slot = pending_[high_res ? 0 : 1];
        return slot && scheduler.isQueued(slot->ticket);
    }

    bool updateQueued(
        InferenceScheduler& scheduler, TensorInput input, InferenceContext context,
        std::chrono::steady_clock::time_point captured_at, bool high_res,
        std::optional<HighResRoiSelection> roi = std::nullopt, double preprocess_ms = -1.0
    ) {
        auto& slot = pending_[high_res ? 0 : 1];
        if (!slot || context.stream_id != stream_id_ || captured_at < slot->captured_at
            || !scheduler.updateQueued(slot->ticket, std::move(input), context)) {
            return false;
        }
        // The scheduler may finish now, but polling is confined to this same thread.
        slot->context = std::move(context);
        slot->captured_at = captured_at;
        slot->roi = std::move(roi);
        slot->preprocess_ms = preprocess_ms;
        return true;
    }

    StreamInferencePoll poll(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()
    ) {
        StreamInferencePoll output;
        const auto polling_started = std::chrono::steady_clock::now();
        for (size_t index = 0; index < pending_.size(); ++index) {
            auto& slot = pending_[index];
            if (!slot || slot->result.wait_for(std::chrono::milliseconds(0))
                             != std::future_status::ready) {
                continue;
            }
            Pending pending = std::move(*slot);
            slot.reset();
            ScheduledInferenceResult scheduled;
            try {
                scheduled = pending.result.get();
            } catch (const std::exception& error) {
                output.failed.push_back({pending.context, index == 0, error.what()});
                continue;
            }
            if (scheduled.status == ScheduledInferenceStatus::Stale
                || scheduled.status == ScheduledInferenceStatus::Replaced) {
                ++output.skipped_count;
                continue;
            }
            if (scheduled.status != ScheduledInferenceStatus::Completed) {
                output.failed.push_back({
                    pending.context, index == 0,
                    scheduled.error_message.empty()
                        ? "scheduled inference did not complete" : scheduled.error_message
                });
                continue;
            }
            const auto& actual = scheduled.result.context;
            if (actual.stream_id != pending.context.stream_id
                || actual.frame_index != pending.context.frame_index
                || actual.timestamp_ms != pending.context.timestamp_ms) {
                output.failed.push_back({
                    pending.context, index == 0, "realtime inference context mismatch"
                });
                continue;
            }
            ++output.completed_count;
            auto& diagnostics = output.diagnostics[index];
            ++diagnostics.outcomes.completed_count;
            const auto elapsed = std::chrono::steady_clock::now() - polling_started;
            const auto received_at = now + elapsed;
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
                continue;
            }
            auto anchor = std::find_if(history_.begin(), history_.end(),
                [&pending](const AuthorityReplayFrame& frame) {
                    return frame.frame_index == pending.context.frame_index;
                });
            if (anchor == history_.end()) {
                ++diagnostics.outcomes.evicted_count;
                ++output.skipped_count;
                continue;
            }
            const auto replay_started = std::chrono::steady_clock::now();
            AuthorityReplayFrame previous = *anchor;
            if (index == 0) {
                anchor->has_high_res = true;
                anchor->high_res_is_roi = pending.roi.has_value();
                if (pending.roi) {
                    anchor->high_res_crop = pending.roi->crop;
                    anchor->high_res_region = pending.roi->authority_region;
                    scheduled.result.detections = translateHighResRoiDetections(
                        scheduled.result.detections, pending.roi->crop,
                        cv::Size(anchor->image_width, anchor->image_height)
                    );
                }
                anchor->high_res_detections = std::move(scheduled.result.detections);
            } else {
                anchor->has_low_res = true;
                anchor->low_res_detections = std::move(scheduled.result.detections);
            }
            AuthorityTracker replayed = base_tracker_;
            for (const auto& frame : history_) {
                applyReplayFrame(frame, replayed);
            }
            const auto replay_finished = std::chrono::steady_clock::now();
            const auto committed_at = now + (replay_finished - polling_started);
            diagnostics.observe(StreamInferenceStage::Replay,
                std::chrono::duration<double, std::milli>(replay_finished - replay_started).count());
            diagnostics.observe(StreamInferenceStage::CommitAge,
                std::chrono::duration<double, std::milli>(committed_at - pending.captured_at).count());
            // Do not commit a correction that exceeded its deadline during replay.
            if (isFrameExpired(true, max_result_age_ms_, pending.captured_at, committed_at)) {
                ++diagnostics.outcomes.expired_count;
                *anchor = std::move(previous);
                ++output.skipped_count;
                continue;
            }
            ++diagnostics.outcomes.applied_count;
            output.tracker = std::move(replayed);
            output.applied.push_back({pending.context, index == 0, pending.roi.has_value()});
        }
        return output;
    }

private:
    static constexpr size_t kMaxReplayFrames = 60;
    struct Pending {
        std::future<ScheduledInferenceResult> result;
        InferenceContext context;
        std::chrono::steady_clock::time_point captured_at;
        std::optional<HighResRoiSelection> roi;
        double preprocess_ms = -1.0;
        ScheduledInferenceTicket ticket;
    };

    std::string stream_id_;
    int max_result_age_ms_;
    std::array<std::optional<Pending>, 2> pending_;
    std::deque<AuthorityReplayFrame> history_;
    AuthorityTracker base_tracker_;
};

}  // namespace yolo
