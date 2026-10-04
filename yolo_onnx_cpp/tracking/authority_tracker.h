#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "model/inference_types.h"

namespace yolo {

struct WeakTrackResult;
struct FrameMotion;

// Shared by authority-age checks and the existing offline high-model cadence.
constexpr int kMinHighResCadenceFrames = 24;

// 当前稳定 / 候选轨迹数及历史峰值；当前数量由 diagnostics() 查询时填充。
struct HighLowTrackCounts {
    int64_t stable_track_count = 0;
    int64_t provisional_track_count = 0;
    int64_t max_stable_track_count = 0;
    int64_t max_provisional_track_count = 0;
};

// 候选轨迹生命周期累计次数：创建、晋升、过期和去重移除。
struct HighLowProvisionalCounts {
    int64_t provisional_created_count = 0;
    int64_t provisional_promoted_count = 0;
    int64_t provisional_expired_count = 0;
    int64_t provisional_deduplicated_count = 0;
};

// 各类轨迹去重及输出重复框抑制次数；不同阶段计数可能涉及同一目标。
struct HighLowDuplicateCounts {
    int64_t stable_stable_duplicate_count = 0;
    int64_t stable_provisional_duplicate_count = 0;
    int64_t provisional_provisional_duplicate_count = 0;
    int64_t output_suppressed_duplicate_count = 0;
};

// 低分辨率校正被拒绝的累计次数：几何约束与类别冲突。
struct HighLowLowResCounts {
    int64_t low_res_geometry_rejection_count = 0;
    int64_t low_res_class_conflict_count = 0;
};

// 光流累计跟踪、拒绝、更新和老化抑制次数；拒绝原因是诊断细分，不能再与总数相加。
struct HighLowFlowCounts {
    int64_t flow_track_count = 0;
    int64_t flow_rejected_track_count = 0;
    int64_t flow_low_point_rejection_count = 0;
    int64_t flow_invalid_ratio_rejection_count = 0;
    int64_t flow_forward_backward_rejection_count = 0;
    int64_t flow_motion_dispersion_rejection_count = 0;
    int64_t flow_motion_jump_rejection_count = 0;
    int64_t flow_boundary_rejection_count = 0;
    int64_t direct_flow_update_count = 0;
    int64_t global_flow_update_count = 0;
    int64_t flow_age_output_suppression_count = 0;
    int64_t flow_age_expired_count = 0;
    int64_t exiting_track_suppression_count = 0;
};

// 紧急检测次数与触发原因；一次检测可同时由多个原因触发。
struct HighLowUrgentCounts {
    int64_t urgent_low_res_detection_count = 0;
    int64_t urgent_high_res_detection_count = 0;
    int64_t urgent_flow_quality_count = 0;
    int64_t urgent_track_change_count = 0;
    int64_t urgent_duplicate_count = 0;
    int64_t urgent_flow_age_count = 0;
    int64_t urgent_geometry_count = 0;
    int64_t urgent_class_conflict_count = 0;
};

// 跟踪器与离线运行诊断；累计字段沿用各自统计生命周期，不跨任务共享。
struct HighLowDiagnostics {
    HighLowTrackCounts tracks;
    HighLowProvisionalCounts provisional;
    HighLowDuplicateCounts duplicates;
    HighLowLowResCounts low_res;
    HighLowFlowCounts flow;
    HighLowUrgentCounts urgent;
};

struct TrackMotion {
    int track_id = -1;
    float dx = 0.0F;
    float dy = 0.0F;
    float source_center_x = 0.0F;
    float source_center_y = 0.0F;
    float source_width = 0.0F;
    float source_height = 0.0F;
    double quality = 1.0;
    bool boundary_clipped = false;
    bool center_outside = false;
};

struct ProjectedTrack {
    TrackedDetection track;
    bool direct_flow = false;
    double flow_quality = 1.0;
    double visible_area_ratio = 1.0;
    bool boundary_clipped = false;
    bool center_outside = false;
};

struct HighResRegion {
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
};

std::vector<TrackMotion> directTrackMotions(
    const std::vector<TrackedDetection>& previous_tracks,
    const WeakTrackResult& weak_result
);

std::vector<ProjectedTrack> projectTracks(
    const std::vector<TrackedDetection>& tracks,
    const std::vector<TrackMotion>& direct_motions,
    const FrameMotion& motion,
    bool allow_global_motion,
    int image_width,
    int image_height
);

std::vector<TrackedDetection> projectedDetections(
    const std::vector<ProjectedTrack>& projected_tracks
);

// 双模型跟踪器：高分辨率检测建立稳定轨迹，低分辨率检测补充候选并校正位置。
// 光流延续已有目标；候选获高分辨率确认后晋升稳定轨迹并保留 ID。
class AuthorityTracker {
public:
    std::vector<TrackedDetection> updateHighRes(
        const std::vector<Detection>& detections,
        int64_t frame_index
    );

