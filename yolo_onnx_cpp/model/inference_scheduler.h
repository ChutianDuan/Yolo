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

// 调度器生命周期内的统计快照；stats() 在队列锁内复制。
struct InferenceSchedulerStats {
    // 累计事件：完成仅指成功推理；更新排队输入不会增加提交次数。
    uint64_t submitted_count = 0;
    uint64_t completed_count = 0;
    uint64_t replaced_count = 0;
    uint64_t updated_count = 0;
    uint64_t cancelled_count = 0; // 显式取消的排队任务，不包含模型完成或停机清队列。
    uint64_t stale_count = 0;
    uint64_t failed_count = 0;
    // 当前全局待执行任务数及历史峰值，不包含正在执行的任务。
    size_t queued_count = 0;
    size_t max_queued_count = 0;
    // 当前已出队任务数（含过期检查），以及工作线程数量。
    size_t in_flight_count = 0;
    size_t worker_count = 0;
};

// 多流共享引擎：流内串行、流间并发，排队满时以新帧替换最旧待执行帧。
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

    // 除 future 外返回弱引用票据，用于更新或取消仍在排队的请求。
    ScheduledInferenceSubmission submitTracked(
        TensorInput input, InferenceContext context, bool urgent = false
    );

    bool isQueued(const ScheduledInferenceTicket& ticket) const;
    bool cancelQueued(const ScheduledInferenceTicket& ticket);
    // 只接受同一流中更新的兼容输入；请求一旦出队就不能再修改。
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
