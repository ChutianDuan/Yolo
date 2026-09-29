#include "model/inference_scheduler.h"

#include <algorithm>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "model/yolo_engine.h"

namespace yolo {

class ScheduledTask {
public:
    TensorInput input;
    InferenceContext context;
    bool urgent = false;
    std::chrono::steady_clock::time_point submitted_at;
    std::promise<ScheduledInferenceResult> promise;
};

namespace {
constexpr size_t kMaxUrgentBurst = 3;
constexpr const char* kDefaultStreamId = "__default__";

ScheduledInferenceResult statusResult(
    ScheduledInferenceStatus status,
    std::string message
) {
    ScheduledInferenceResult result;
    result.status = status;
    result.error_message = std::move(message);
    return result;
}

}  // namespace

class InferenceScheduler::Impl {
public:
    Impl(
        std::shared_ptr<YoloEngine> engine,
        size_t worker_count,
        size_t per_stream_queue_depth,
        std::chrono::milliseconds max_request_age
    ) : engine_(std::move(engine)),
        per_stream_queue_depth_(std::max<size_t>(per_stream_queue_depth, 1)),
        max_request_age_(max_request_age) {
        if (engine_ == nullptr) {
            throw std::invalid_argument("InferenceScheduler requires an engine");
        }

        const size_t count = std::max<size_t>(worker_count, 1);
        stats_.worker_count = count;
        workers_.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            workers_.emplace_back(&Impl::run, this);
        }
    }

    ~Impl() {
        stop();
    }

    ScheduledInferenceSubmission submit(
        TensorInput input,
        InferenceContext context,
        bool urgent
    ) {
        auto task = std::make_shared<ScheduledTask>();
        task->input = std::move(input);
        task->context = std::move(context);
        task->urgent = urgent;
        task->submitted_at = std::chrono::steady_clock::now();
        auto future = task->promise.get_future();

        std::shared_ptr<ScheduledTask> replaced;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                task->promise.set_value(statusResult(
                    ScheduledInferenceStatus::Stopped,
                    "inference scheduler is stopped"
                ));
                return {std::move(future), {task}};
            }

            const std::string stream_id = normalizedStreamId(task->context.stream_id);
            auto& queue = queues_[stream_id];
            if (queue.size() >= per_stream_queue_depth_) {
                replaced = std::move(queue.front());
                queue.pop_front();
                queued_tasks_.erase(replaced.get());
                --stats_.queued_count;
                ++stats_.replaced_count;
            }

            queue.push_back(task);
            queued_tasks_.insert(task.get());
            ++stats_.submitted_count;
            ++stats_.queued_count;
            stats_.max_queued_count = std::max(
                stats_.max_queued_count,
                stats_.queued_count
            );
            if (replaced != nullptr) {
                unscheduleReadyLocked(stream_id);
            }
            scheduleReadyLocked(stream_id);
        }

        if (replaced != nullptr) {
            replaced->promise.set_value(statusResult(
                ScheduledInferenceStatus::Replaced,
                "queued inference was replaced by a newer frame"
            ));
        }
        ready_.notify_one();
        return {std::move(future), {task}};
    }

    bool isQueued(const ScheduledInferenceTicket& ticket) const {
        const auto task = ticket.task.lock();
        std::lock_guard<std::mutex> lock(mutex_);
        return !stopping_ && task && queued_tasks_.count(task.get()) != 0;
    }

    bool cancelQueued(const ScheduledInferenceTicket& ticket) {
        const auto task = ticket.task.lock();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || !task || queued_tasks_.count(task.get()) == 0) {
                return false;
            }
            const std::string stream_id = normalizedStreamId(task->context.stream_id);
            auto found = queues_.find(stream_id);
            auto& queue = found->second;
            const auto entry = std::find(queue.begin(), queue.end(), task);
            const bool was_front = entry == queue.begin();
            queue.erase(entry);
            queued_tasks_.erase(task.get());
            --stats_.queued_count;
            ++stats_.cancelled_count;
            if (queue.empty()) {
                unscheduleReadyLocked(stream_id);
                queues_.erase(found);
            } else if (was_front) {
                unscheduleReadyLocked(stream_id);
                scheduleReadyLocked(stream_id);
            }
        }
        // The dequeue/cancel race has one winner; fulfill outside the scheduler lock.
        task->promise.set_value(statusResult(
            ScheduledInferenceStatus::Stopped, "queued inference was cancelled"
        ));
        ready_.notify_one();
        return true;
    }

    bool updateQueued(
        const ScheduledInferenceTicket& ticket, TensorInput input, InferenceContext context
    ) {
        const auto task = ticket.task.lock();
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || !task || queued_tasks_.count(task.get()) == 0
            || context.stream_id != task->context.stream_id
            || context.frame_index <= task->context.frame_index
            || context.timestamp_ms < task->context.timestamp_ms
            || !std::isfinite(context.timestamp_ms)
            || input.shape != task->input.shape
            || input.values.size() != task->input.values.size()) {
            return false;
        }
        // Preserve the promise, queue position, stream identity and urgency classification.
        task->input = std::move(input);
        task->context = std::move(context);
        task->submitted_at = std::chrono::steady_clock::now();
        ++stats_.updated_count;
        return true;
    }

    InferResult infer(TensorInput input, InferenceContext context, bool urgent) {
        ScheduledInferenceResult scheduled =
            submit(std::move(input), std::move(context), urgent).result.get();
        if (scheduled.status == ScheduledInferenceStatus::Completed) {
            return std::move(scheduled.result);
        }
        throw std::runtime_error(
            scheduled.error_message.empty()
                ? "scheduled inference did not complete"
                : scheduled.error_message
        );
    }

    InferenceSchedulerStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

