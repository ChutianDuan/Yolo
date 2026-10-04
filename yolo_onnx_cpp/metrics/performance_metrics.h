#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <sys/resource.h>

namespace yolo {

// 耗时样本的分位数；单位由使用处约定，本项目的性能统计使用毫秒。
struct LatencyPercentiles {
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

// 单次调用或视频任务的原始阶段样本，单位毫秒，用于累计及分位数计算。
// 实时流使用常量空间的阶段汇总，避免长期保留逐帧样本。
struct StageTimingSamples {
    std::vector<double> decode_ms;
    std::vector<double> preprocess_ms;
    std::vector<double> infer_ms;
    std::vector<double> postprocess_ms;
    std::vector<double> tracker_ms;
    std::vector<double> queue_wait_ms;
    std::vector<double> end_to_end_ms;
};

struct ProcessUsageSnapshot {
    std::chrono::steady_clock::time_point wall_time;
    double cpu_seconds = 0.0;
};

// 阶段累计耗时，单位毫秒；end_to_end_ms 是整次调用的墙钟耗时。
struct PerformanceTiming {
    double decode_ms = 0.0;
    double preprocess_ms = 0.0;
    double infer_ms = 0.0;
    double postprocess_ms = 0.0;
    double tracker_ms = 0.0;
    double queue_wait_ms = 0.0;
    double end_to_end_ms = 0.0;
};

// 各阶段样本的 P50/P95/P99，单位毫秒；空样本返回零。
struct PerformanceLatencyPercentiles {
    LatencyPercentiles decode_percentiles_ms;
    LatencyPercentiles preprocess_percentiles_ms;
    LatencyPercentiles infer_percentiles_ms;
    LatencyPercentiles postprocess_percentiles_ms;
    LatencyPercentiles tracker_percentiles_ms;
    LatencyPercentiles queue_wait_percentiles_ms;
    LatencyPercentiles end_to_end_percentiles_ms;
};

// 进程资源：CPU 按进程 CPU 时间 / 墙钟时间计算，多核时可超过 100%；RSS 按 1024 换算。
struct PerformanceResources {
    double cpu_utilization_percent = 0.0;
    double rss_memory_mb = 0.0;
};

// 采样时的队列长度、运行期间峰值和累计丢帧数。
struct PerformanceQueueStats {
    size_t queue_length = 0;
    size_t max_queue_length = 0;
    int64_t dropped_frame_count = 0;
};

// 单次调用或视频任务的性能汇总，不跨任务累计。
struct PerformanceMetrics {
    PerformanceTiming timing;
    PerformanceLatencyPercentiles latency_percentiles;
    // 输出数量 / 墙钟耗时，区别于视频源 FPS。
    double average_fps = 0.0;
    PerformanceResources resources;
    PerformanceQueueStats queue;
};

inline double sumSamples(const std::vector<double>& samples) {
    double total = 0.0;
    for (const double sample : samples) {
        total += sample;
    }
    return total;
}

inline LatencyPercentiles percentileSummary(std::vector<double> samples) {
    LatencyPercentiles summary;
    if (samples.empty()) {
        return summary;
    }

    std::sort(samples.begin(), samples.end());
    auto percentile = [&samples](double ratio) {
        const double scaled = ratio * static_cast<double>(samples.size() - 1);
        const size_t index = static_cast<size_t>(std::round(scaled));
        return samples[std::min(index, samples.size() - 1)];
    };

    summary.p50 = percentile(0.50);
    summary.p95 = percentile(0.95);
    summary.p99 = percentile(0.99);
    return summary;
}

inline ProcessUsageSnapshot captureProcessUsage() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);

    const double user_seconds =
        static_cast<double>(usage.ru_utime.tv_sec)
        + static_cast<double>(usage.ru_utime.tv_usec) / 1000000.0;
    const double system_seconds =
        static_cast<double>(usage.ru_stime.tv_sec)
        + static_cast<double>(usage.ru_stime.tv_usec) / 1000000.0;

    return ProcessUsageSnapshot{
        std::chrono::steady_clock::now(),
        user_seconds + system_seconds
    };
}

inline double cpuUtilizationPercent(
    const ProcessUsageSnapshot& start,
    const ProcessUsageSnapshot& end
) {
    const double wall_seconds =
        std::chrono::duration<double>(end.wall_time - start.wall_time).count();
    if (wall_seconds <= 0.0 || end.cpu_seconds < start.cpu_seconds) {
        return 0.0;
    }
    return (end.cpu_seconds - start.cpu_seconds) * 100.0 / wall_seconds;
}

inline double currentRssMemoryMb() {
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            double value_kb = 0.0;
            std::string unit;
            status >> value_kb >> unit;
            return value_kb / 1024.0;
        }

        std::string ignored;
        std::getline(status, ignored);
    }
    return 0.0;
}

inline PerformanceMetrics buildPerformanceMetrics(
    const StageTimingSamples& samples,
    int64_t output_count,
    double end_to_end_ms,
    const ProcessUsageSnapshot& usage_start,
    const ProcessUsageSnapshot& usage_end,
    size_t queue_length,
    size_t max_queue_length,
    int64_t dropped_frame_count
) {
    PerformanceMetrics metrics;
    metrics.timing.decode_ms = sumSamples(samples.decode_ms);
    metrics.timing.preprocess_ms = sumSamples(samples.preprocess_ms);
    metrics.timing.infer_ms = sumSamples(samples.infer_ms);
    metrics.timing.postprocess_ms = sumSamples(samples.postprocess_ms);
    metrics.timing.tracker_ms = sumSamples(samples.tracker_ms);
    metrics.timing.queue_wait_ms = sumSamples(samples.queue_wait_ms);
    metrics.timing.end_to_end_ms = end_to_end_ms;

    metrics.latency_percentiles.decode_percentiles_ms = percentileSummary(samples.decode_ms);
    metrics.latency_percentiles.preprocess_percentiles_ms = percentileSummary(samples.preprocess_ms);
    metrics.latency_percentiles.infer_percentiles_ms = percentileSummary(samples.infer_ms);
    metrics.latency_percentiles.postprocess_percentiles_ms = percentileSummary(samples.postprocess_ms);
    metrics.latency_percentiles.tracker_percentiles_ms = percentileSummary(samples.tracker_ms);
    metrics.latency_percentiles.queue_wait_percentiles_ms = percentileSummary(samples.queue_wait_ms);
    metrics.latency_percentiles.end_to_end_percentiles_ms = percentileSummary(samples.end_to_end_ms);

    metrics.average_fps = end_to_end_ms > 0.0
        ? static_cast<double>(output_count) * 1000.0 / end_to_end_ms
        : 0.0;
    metrics.resources.cpu_utilization_percent = cpuUtilizationPercent(usage_start, usage_end);
    metrics.resources.rss_memory_mb = currentRssMemoryMb();
    metrics.queue.queue_length = queue_length;
    metrics.queue.max_queue_length = max_queue_length;
    metrics.queue.dropped_frame_count = dropped_frame_count;
    return metrics;
}

}  // namespace yolo