    // ROI 检测只校正区域内的目标，区域外目标继续依赖投影与老化策略。
    std::vector<TrackedDetection> updateHighResRegion(
        const std::vector<Detection>& detections,
        const std::vector<ProjectedTrack>& projected_tracks,
        const HighResRegion& region,
        int64_t frame_index
    );

    std::vector<TrackedDetection> updateLowRes(
        const std::vector<Detection>& detections,
        const std::vector<ProjectedTrack>& projected_tracks,
        int64_t frame_index
    );

    std::vector<TrackedDetection> updateFlow(
        const std::vector<ProjectedTrack>& projected_tracks,
        int64_t frame_index
    );

    std::vector<TrackedDetection> tracks() const;

    // consume 接口读取并消费刷新信号，由上层结合检测频率决定实际提交时机。
    bool consumeLowResRefreshRequest();

    bool consumeFlowAgeRefreshRequest();

    bool consumeHighResRefreshRequest();

    HighLowDiagnostics diagnostics() const;

private:
    struct AuthorityTrack {
        int id = -1;
        Detection detection;
        int high_res_miss_count = 0;
        int low_res_miss_count = 0;
        int flow_only_age = 0;
        int exiting_frame_count = 0;
        int geometry_rejection_count = 0;
        bool flow_age_refresh_reported = false;
        float detector_score = 0.0F;
        double flow_confidence = 1.0;
        int64_t last_update_frame_index = -1;
        int64_t last_detector_frame_index = -1;
        int64_t last_low_res_frame_index = -1;
        int64_t last_valid_flow_frame_index = -1;
        int64_t last_high_res_frame_index = -1;
    };

    struct ProvisionalTrack {
        int id = -1;
        Detection detection;
        int consecutive_low_res_hit_count = 0;
        int consecutive_low_res_miss_count = 0;
        bool output_confirmed = false;
        int flow_only_age = 0;
        int exiting_frame_count = 0;
        float detector_score = 0.0F;
        double flow_confidence = 1.0;
        int64_t last_update_frame_index = -1;
        int64_t last_detector_frame_index = -1;
        int64_t last_valid_flow_frame_index = -1;
        int64_t last_low_res_frame_index = -1;
    };

    struct Match {
        size_t track_index = 0;
        size_t detection_index = 0;
        float score = 0.0F;
    };

    static std::vector<Match> selectMatches(
        std::vector<Match> edges,
        size_t track_count,
        size_t detection_count
    );

    static float highResMatchScore(
        const Detection& track_box,
        const Detection& detection
    );

    std::vector<Match> stableHighResEdges(
        const std::vector<Detection>& detections
    ) const;

    std::vector<Match> stableLowResEdges(
        const std::vector<Detection>& detections,
        int64_t frame_index
    ) const;

    std::vector<Match> provisionalHighResEdges(
        const std::vector<Detection>& detections,
        const std::vector<bool>& used_detections
    ) const;

    std::vector<Match> provisionalLowResEdges(
        const std::vector<Detection>& detections,
        const std::vector<bool>& used_detections
    ) const;

    void advanceFlowAge(int64_t frame_index);

    void applyFlow(
        const std::vector<ProjectedTrack>& projected_tracks,
        int64_t frame_index
    );

    void createStableTrack(const Detection& detection, int64_t frame_index);

    void createProvisionalTrack(const Detection& detection, int64_t frame_index);

    void createConfirmedProvisionalTrack(
        const ProvisionalTrack& provisional,
        const Detection& high_res_detection,
        int64_t frame_index
    );

    static bool lowResShouldPullTrack(
        const AuthorityTrack& track,
        int64_t frame_index
    );

    static float lowResBlendWeight(
        const AuthorityTrack& track,
        int64_t frame_index
    );

    static bool geometryUpdateAllowed(
        const AuthorityTrack& track,
        const Detection& candidate,
        int64_t frame_index
    );

    bool overlapsStableGeometry(const Detection& detection) const;

    void recordLowResClassConflict(
        const Detection& detection,
        int64_t frame_index
    );

    void eraseExpiredStableTracks(int64_t frame_index);

    void pruneStaleTracks(int64_t frame_index);

    static bool stablePreferred(
        const AuthorityTrack& a,
        const AuthorityTrack& b
    );

    static bool provisionalPreferred(
        const ProvisionalTrack& a,
        const ProvisionalTrack& b
    );

    void mergeStableState(AuthorityTrack& winner, const AuthorityTrack& loser);

    void consolidate(int64_t frame_index);

    std::vector<TrackedDetection> currentTracks(bool record_diagnostics) const;

    int next_track_id_ = 1;
    std::vector<AuthorityTrack> tracks_;
    std::vector<ProvisionalTrack> provisionals_;
    mutable HighLowDiagnostics diagnostics_;
    bool low_res_refresh_requested_ = false;
    bool high_res_refresh_requested_ = false;
};

}  // namespace yolo
