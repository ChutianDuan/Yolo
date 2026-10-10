#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "model/inference_types.h"

namespace yolo {

// 单模型跟踪器：Kalman 预测结合两阶段检测关联，维持跨帧目标 ID。
class ByteTracker {
public:
    ByteTracker();

    std::vector<TrackedDetection> update(const std::vector<Detection>& detections);
    // Observations on the same accepted source frame share one prediction/tick.
    std::vector<TrackedDetection> update(
        const std::vector<Detection>& detections, int64_t frame_index
    );
    // 光流已提供目标 ID 时按 ID 更新既有轨迹，不创建新目标。
    std::vector<TrackedDetection> updateTracked(
        const std::vector<TrackedDetection>& tracked_detections
    );
    std::vector<TrackedDetection> updateTracked(
        const std::vector<TrackedDetection>& tracked_detections, int64_t frame_index
    );

private:
    enum class TrackState {
        Tracked,
        Lost,
        Removed,
    };

    struct Track {
        int id = -1;
        Detection detection;
        // 状态依次为中心 x/y、宽高比、高度，以及这四个量的速度。
        std::array<float, 8> mean{};
        std::array<std::array<float, 8>, 8> covariance{};
        TrackState state = TrackState::Tracked;
        bool activated = false;
        bool matched = false;
        int frame_id = 0;
        int start_frame = 0;
        int tracklet_len = 0;
    };

    struct Match {
        size_t track_index = 0;
        size_t detection_index = 0;
        float cost = 0.0F;
    };

    static float boxIou(const Detection& a, const Detection& b);
    static Detection stateToDetection(
        const std::array<float, 8>& mean,
        int class_id,
        float score
    );
    static std::array<float, 4> detectionToMeasurement(const Detection& detection);

    Track createTrack(const Detection& detection);
    void beginFrame(int64_t frame_index);
    void predictTrack(Track& track) const;
    void updateTrack(Track& track, const Detection& detection);
    void markLost(Track& track);
    void markRemoved(Track& track);

    std::vector<Match> assignDetections(
        const std::vector<size_t>& track_indices,
        const std::vector<Detection>& detections,
        float match_threshold,
        bool fuse_score
    ) const;
    std::vector<TrackedDetection> currentTrackedDetections() const;
    void removeDuplicateTracks();
    void pruneTracks();

    int frame_id_ = 0;
    std::optional<int64_t> last_frame_index_;
    int next_track_id_ = 1;
    int track_buffer_ = 30;
    std::vector<Track> tracks_;
};

}  // namespace yolo
