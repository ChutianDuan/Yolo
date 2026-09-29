#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace yolo::api {

struct VideoJobExecutorStats {
    uint64_t submitted_count = 0;
    uint64_t completed_count = 0;
    uint64_t rejected_count = 0;
    size_t queued_count = 0;
    size_t in_flight_count = 0;
    size_t worker_count = 0;
};

class VideoJobExecutor final {
public:
    VideoJobExecutor(size_t worker_count, size_t max_queue_depth);
    ~VideoJobExecutor();

    VideoJobExecutor(const VideoJobExecutor&) = delete;
    VideoJobExecutor& operator=(const VideoJobExecutor&) = delete;

    bool submit(std::function<void()> job);
    VideoJobExecutorStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace yolo::api