private:
    static std::string normalizedStreamId(const std::string& stream_id) {
        return stream_id.empty() ? kDefaultStreamId : stream_id;
    }

    void unscheduleReadyLocked(const std::string& stream_id) {
        if (ready_streams_.erase(stream_id) == 0) {
            return;
        }
        const auto remove_stream = [&stream_id](auto& ready_queue) {
            const auto found = std::find(
                ready_queue.begin(),
                ready_queue.end(),
                stream_id
            );
            if (found != ready_queue.end()) {
                ready_queue.erase(found);
            }
        };
        remove_stream(urgent_ready_);
        remove_stream(normal_ready_);
    }

    void scheduleReadyLocked(const std::string& stream_id) {
        if (stopping_
            || in_flight_streams_.find(stream_id) != in_flight_streams_.end()
            || ready_streams_.find(stream_id) != ready_streams_.end()) {
            return;
        }

        auto queue = queues_.find(stream_id);
        if (queue == queues_.end() || queue->second.empty()) {
            return;
        }

        ready_streams_.insert(stream_id);
        if (queue->second.front()->urgent) {
            urgent_ready_.push_back(stream_id);
        } else {
            normal_ready_.push_back(stream_id);
        }
    }

    std::shared_ptr<ScheduledTask> takeNext(std::string& stream_id) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this]() {
            return stopping_ || !urgent_ready_.empty() || !normal_ready_.empty();
        });
        if (stopping_) {
            return nullptr;
        }

        if (!urgent_ready_.empty()
            && (normal_ready_.empty() || urgent_burst_ < kMaxUrgentBurst)) {
            stream_id = std::move(urgent_ready_.front());
            urgent_ready_.pop_front();
            ++urgent_burst_;
        } else {
            stream_id = std::move(normal_ready_.front());
            normal_ready_.pop_front();
            urgent_burst_ = 0;
        }

        ready_streams_.erase(stream_id);
        auto queue = queues_.find(stream_id);
        if (queue == queues_.end() || queue->second.empty()) {
            return nullptr;
        }

        auto task = std::move(queue->second.front());
        queue->second.pop_front();
        queued_tasks_.erase(task.get());
        --stats_.queued_count;
        ++stats_.in_flight_count;
        in_flight_streams_.insert(stream_id);
        return task;
    }

    void finishTask(
        const std::string& stream_id,
        ScheduledInferenceStatus status
    ) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            in_flight_streams_.erase(stream_id);
            --stats_.in_flight_count;
            if (status == ScheduledInferenceStatus::Completed) {
                ++stats_.completed_count;
            } else if (status == ScheduledInferenceStatus::Stale) {
                ++stats_.stale_count;
            } else if (status == ScheduledInferenceStatus::Failed) {
                ++stats_.failed_count;
            }

            auto queue = queues_.find(stream_id);
            if (queue != queues_.end() && queue->second.empty()) {
                queues_.erase(queue);
            } else {
                scheduleReadyLocked(stream_id);
            }
        }
        ready_.notify_one();
    }

    void run() {
        while (true) {
            std::string stream_id;
            auto task = takeNext(stream_id);
            if (task == nullptr) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) {
                    return;
                }
                continue;
            }

            ScheduledInferenceResult output;
            const auto now = std::chrono::steady_clock::now();
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - task->submitted_at
            );
            if (max_request_age_.count() > 0 && age > max_request_age_) {
                output = statusResult(
                    ScheduledInferenceStatus::Stale,
                    "queued inference exceeded the maximum request age"
                );
            } else {
                try {
                    output.status = ScheduledInferenceStatus::Completed;
                    output.result = engine_->infer(
                        task->input,
                        std::move(task->context)
                    );
                    output.result.queue_wait_ms =
                        std::chrono::duration<double, std::milli>(
                            now - task->submitted_at
                        ).count();
                    output.result.timing_samples.queue_wait_ms.push_back(
                        output.result.queue_wait_ms
                    );
                } catch (const std::exception& e) {
                    output = statusResult(
                        ScheduledInferenceStatus::Failed,
                        e.what()
                    );
                }
            }

            output.completed_at = std::chrono::steady_clock::now();
            if (output.status == ScheduledInferenceStatus::Completed) {
                output.execution_ms = std::chrono::duration<double, std::milli>(
                    output.completed_at - now
                ).count();
            }
            const ScheduledInferenceStatus status = output.status;
            finishTask(stream_id, status);
            task->promise.set_value(std::move(output));
        }
    }

    void stop() {
        std::vector<std::shared_ptr<ScheduledTask>> cancelled;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
            for (auto& [stream_id, queue] : queues_) {
                (void)stream_id;
                while (!queue.empty()) {
                    cancelled.push_back(std::move(queue.front()));
                    queue.pop_front();
                }
            }
            queues_.clear();
            queued_tasks_.clear();
            urgent_ready_.clear();
            normal_ready_.clear();
            ready_streams_.clear();
            stats_.queued_count = 0;
        }

        for (auto& task : cancelled) {
            task->promise.set_value(statusResult(
                ScheduledInferenceStatus::Stopped,
                "inference scheduler stopped before the request ran"
            ));
        }
        ready_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    std::shared_ptr<YoloEngine> engine_;
    size_t per_stream_queue_depth_;
    std::chrono::milliseconds max_request_age_;

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::unordered_map<std::string, std::deque<std::shared_ptr<ScheduledTask>>> queues_;
    std::unordered_set<const ScheduledTask*> queued_tasks_;
    std::unordered_set<std::string> ready_streams_;
    std::unordered_set<std::string> in_flight_streams_;
    std::deque<std::string> urgent_ready_;
    std::deque<std::string> normal_ready_;
    size_t urgent_burst_ = 0;
    bool stopping_ = false;
    InferenceSchedulerStats stats_;
    std::vector<std::thread> workers_;
};

