#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "model/inference_types.h"

namespace yolo {

class ByteTracker {
public:
    ByteTracker();

    std::vector<TrackedDetection> update(const std::vector<Detection>& detections);
    std::vector<TrackedDetection> updateTracked(
        const std::vector<TrackedDetection>& tracked_detections
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
    int next_track_id_ = 1;
    int track_buffer_ = 30;
    std::vector<Track> tracks_;
};

}  // namespace yolo
