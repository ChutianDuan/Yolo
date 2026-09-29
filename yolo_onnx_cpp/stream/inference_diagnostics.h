#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace yolo {

enum class StreamInferenceStage : size_t {
    Preprocess, QueueWait, Execution, Infer, Postprocess,
    CompletionPickup, ResultAge, Replay, CommitAge, Count
};

inline constexpr std::array<const char*, static_cast<size_t>(StreamInferenceStage::Count)>
    kStreamInferenceStageNames{
        "preprocess", "queue_wait", "execution", "infer", "postprocess",
        "completion_pickup", "result_age", "replay", "commit_age"
    };

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

// Constant space: no retained per-request images, contexts or latency sample vectors.
// Completed counts only context-validated results collected by the live replay worker.
struct StreamInferenceDiagnostics {
    uint64_t completed_count = 0;
    uint64_t applied_count = 0;
    uint64_t expired_count = 0;
    uint64_t evicted_count = 0;
    std::array<StreamInferenceStageTotal, kStreamInferenceStageNames.size()> stages{};

    void observe(StreamInferenceStage stage, double milliseconds) {
        stages[static_cast<size_t>(stage)].observe(milliseconds);
    }

    void merge(const StreamInferenceDiagnostics& other) {
        completed_count += other.completed_count;
        applied_count += other.applied_count;
        expired_count += other.expired_count;
        evicted_count += other.evicted_count;
        for (size_t i = 0; i < stages.size(); ++i) {
            stages[i].merge(other.stages[i]);
        }
    }
};

// Constant-space wall time for admitted frame attempts; frame_work contains sub-stages.
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

// High at index 0, low at index 1, matching live replay's independent future slots.
using StreamInferenceDiagnosticsByTier = std::array<StreamInferenceDiagnostics, 2>;

}  // namespace yolo