InferenceScheduler::InferenceScheduler(
    std::shared_ptr<YoloEngine> engine,
    size_t worker_count,
    size_t per_stream_queue_depth,
    std::chrono::milliseconds max_request_age
) : impl_(std::make_unique<Impl>(
        std::move(engine),
        worker_count,
        per_stream_queue_depth,
        max_request_age
    )) {}

InferenceScheduler::~InferenceScheduler() = default;

std::future<ScheduledInferenceResult> InferenceScheduler::submit(
    TensorInput input,
    InferenceContext context,
    bool urgent
) {
    return impl_->submit(std::move(input), std::move(context), urgent).result;
}

ScheduledInferenceSubmission InferenceScheduler::submitTracked(
    TensorInput input, InferenceContext context, bool urgent
) {
    return impl_->submit(std::move(input), std::move(context), urgent);
}

bool InferenceScheduler::isQueued(const ScheduledInferenceTicket& ticket) const {
    return impl_->isQueued(ticket);
}

bool InferenceScheduler::cancelQueued(const ScheduledInferenceTicket& ticket) {
    return impl_->cancelQueued(ticket);
}

bool InferenceScheduler::updateQueued(
    const ScheduledInferenceTicket& ticket, TensorInput input, InferenceContext context
) {
    return impl_->updateQueued(ticket, std::move(input), std::move(context));
}

InferResult InferenceScheduler::infer(
    TensorInput input,
    InferenceContext context,
    bool urgent
) {
    return impl_->infer(std::move(input), std::move(context), urgent);
}

InferenceSchedulerStats InferenceScheduler::stats() const {
    return impl_->stats();
}

}  // namespace yolo
