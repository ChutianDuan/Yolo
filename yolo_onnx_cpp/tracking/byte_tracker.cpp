#include "byte_tracker.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace yolo {
namespace {

constexpr float kHighScoreThreshold = 0.5F;
constexpr float kLowScoreThreshold = 0.1F;
constexpr float kNewTrackThreshold = 0.25F;
constexpr float kHighMatchIouThreshold = 0.3F;
constexpr float kLowMatchIouThreshold = 0.2F;
constexpr int kDefaultTrackBuffer = 30;

}  // namespace

ByteTracker::ByteTracker()
    : track_buffer_(kDefaultTrackBuffer) {}

float ByteTracker::boxIou(const Detection& a, const Detection& b) {
    const float inter_x1 = std::max(a.x1, b.x1);
    const float inter_y1 = std::max(a.y1, b.y1);
    const float inter_x2 = std::min(a.x2, b.x2);
    const float inter_y2 = std::min(a.y2, b.y2);

    const float inter_w = std::max(0.0F, inter_x2 - inter_x1);
    const float inter_h = std::max(0.0F, inter_y2 - inter_y1);
    const float inter_area = inter_w * inter_h;

    const float area_a = std::max(0.0F, a.x2 - a.x1) * std::max(0.0F, a.y2 - a.y1);
    const float area_b = std::max(0.0F, b.x2 - b.x1) * std::max(0.0F, b.y2 - b.y1);
    const float union_area = area_a + area_b - inter_area;

    if (union_area <= 0.0F) {
        return 0.0F;
    }
    return inter_area / union_area;
}

Detection ByteTracker::predictDetection(const Track& track) const {
    Detection predicted = track.detection;
    const float step = static_cast<float>(track.lost_frames + 1);
    predicted.x1 += track.vx1 * step;
    predicted.y1 += track.vy1 * step;
    predicted.x2 += track.vx2 * step;
    predicted.y2 += track.vy2 * step;
    return predicted;
}

void ByteTracker::updateTrack(Track& track, const Detection& detection) {
    track.vx1 = detection.x1 - track.detection.x1;
    track.vy1 = detection.y1 - track.detection.y1;
    track.vx2 = detection.x2 - track.detection.x2;
    track.vy2 = detection.y2 - track.detection.y2;
    track.detection = detection;
    track.predicted = detection;
    track.lost_frames = 0;
    track.matched = true;
}

std::vector<ByteTracker::Match> ByteTracker::greedyMatch(
    const std::vector<size_t>& track_indices,
    const std::vector<Detection>& detections,
    const std::vector<size_t>& detection_indices,
    float iou_threshold
) const {
    std::vector<Match> candidates;

    for (size_t track_index : track_indices) {
        const auto& track = tracks_[track_index];
        for (size_t detection_index : detection_indices) {
            const auto& detection = detections[detection_index];
            if (track.detection.class_id != detection.class_id) {
                continue;
            }

            const float iou = boxIou(track.predicted, detection);
            if (iou >= iou_threshold) {
                candidates.push_back(Match{track_index, detection_index, iou});
            }
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Match& a, const Match& b) {
            if (a.iou != b.iou) {
                return a.iou > b.iou;
            }
            if (a.track_index != b.track_index) {
                return a.track_index < b.track_index;
            }
            return a.detection_index < b.detection_index;
        }
    );

    std::vector<Match> matches;
    std::vector<bool> used_tracks(tracks_.size(), false);
    std::vector<bool> used_detections(detections.size(), false);

    for (const auto& candidate : candidates) {
        if (used_tracks[candidate.track_index] || used_detections[candidate.detection_index]) {
            continue;
        }

        used_tracks[candidate.track_index] = true;
        used_detections[candidate.detection_index] = true;
        matches.push_back(candidate);
    }

    return matches;
}

