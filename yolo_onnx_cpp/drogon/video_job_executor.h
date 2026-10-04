#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace yolo::api {

// 视频任务池生命周期内的统计快照。
struct VideoJobExecutorStats {
    // 累计任务事件；completed 包含抛出异常后执行结束的任务，不表示业务成功。
    uint64_t submitted_count = 0;
    uint64_t completed_count = 0;
    uint64_t rejected_count = 0; // 停止接收或队列满导致拒绝；空回调不计入。
    // 当前等待与执行中的任务数，以及工作线程数量。
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
