#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace yolo {

enum class StreamInferenceStage : size_t {
    Preprocess, QueueWait, Execution, Infer, Postprocess,
    // 完成到收取的延迟、解码到收取的年龄、回放耗时、解码到提交的年龄。
    CompletionPickup, ResultAge, Replay, CommitAge, Count
};

inline constexpr std::array<const char*, static_cast<size_t>(StreamInferenceStage::Count)>
    kStreamInferenceStageNames{
        "preprocess", "queue_wait", "execution", "infer", "postprocess",
        "completion_pickup", "result_age", "replay", "commit_age"
    };

// 阶段有效样本数、累计毫秒数与最大毫秒数；合并时最大值取 max，不相加。
// Execution 包含 Infer / Postprocess 等内部耗时，各阶段不是互斥区间。
struct StreamInferenceStageTotal {
    uint64_t count = 0;
    double sum_ms = 0.0;
    double max_ms = 0.0;

    void observe(double milliseconds) {
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
            return;
        }
        ++count;
        sum_ms += milliseconds;
        max_ms = std::max(max_ms, milliseconds);
    }

    void merge(const StreamInferenceStageTotal& other) {
        count += other.count;
        sum_ms += other.sum_ms;
        max_ms = std::max(max_ms, other.max_ms);
    }
};

// 经上下文校验并收取的异步结果，按当前流生命周期累计。
struct StreamInferenceOutcomes {
    uint64_t completed_count = 0; // 收取的完成结果，包含后续被拒绝的结果。
    uint64_t applied_count = 0;   // 回放后成功提交的校正结果。
    uint64_t expired_count = 0;   // 收取或提交时超过结果年龄限制。
    uint64_t evicted_count = 0;   // 源帧已从回放历史中移除，无法应用。
};

// 常量空间诊断：不保留图像、上下文或逐请求耗时样本。
struct StreamInferenceDiagnostics {
    StreamInferenceOutcomes outcomes;
    std::array<StreamInferenceStageTotal, kStreamInferenceStageNames.size()> stages{};

    void observe(StreamInferenceStage stage, double milliseconds) {
        stages[static_cast<size_t>(stage)].observe(milliseconds);
    }

    void merge(const StreamInferenceDiagnostics& other) {
        outcomes.completed_count += other.outcomes.completed_count;
        outcomes.applied_count += other.outcomes.applied_count;
        outcomes.expired_count += other.outcomes.expired_count;
        outcomes.evicted_count += other.outcomes.evicted_count;
        for (size_t i = 0; i < stages.size(); ++i) {
            stages[i].merge(other.stages[i]);
        }
    }
};

// 已接纳帧处理尝试的墙钟耗时；FrameWork 包含内部子阶段，不能与子阶段相加。
enum class StreamProcessingStage : size_t {
    FrameWork, Prepare, Poll, Publish, Gray, WeakFlow, Motion, Projection,
    FrameDiff, RoiFeatures, PyramidBuild, LkForward, LkBackward, FlowQuality, Count
};
inline constexpr std::array<const char*, static_cast<size_t>(StreamProcessingStage::Count)>
    kStreamProcessingStageNames{
        "frame_work", "prepare", "poll", "publish", "gray", "weak_flow",
        "motion", "projection", "frame_diff", "roi_features", "pyramid_build",
        "lk_forward", "lk_backward", "flow_quality"
    };
using StreamProcessingDiagnostics =
    std::array<StreamInferenceStageTotal, kStreamProcessingStageNames.size()>;

// 下标 0 为 high、1 为 low，对应实时回放的两个独立异步槽位。
using StreamInferenceDiagnosticsByTier = std::array<StreamInferenceDiagnostics, 2>;

}  // namespace yolo
