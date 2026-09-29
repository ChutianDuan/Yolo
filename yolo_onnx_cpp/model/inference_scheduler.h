#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <string>

#include "model/inference_types.h"

namespace yolo {

class YoloEngine;
class ScheduledTask;

// Weak task identity; it neither retains tensors nor collides with source contexts.
struct ScheduledInferenceTicket {
    std::weak_ptr<ScheduledTask> task;
};

enum class ScheduledInferenceStatus {
    Completed,
    Replaced,
    Stale,
    Failed,
    Stopped,
};

struct ScheduledInferenceResult {
    ScheduledInferenceStatus status = ScheduledInferenceStatus::Failed;
    InferResult result;
    std::string error_message;
    // Full engine call, including request acquisition/input copy/decode/postprocess.
    double execution_ms = 0.0;
    std::chrono::steady_clock::time_point completed_at{};
};

struct ScheduledInferenceSubmission {
    std::future<ScheduledInferenceResult> result;
    ScheduledInferenceTicket ticket;
};

struct InferenceSchedulerStats {
    uint64_t submitted_count = 0;
    uint64_t completed_count = 0;
    uint64_t replaced_count = 0;
    uint64_t updated_count = 0;
    uint64_t cancelled_count = 0; // Explicit queued-ticket cancellations, not model completions.
    uint64_t stale_count = 0;
    uint64_t failed_count = 0;
    size_t queued_count = 0;
    size_t max_queued_count = 0;
    size_t in_flight_count = 0;
    size_t worker_count = 0;
};

class InferenceScheduler final {
public:
    InferenceScheduler(
        std::shared_ptr<YoloEngine> engine,
        size_t worker_count,
        size_t per_stream_queue_depth,
        std::chrono::milliseconds max_request_age
    );
    ~InferenceScheduler();

    InferenceScheduler(const InferenceScheduler&) = delete;
    InferenceScheduler& operator=(const InferenceScheduler&) = delete;

    std::future<ScheduledInferenceResult> submit(
        TensorInput input,
        InferenceContext context,
        bool urgent = false
    );

    ScheduledInferenceSubmission submitTracked(
        TensorInput input, InferenceContext context, bool urgent = false
    );

    bool isQueued(const ScheduledInferenceTicket& ticket) const;
    bool cancelQueued(const ScheduledInferenceTicket& ticket);
    bool updateQueued(
        const ScheduledInferenceTicket& ticket, TensorInput input, InferenceContext context
    );

    InferResult infer(
        TensorInput input,
        InferenceContext context,
        bool urgent = false
    );

    InferenceSchedulerStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace yolo