std::vector<TrackedDetection> ByteTracker::updateTracked(
    const std::vector<TrackedDetection>& tracked_detections
) {
    for (auto& track : tracks_) {
        track.matched = false;
    }

    std::vector<TrackedDetection> output_tracks;
    output_tracks.reserve(tracked_detections.size());

    for (const auto& tracked_detection : tracked_detections) {
        auto matched_track = std::find_if(
            tracks_.begin(),
            tracks_.end(),
            [&tracked_detection](const Track& track) {
                return track.id == tracked_detection.track_id;
            }
        );
        if (matched_track == tracks_.end()) {
            continue;
        }

        updateTrack(*matched_track, tracked_detection.detection);
        output_tracks.push_back(TrackedDetection{
            matched_track->id,
            matched_track->detection
        });
    }

    for (auto& track : tracks_) {
        if (!track.matched) {
            ++track.lost_frames;
        }
    }

    tracks_.erase(
        std::remove_if(
            tracks_.begin(),
            tracks_.end(),
            [this](const Track& track) {
                return track.lost_frames > track_buffer_;
            }
        ),
        tracks_.end()
    );

    std::sort(
        output_tracks.begin(),
        output_tracks.end(),
        [](const TrackedDetection& a, const TrackedDetection& b) {
            return a.track_id < b.track_id;
        }
    );

    return output_tracks;
}

std::vector<TrackedDetection> ByteTracker::update(
    const std::vector<Detection>& detections
) {
    for (auto& track : tracks_) {
        track.predicted = predictDetection(track);
        track.matched = false;
    }

    std::vector<size_t> high_detections;
    std::vector<size_t> low_detections;
    for (size_t i = 0; i < detections.size(); ++i) {
        if (detections[i].score >= kHighScoreThreshold) {
            high_detections.push_back(i);
        } else if (detections[i].score >= kLowScoreThreshold) {
            low_detections.push_back(i);
        }
    }

    std::vector<size_t> track_indices;
    track_indices.reserve(tracks_.size());
    for (size_t i = 0; i < tracks_.size(); ++i) {
        track_indices.push_back(i);
    }

    std::vector<bool> matched_detections(detections.size(), false);
    std::vector<TrackedDetection> output_tracks;
    output_tracks.reserve(detections.size());

    const auto high_matches = greedyMatch(
        track_indices,
        detections,
        high_detections,
        kHighMatchIouThreshold
    );

    for (const auto& match : high_matches) {
        updateTrack(tracks_[match.track_index], detections[match.detection_index]);
        matched_detections[match.detection_index] = true;
        output_tracks.push_back(TrackedDetection{
            tracks_[match.track_index].id,
            tracks_[match.track_index].detection
        });
    }

    std::vector<size_t> unmatched_tracks;
    unmatched_tracks.reserve(tracks_.size());
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (!tracks_[i].matched) {
            unmatched_tracks.push_back(i);
        }
    }

    std::vector<size_t> unmatched_low_detections;
    unmatched_low_detections.reserve(low_detections.size());
    for (size_t detection_index : low_detections) {
        if (!matched_detections[detection_index]) {
            unmatched_low_detections.push_back(detection_index);
        }
    }

    const auto low_matches = greedyMatch(
        unmatched_tracks,
        detections,
        unmatched_low_detections,
        kLowMatchIouThreshold
    );

    for (const auto& match : low_matches) {
        updateTrack(tracks_[match.track_index], detections[match.detection_index]);
        matched_detections[match.detection_index] = true;
        output_tracks.push_back(TrackedDetection{
            tracks_[match.track_index].id,
            tracks_[match.track_index].detection
        });
    }

    for (auto& track : tracks_) {
        if (!track.matched) {
            ++track.lost_frames;
        }
    }

    for (size_t i = 0; i < detections.size(); ++i) {
        if (matched_detections[i] || detections[i].score < kNewTrackThreshold) {
            continue;
        }

        Track track;
        track.id = next_track_id_++;
        track.detection = detections[i];
        track.predicted = detections[i];
        track.matched = true;
        tracks_.push_back(track);
        output_tracks.push_back(TrackedDetection{track.id, track.detection});
    }

    tracks_.erase(
        std::remove_if(
            tracks_.begin(),
            tracks_.end(),
            [this](const Track& track) {
                return track.lost_frames > track_buffer_;
            }
        ),
        tracks_.end()
    );

    std::sort(
        output_tracks.begin(),
        output_tracks.end(),
        [](const TrackedDetection& a, const TrackedDetection& b) {
            return a.track_id < b.track_id;
        }
    );

    return output_tracks;
}

}  // namespace yolo
