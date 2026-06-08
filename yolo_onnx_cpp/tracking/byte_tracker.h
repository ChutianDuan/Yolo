#pragma once

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
    struct Track {
        int id = -1;
        Detection detection;
        Detection predicted;
        float vx1 = 0.0F;
        float vy1 = 0.0F;
        float vx2 = 0.0F;
        float vy2 = 0.0F;
        int lost_frames = 0;
        bool matched = false;
    };

    struct Match {
        size_t track_index = 0;
        size_t detection_index = 0;
        float iou = 0.0F;
    };

    static float boxIou(const Detection& a, const Detection& b);
    Detection predictDetection(const Track& track) const;
    void updateTrack(Track& track, const Detection& detection);
    std::vector<Match> greedyMatch(
        const std::vector<size_t>& track_indices,
        const std::vector<Detection>& detections,
        const std::vector<size_t>& detection_indices,
        float iou_threshold
    ) const;

    int next_track_id_ = 1;
    int track_buffer_ = 30;
    std::vector<Track> tracks_;
};

}  // namespace yolo
