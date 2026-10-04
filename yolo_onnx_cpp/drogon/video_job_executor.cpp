#include "drogon/video_job_executor.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace yolo::api {

class VideoJobExecutor::Impl {
public:
    Impl(size_t worker_count, size_t max_queue_depth)
        : max_queue_depth_(std::max<size_t>(max_queue_depth, 1)) {
        const size_t count = std::max<size_t>(worker_count, 1);
        stats_.worker_count = count;
        workers_.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            workers_.emplace_back(&Impl::run, this);
        }
    }

    ~Impl() {
        // 停止接收新任务，唤醒线程；run 会处理完已接收的任务后退出。
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    bool submit(std::function<void()> job) {
        if (!job) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || jobs_.size() >= max_queue_depth_) {
                ++stats_.rejected_count;
                return false;
            }
            jobs_.push_back(std::move(job));
            ++stats_.submitted_count;
            stats_.queued_count = jobs_.size();
        }
        ready_.notify_one();
        return true;
    }

    VideoJobExecutorStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

private:
    void run() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this]() {
                    return stopping_ || !jobs_.empty();
                });
                if (jobs_.empty()) {
                    if (stopping_) {
                        return;
                    }
                    continue;
                }
                job = std::move(jobs_.front());
                jobs_.pop_front();
                stats_.queued_count = jobs_.size();
                ++stats_.in_flight_count;
            }

            // 视频解码和推理在队列锁之外运行，让提交与其他工作线程继续推进。
            try {
                job();
            } catch (...) {
                // A request job owns its error response; keep the worker alive.
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                --stats_.in_flight_count;
                ++stats_.completed_count;
            }
        }
    }

    size_t max_queue_depth_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
    VideoJobExecutorStats stats_;
};

VideoJobExecutor::VideoJobExecutor(size_t worker_count, size_t max_queue_depth)
    : impl_(std::make_unique<Impl>(worker_count, max_queue_depth)) {}

VideoJobExecutor::~VideoJobExecutor() = default;

bool VideoJobExecutor::submit(std::function<void()> job) {
    return impl_->submit(std::move(job));
}

VideoJobExecutorStats VideoJobExecutor::stats() const {
    return impl_->stats();
}

}  // namespace yolo::api
