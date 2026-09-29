#include "tracking/authority_tracker.h"
#include "video/optical_flow_tracker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace yolo {
namespace {

constexpr float kHighResSameClassMatchIou = 0.25F;
constexpr float kHighResClassCorrectionIou = 0.65F;
constexpr float kLowResMatchIou = 0.20F;
constexpr float kProvisionalMatchIou = 0.40F;
constexpr float kProvisionalCenterMatchIou = 0.12F;
constexpr float kLowResBoxBlend = 0.35F;
constexpr float kLowResStaleBoxBlend = 0.50F;
constexpr float kLowResCenterDistanceScale = 0.85F;
constexpr float kLowResMinCenterDistancePx = 28.0F;
constexpr float kLowResMaxAreaRatio = 2.0F;
constexpr float kDirectFlowBoxBlend = 1.00F;
constexpr float kGlobalFlowBoxBlend = 0.65F;
constexpr int kHighResMissTolerance = 3;
constexpr int kProvisionalBufferFrames = 12;
constexpr int kProvisionalOutputHits = 3;
constexpr int kHighConfidenceProvisionalOutputHits = 2;
constexpr float kHighConfidenceProvisionalScore = 0.60F;
constexpr int kProvisionalOutputMissTolerance = 2;
constexpr int kFlowOutputMaxAge = 16;
constexpr int kFlowRetentionMaxAge = 24;
constexpr float kDuplicateSameClassIou = 0.45F;
constexpr float kDuplicateDifferentClassIou = 0.70F;
constexpr float kDuplicateCenterDiagonalRatio = 0.25F;
constexpr float kDuplicateMaxAreaRatio = 2.0F;

float boxArea(const Detection& detection) {
    return std::max(0.0F, detection.x2 - detection.x1)
        * std::max(0.0F, detection.y2 - detection.y1);
}

float boxIou(const Detection& a, const Detection& b) {
    const float inter_x1 = std::max(a.x1, b.x1);
    const float inter_y1 = std::max(a.y1, b.y1);
    const float inter_x2 = std::min(a.x2, b.x2);
    const float inter_y2 = std::min(a.y2, b.y2);
    const float inter_w = std::max(0.0F, inter_x2 - inter_x1);
    const float inter_h = std::max(0.0F, inter_y2 - inter_y1);
    const float inter_area = inter_w * inter_h;
    const float union_area = boxArea(a) + boxArea(b) - inter_area;
    return union_area > 0.0F ? inter_area / union_area : 0.0F;
}

float clipped(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

void clipDetection(Detection& detection, int image_width, int image_height) {
    const float max_x = static_cast<float>(std::max(image_width, 0));
    const float max_y = static_cast<float>(std::max(image_height, 0));
    detection.x1 = clipped(detection.x1, 0.0F, max_x);
    detection.x2 = clipped(detection.x2, 0.0F, max_x);
    detection.y1 = clipped(detection.y1, 0.0F, max_y);
    detection.y2 = clipped(detection.y2, 0.0F, max_y);
}

bool hasValidBox(const Detection& detection) {
    return detection.x2 > detection.x1 && detection.y2 > detection.y1;
}

Detection blendedDetection(
    const Detection& authority_detection,
    const Detection& low_res_detection,
    float low_res_weight
) {
    auto blend = [low_res_weight](float base, float update) {
        return base * (1.0F - low_res_weight) + update * low_res_weight;
    };

    Detection detection = authority_detection;
    detection.x1 = blend(authority_detection.x1, low_res_detection.x1);
    detection.y1 = blend(authority_detection.y1, low_res_detection.y1);
    detection.x2 = blend(authority_detection.x2, low_res_detection.x2);
    detection.y2 = blend(authority_detection.y2, low_res_detection.y2);
    detection.score = std::max(authority_detection.score * 0.95F, low_res_detection.score);
    return detection;
}

Detection blendedGeometry(
    const Detection& authority_detection,
    const Detection& measurement,
    float measurement_weight
) {
    auto blend = [measurement_weight](float base, float update) {
        return base * (1.0F - measurement_weight) + update * measurement_weight;
    };

    Detection detection = authority_detection;
    detection.x1 = blend(authority_detection.x1, measurement.x1);
    detection.y1 = blend(authority_detection.y1, measurement.y1);
    detection.x2 = blend(authority_detection.x2, measurement.x2);
    detection.y2 = blend(authority_detection.y2, measurement.y2);
    return detection;
}

Detection blendedDetection(
    const Detection& authority_detection,
    const Detection& low_res_detection
) {
    return blendedDetection(authority_detection, low_res_detection, kLowResBoxBlend);
}

Detection blendedFlowDetection(
    const Detection& authority_detection,
    const Detection& flow_detection,
    float flow_weight
) {
    return blendedGeometry(authority_detection, flow_detection, flow_weight);
}

float boxWidth(const Detection& detection) {
    return std::max(0.0F, detection.x2 - detection.x1);
}

float boxHeight(const Detection& detection) {
    return std::max(0.0F, detection.y2 - detection.y1);
}

float boxCenterX(const Detection& detection) {
    return (detection.x1 + detection.x2) * 0.5F;
}

float boxCenterY(const Detection& detection) {
    return (detection.y1 + detection.y2) * 0.5F;
}

float boxDiagonal(const Detection& detection) {
    const float width = boxWidth(detection);
    const float height = boxHeight(detection);
    return std::sqrt(width * width + height * height);
}

float centerDistance(const Detection& a, const Detection& b) {
    const float dx = boxCenterX(a) - boxCenterX(b);
    const float dy = boxCenterY(a) - boxCenterY(b);
    return std::sqrt(dx * dx + dy * dy);
}

float areaRatio(const Detection& a, const Detection& b) {
    const float area_a = boxArea(a);
    const float area_b = boxArea(b);
    if (area_a <= 0.0F || area_b <= 0.0F) {
        return std::numeric_limits<float>::infinity();
    }
    return std::max(area_a, area_b) / std::min(area_a, area_b);
}

bool duplicateGeometry(const Detection& a, const Detection& b) {
    const float iou = boxIou(a, b);
    if (a.class_id != b.class_id) {
        return iou >= kDuplicateDifferentClassIou;
    }
    if (iou >= kDuplicateSameClassIou) {
        return true;
    }
    const float diagonal = std::max(boxDiagonal(a), boxDiagonal(b));
    return diagonal > 0.0F
        && centerDistance(a, b) <= diagonal * kDuplicateCenterDiagonalRatio
        && areaRatio(a, b) <= kDuplicateMaxAreaRatio;
}

bool compatibleArea(const Detection& a, const Detection& b) {
    return areaRatio(a, b) <= kLowResMaxAreaRatio;
}

float spatialCompatibilityScore(const Detection& track_box, const Detection& detection) {
    if (!compatibleArea(track_box, detection)) {
        return 0.0F;
    }

    const float threshold = std::max(
        kLowResMinCenterDistancePx,
        kLowResCenterDistanceScale * std::max(
            boxDiagonal(track_box),
            boxDiagonal(detection)
        )
    );
    if (threshold <= 0.0F) {
        return 0.0F;
    }

    const float distance = centerDistance(track_box, detection);
    if (distance > threshold) {
        return 0.0F;
    }

    return 1.0F - distance / threshold;
}

float centerCompatibilityScore(const Detection& track_box, const Detection& detection) {
    if (track_box.class_id != detection.class_id) {
        return 0.0F;
    }
    return spatialCompatibilityScore(track_box, detection);
}

}  // namespace

std::vector<TrackMotion> directTrackMotions(
    const std::vector<TrackedDetection>& previous_tracks,
    const WeakTrackResult& weak_result
) {
    std::unordered_map<int, const Detection*> previous_by_id;
    previous_by_id.reserve(previous_tracks.size());
    for (const auto& track : previous_tracks) {
        previous_by_id.emplace(track.track_id, &track.detection);
    }

    std::vector<TrackMotion> motions;
    motions.reserve(weak_result.track_qualities.size());
    for (const auto& quality : weak_result.track_qualities) {
        if (!quality.accepted && !quality.boundary_clipped) {
            continue;
        }
        const auto previous = previous_by_id.find(quality.track_id);
        if (previous == previous_by_id.end()) {
            continue;
        }
        motions.push_back(TrackMotion{
            quality.track_id,
            quality.median_dx,
            quality.median_dy,
            boxCenterX(*previous->second),
            boxCenterY(*previous->second),
            boxWidth(*previous->second),
            boxHeight(*previous->second),
            std::max(0.0, 1.0 - quality.displacement_spread_ratio),
            quality.boundary_clipped,
            quality.visible_area_ratio <= 0.0
        });
    }
    return motions;
}

std::vector<ProjectedTrack> projectTracks(
    const std::vector<TrackedDetection>& tracks,
    const std::vector<TrackMotion>& direct_motions,
    const FrameMotion& motion,
    bool allow_global_motion,
    int image_width,
    int image_height
) {
    std::unordered_map<int, const TrackMotion*> direct_motion_by_id;
    direct_motion_by_id.reserve(direct_motions.size());
    for (const auto& direct_motion : direct_motions) {
        direct_motion_by_id.emplace(
            direct_motion.track_id,
            &direct_motion
        );
    }

    std::vector<ProjectedTrack> projected_tracks;
    projected_tracks.reserve(tracks.size());
    for (const auto& track : tracks) {
        const auto direct_motion = direct_motion_by_id.find(track.track_id);
        bool direct_flow = direct_motion != direct_motion_by_id.end();
        if (direct_flow) {
            const TrackMotion& candidate = *direct_motion->second;
            Detection source = track.detection;
            source.x1 = candidate.source_center_x - candidate.source_width * 0.5F;
            source.y1 = candidate.source_center_y - candidate.source_height * 0.5F;
            source.x2 = candidate.source_center_x + candidate.source_width * 0.5F;
            source.y2 = candidate.source_center_y + candidate.source_height * 0.5F;
            direct_flow = spatialCompatibilityScore(track.detection, source) > 0.0F;
        }
        if (!direct_flow && (!allow_global_motion || !motion.valid)) {
            continue;
        }

        const float dx = direct_flow ? direct_motion->second->dx : motion.dx;
        const float dy = direct_flow ? direct_motion->second->dy : motion.dy;
        Detection detection = track.detection;
        detection.x1 += dx;
        detection.x2 += dx;
        detection.y1 += dy;
        detection.y2 += dy;
        const float projected_center_x = boxCenterX(detection);
        const float projected_center_y = boxCenterY(detection);
        if (projected_center_x < 0.0F
            || projected_center_x >= static_cast<float>(image_width)
            || projected_center_y < 0.0F
            || projected_center_y >= static_cast<float>(image_height)) {
            projected_tracks.push_back(ProjectedTrack{
                track,
                direct_flow,
                direct_flow ? direct_motion->second->quality : motion.inlier_ratio,
                0.0,
                true,
                true
            });
            continue;
        }
        const float original_area = boxArea(detection);
        clipDetection(detection, image_width, image_height);
        if (!hasValidBox(detection)) {
            continue;
        }
        const double visible_area_ratio = original_area > 0.0F
            ? static_cast<double>(boxArea(detection))
                / static_cast<double>(original_area)
            : 0.0;
        projected_tracks.push_back(ProjectedTrack{
            TrackedDetection{track.track_id, detection},
            direct_flow,
            direct_flow ? direct_motion->second->quality : motion.inlier_ratio,
            visible_area_ratio,
            (direct_flow && direct_motion->second->boundary_clipped)
                || visible_area_ratio < 0.999,
            false
        });
    }

    std::sort(
        projected_tracks.begin(),
        projected_tracks.end(),
        [](const ProjectedTrack& a, const ProjectedTrack& b) {
            return a.track.track_id < b.track.track_id;
        }
    );
    return projected_tracks;
}

std::vector<TrackedDetection> projectedDetections(
    const std::vector<ProjectedTrack>& projected_tracks
) {
    std::vector<TrackedDetection> tracks;
    tracks.reserve(projected_tracks.size());
    for (const auto& projected : projected_tracks) {
        tracks.push_back(projected.track);
    }
    return tracks;
}

std::vector<TrackedDetection> AuthorityTracker::updateHighRes(
    const std::vector<Detection>& detections,
    int64_t frame_index
) {
    advanceFlowAge(frame_index);
    std::vector<bool> matched_tracks(tracks_.size(), false);
    std::vector<bool> matched_detections(detections.size(), false);
    const auto stable_matches = selectMatches(
        stableHighResEdges(detections),
        tracks_.size(),
        detections.size()
    );

    for (const Match& match : stable_matches) {
        AuthorityTrack& track = tracks_[match.track_index];
        track.detection = detections[match.detection_index];
        track.detector_score = track.detection.score;
        track.high_res_miss_count = 0;
        track.low_res_miss_count = 0;
        track.last_high_res_frame_index = frame_index;
        track.last_detector_frame_index = frame_index;
        track.flow_only_age = 0;
        track.flow_confidence = 1.0;
        track.exiting_frame_count = 0;
        track.geometry_rejection_count = 0;
        track.flow_age_refresh_reported = false;
        matched_tracks[match.track_index] = true;
        matched_detections[match.detection_index] = true;
    }

    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (!matched_tracks[i]) {
            ++tracks_[i].high_res_miss_count;
        }
    }
    eraseExpiredStableTracks(frame_index);

    const auto provisional_matches = selectMatches(
        provisionalHighResEdges(detections, matched_detections),
        provisionals_.size(),
        detections.size()
    );
    for (const Match& match : provisional_matches) {
        createConfirmedProvisionalTrack(
            provisionals_[match.track_index],
            detections[match.detection_index],
            frame_index
        );
        matched_detections[match.detection_index] = true;
    }

    diagnostics_.provisional_expired_count += static_cast<int64_t>(
        provisionals_.size() - provisional_matches.size()
    );
    // High-res is authoritative for existence: unmatched low-res candidates are rejected.
    provisionals_.clear();

    for (size_t i = 0; i < detections.size(); ++i) {
        if (!matched_detections[i] && hasValidBox(detections[i])) {
            createStableTrack(detections[i], frame_index);
        }
    }

    high_res_refresh_requested_ = false;
    consolidate(frame_index);
    return currentTracks(true);
}

std::vector<TrackedDetection> AuthorityTracker::updateHighResRegion(
    const std::vector<Detection>& detections,
    const std::vector<ProjectedTrack>& projected_tracks,
    const HighResRegion& region,
    int64_t frame_index
) {
    applyFlow(projected_tracks, frame_index);
    const auto center_in_region = [&region](const Detection& detection) {
        const float center_x = boxCenterX(detection);
        const float center_y = boxCenterY(detection);
        return center_x >= region.x1 && center_x < region.x2
            && center_y >= region.y1 && center_y < region.y2;
    };

    std::vector<bool> region_tracks(tracks_.size(), false);
    for (size_t i = 0; i < tracks_.size(); ++i) {
        region_tracks[i] = center_in_region(tracks_[i].detection);
    }
    auto stable_edges = stableHighResEdges(detections);
    stable_edges.erase(
        std::remove_if(
            stable_edges.begin(), stable_edges.end(),
            [&region_tracks](const Match& edge) {
                return !region_tracks[edge.track_index];
            }
        ),
        stable_edges.end()
    );

    std::vector<bool> matched_tracks(tracks_.size(), false);
    std::vector<bool> matched_detections(detections.size(), false);
    const auto stable_matches = selectMatches(
        std::move(stable_edges), tracks_.size(), detections.size()
    );
    for (const Match& match : stable_matches) {
        AuthorityTrack& track = tracks_[match.track_index];
        track.detection = detections[match.detection_index];
        track.detector_score = track.detection.score;
        track.high_res_miss_count = 0;
        track.low_res_miss_count = 0;
        track.last_high_res_frame_index = frame_index;
        track.last_detector_frame_index = frame_index;
        track.flow_only_age = 0;
        track.flow_confidence = 1.0;
        track.exiting_frame_count = 0;
        track.geometry_rejection_count = 0;
        track.flow_age_refresh_reported = false;
        matched_tracks[match.track_index] = true;
        matched_detections[match.detection_index] = true;
    }
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (region_tracks[i] && !matched_tracks[i]) {
            ++tracks_[i].high_res_miss_count;
        }
    }
    eraseExpiredStableTracks(frame_index);

    std::vector<bool> region_provisionals(provisionals_.size(), false);
    for (size_t i = 0; i < provisionals_.size(); ++i) {
        region_provisionals[i] = center_in_region(provisionals_[i].detection);
    }
    auto provisional_edges = provisionalHighResEdges(
        detections, matched_detections
    );
    provisional_edges.erase(
        std::remove_if(
            provisional_edges.begin(), provisional_edges.end(),
            [&region_provisionals](const Match& edge) {
                return !region_provisionals[edge.track_index];
            }
        ),
        provisional_edges.end()
    );
    const auto provisional_matches = selectMatches(
        std::move(provisional_edges), provisionals_.size(), detections.size()
    );
    std::vector<bool> matched_provisionals(provisionals_.size(), false);
    for (const Match& match : provisional_matches) {
        createConfirmedProvisionalTrack(
            provisionals_[match.track_index],
            detections[match.detection_index],
            frame_index
        );
        matched_provisionals[match.track_index] = true;
        matched_detections[match.detection_index] = true;
    }

    std::vector<ProvisionalTrack> retained_provisionals;
    retained_provisionals.reserve(provisionals_.size());
    for (size_t i = 0; i < provisionals_.size(); ++i) {
        if (!region_provisionals[i]) {
            retained_provisionals.push_back(std::move(provisionals_[i]));
        } else if (!matched_provisionals[i]) {
            ++diagnostics_.provisional_expired_count;
        }
    }
    provisionals_ = std::move(retained_provisionals);
    // Outside candidates are not rejected by this ROI, but normal age limits still apply.
    pruneStaleTracks(frame_index);

    for (size_t i = 0; i < detections.size(); ++i) {
        if (!matched_detections[i] && hasValidBox(detections[i])) {
            createStableTrack(detections[i], frame_index);
        }
    }

    consolidate(frame_index);
    return currentTracks(true);
}

std::vector<TrackedDetection> AuthorityTracker::updateLowRes(
    const std::vector<Detection>& detections,
    const std::vector<ProjectedTrack>& projected_tracks,
    int64_t frame_index
) {
    applyFlow(projected_tracks, frame_index);
    low_res_refresh_requested_ = false;

    std::vector<bool> matched_tracks(tracks_.size(), false);
    std::vector<bool> matched_detections(detections.size(), false);
    const auto stable_matches = selectMatches(
        stableLowResEdges(detections, frame_index),
        tracks_.size(),
        detections.size()
    );
    for (const Match& match : stable_matches) {
        AuthorityTrack& track = tracks_[match.track_index];
        const Detection candidate = blendedGeometry(
            track.detection,
            detections[match.detection_index],
            lowResBlendWeight(track, frame_index)
        );
        if (geometryUpdateAllowed(
                track,
                detections[match.detection_index],
                frame_index)) {
            track.detection = candidate;
            track.geometry_rejection_count = 0;
        } else {
            ++track.geometry_rejection_count;
            ++diagnostics_.low_res_geometry_rejection_count;
            if (track.geometry_rejection_count >= 2) {
                high_res_refresh_requested_ = true;
            }
        }
        track.low_res_miss_count = 0;
        track.last_low_res_frame_index = frame_index;
        track.last_detector_frame_index = frame_index;
        track.flow_only_age = 0;
        track.flow_confidence = 1.0;
        track.exiting_frame_count = 0;
        track.flow_age_refresh_reported = false;
        matched_tracks[match.track_index] = true;
        matched_detections[match.detection_index] = true;
    }
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (!matched_tracks[i]) {
            ++tracks_[i].low_res_miss_count;
        }
    }

    const auto provisional_matches = selectMatches(
        provisionalLowResEdges(detections, matched_detections),
        provisionals_.size(),
        detections.size()
    );
    std::vector<bool> matched_provisionals(provisionals_.size(), false);
    for (const Match& match : provisional_matches) {
        ProvisionalTrack& provisional = provisionals_[match.track_index];
        provisional.detection = blendedDetection(
            provisional.detection,
            detections[match.detection_index]
        );
        provisional.detector_score = std::max(
            provisional.detector_score,
            detections[match.detection_index].score
        );
        ++provisional.consecutive_low_res_hit_count;
        provisional.consecutive_low_res_miss_count = 0;
        provisional.last_low_res_frame_index = frame_index;
        provisional.last_detector_frame_index = frame_index;
        provisional.flow_only_age = 0;
        provisional.flow_confidence = 1.0;
        provisional.exiting_frame_count = 0;
        const int required_hits = provisional.detector_score
                >= kHighConfidenceProvisionalScore
            ? kHighConfidenceProvisionalOutputHits
            : kProvisionalOutputHits;
        provisional.output_confirmed =
            provisional.output_confirmed
            || provisional.consecutive_low_res_hit_count >= required_hits;
        matched_provisionals[match.track_index] = true;
        matched_detections[match.detection_index] = true;
    }
    for (size_t i = 0; i < provisionals_.size(); ++i) {
        if (!matched_provisionals[i]) {
            provisionals_[i].consecutive_low_res_hit_count = 0;
            ++provisionals_[i].consecutive_low_res_miss_count;
        }
    }

    for (size_t i = 0; i < detections.size(); ++i) {
        if (matched_detections[i] || !hasValidBox(detections[i])) {
            continue;
        }
        if (overlapsStableGeometry(detections[i])) {
            recordLowResClassConflict(detections[i], frame_index);
        } else {
            createProvisionalTrack(detections[i], frame_index);
        }
    }

    pruneStaleTracks(frame_index);
    consolidate(frame_index);
    return currentTracks(true);
}

std::vector<TrackedDetection> AuthorityTracker::updateFlow(
    const std::vector<ProjectedTrack>& projected_tracks,
    int64_t frame_index
) {
    applyFlow(projected_tracks, frame_index);
    pruneStaleTracks(frame_index);
    consolidate(frame_index);
    return currentTracks(true);
}

std::vector<TrackedDetection> AuthorityTracker::tracks() const {
    return currentTracks(false);
}

bool AuthorityTracker::consumeLowResRefreshRequest() {
    const bool requested = low_res_refresh_requested_;
    low_res_refresh_requested_ = false;
    return requested;
}

bool AuthorityTracker::consumeFlowAgeRefreshRequest() {
    size_t newly_near_limit = 0;
    for (auto& track : tracks_) {
        if (track.flow_only_age < kFlowOutputMaxAge - 2
            || track.flow_age_refresh_reported) {
            continue;
        }
        track.flow_age_refresh_reported = true;
        ++newly_near_limit;
    }
    return newly_near_limit > 0
        && newly_near_limit * 5 >= tracks_.size();
}

bool AuthorityTracker::consumeHighResRefreshRequest() {
    const bool requested = high_res_refresh_requested_;
    high_res_refresh_requested_ = false;
    return requested;
}

HighLowDiagnostics AuthorityTracker::diagnostics() const {
    HighLowDiagnostics diagnostics = diagnostics_;
    diagnostics.stable_track_count = static_cast<int64_t>(tracks_.size());
    diagnostics.provisional_track_count =
        static_cast<int64_t>(provisionals_.size());
    return diagnostics;
}

std::vector<AuthorityTracker::Match> AuthorityTracker::selectMatches(
    std::vector<Match> edges,
    size_t track_count,
    size_t detection_count
) {
    std::sort(edges.begin(), edges.end(), [](const Match& a, const Match& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.track_index != b.track_index) {
            return a.track_index < b.track_index;
        }
        return a.detection_index < b.detection_index;
    });

    std::vector<bool> used_tracks(track_count, false);
    std::vector<bool> used_detections(detection_count, false);
    std::vector<Match> matches;
    matches.reserve(std::min(track_count, detection_count));
    for (const Match& edge : edges) {
        if (used_tracks[edge.track_index] || used_detections[edge.detection_index]) {
            continue;
        }
        used_tracks[edge.track_index] = true;
        used_detections[edge.detection_index] = true;
        matches.push_back(edge);
    }
    return matches;
}

float AuthorityTracker::highResMatchScore(
    const Detection& track_box,
    const Detection& detection
) {
    const float iou = boxIou(track_box, detection);
    const float spatial_score = spatialCompatibilityScore(track_box, detection);
    const bool same_class = track_box.class_id == detection.class_id;
    if (same_class) {
        if (iou >= kHighResSameClassMatchIou) {
            return 4.0F + iou;
        }
        if (spatial_score >= 0.20F) {
            return 2.0F + spatial_score;
        }
        return 0.0F;
    }
    if (iou >= kHighResClassCorrectionIou) {
        return 3.0F + iou;
    }
    if (spatial_score >= 0.75F) {
        return 1.0F + spatial_score;
    }
    return 0.0F;
}

std::vector<AuthorityTracker::Match> AuthorityTracker::stableHighResEdges(
    const std::vector<Detection>& detections
) const {
    std::vector<Match> edges;
    for (size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        for (size_t detection_index = 0;
             detection_index < detections.size();
             ++detection_index) {
            const float score = highResMatchScore(
                tracks_[track_index].detection,
                detections[detection_index]
            );
            if (score > 0.0F) {
                edges.push_back(Match{track_index, detection_index, score});
            }
        }
    }
    return edges;
}

std::vector<AuthorityTracker::Match> AuthorityTracker::stableLowResEdges(
    const std::vector<Detection>& detections,
    int64_t frame_index
) const {
    std::vector<Match> edges;
    for (size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        const AuthorityTrack& track = tracks_[track_index];
        for (size_t detection_index = 0;
             detection_index < detections.size();
             ++detection_index) {
            const Detection& detection = detections[detection_index];
            if (track.detection.class_id != detection.class_id) {
                continue;
            }
            if (!compatibleArea(track.detection, detection)) {
                continue;
            }
            const float iou = boxIou(track.detection, detection);
            const float center_score = centerCompatibilityScore(
                track.detection,
                detection
            );
            const bool stale = lowResShouldPullTrack(track, frame_index);
            float score = 0.0F;
            if (!stale && iou >= kLowResMatchIou) {
                score = 2.0F + iou;
            } else if (!stale && center_score >= 0.65F) {
                score = 1.0F + center_score;
            } else if (stale
                && center_score >= 0.45F
                && (iou >= 0.12F || center_score >= 0.65F)) {
                score = 1.0F + center_score + iou;
            }
            if (score > 0.0F) {
                edges.push_back(Match{track_index, detection_index, score});
            }
        }
    }
    return edges;
}

std::vector<AuthorityTracker::Match> AuthorityTracker::provisionalHighResEdges(
    const std::vector<Detection>& detections,
    const std::vector<bool>& used_detections
) const {
    std::vector<Match> edges;
    for (size_t track_index = 0; track_index < provisionals_.size(); ++track_index) {
        for (size_t detection_index = 0;
             detection_index < detections.size();
             ++detection_index) {
            if (used_detections[detection_index]) {
                continue;
            }
            const Detection& provisional = provisionals_[track_index].detection;
            const Detection& detection = detections[detection_index];
            const float iou = boxIou(provisional, detection);
            const float spatial_score = spatialCompatibilityScore(
                provisional,
                detection
            );
            float score = 0.0F;
            if (iou >= kProvisionalMatchIou) {
                score = 3.0F + iou;
            } else if (spatial_score >= 0.60F) {
                score = 1.0F + spatial_score;
            }
            if (score > 0.0F && provisional.class_id == detection.class_id) {
                score += 1.0F;
            }
            if (score > 0.0F) {
                edges.push_back(Match{track_index, detection_index, score});
            }
        }
    }
    return edges;
}

std::vector<AuthorityTracker::Match> AuthorityTracker::provisionalLowResEdges(
    const std::vector<Detection>& detections,
    const std::vector<bool>& used_detections
) const {
    std::vector<Match> edges;
    for (size_t track_index = 0; track_index < provisionals_.size(); ++track_index) {
        for (size_t detection_index = 0;
             detection_index < detections.size();
             ++detection_index) {
            if (used_detections[detection_index]) {
                continue;
            }
            const Detection& provisional = provisionals_[track_index].detection;
            const Detection& detection = detections[detection_index];
            if (provisional.class_id != detection.class_id) {
                continue;
            }
            const float iou = boxIou(provisional, detection);
            const float center_score = centerCompatibilityScore(
                provisional,
                detection
            );
            const float score = iou >= kProvisionalCenterMatchIou
                ? 2.0F + iou
                : center_score;
            if (score > 0.0F) {
                edges.push_back(Match{track_index, detection_index, score});
            }
        }
    }
    return edges;
}

void AuthorityTracker::advanceFlowAge(int64_t frame_index) {
    auto advance = [frame_index](auto& track) {
        if (track.last_update_frame_index < 0) {
            track.last_update_frame_index = frame_index;
            return;
        }
        if (frame_index <= track.last_update_frame_index) {
            return;
        }
        const int64_t elapsed_frames = frame_index - track.last_update_frame_index;
        track.flow_only_age += static_cast<int>(elapsed_frames);
        track.flow_confidence *= std::pow(0.97, static_cast<double>(elapsed_frames));
        track.last_update_frame_index = frame_index;
    };
    for (auto& track : tracks_) {
        advance(track);
    }
    for (auto& track : provisionals_) {
        advance(track);
    }
}

void AuthorityTracker::applyFlow(
    const std::vector<ProjectedTrack>& projected_tracks,
    int64_t frame_index
) {
    advanceFlowAge(frame_index);
    for (const ProjectedTrack& projected : projected_tracks) {
        const int track_id = projected.track.track_id;
        auto stable = std::find_if(
            tracks_.begin(),
            tracks_.end(),
            [track_id](const AuthorityTrack& candidate) {
                return candidate.id == track_id;
            }
        );
        if (stable != tracks_.end()) {
            if (projected.center_outside) {
                stable->flow_only_age = kFlowRetentionMaxAge + 1;
                stable->exiting_frame_count = 2;
                continue;
            }
            const float weight = projected.direct_flow
                ? kDirectFlowBoxBlend
                : kGlobalFlowBoxBlend;
            stable->detection = blendedFlowDetection(
                stable->detection,
                projected.track.detection,
                weight
            );
            stable->flow_confidence *= projected.direct_flow
                ? 0.98 * projected.flow_quality
                : 0.88 * projected.flow_quality;
            stable->last_valid_flow_frame_index = frame_index;
            stable->exiting_frame_count = projected.boundary_clipped
                ? stable->exiting_frame_count + 1
                : 0;
            if (projected.direct_flow) {
                ++diagnostics_.direct_flow_update_count;
            } else {
                ++diagnostics_.global_flow_update_count;
            }
            continue;
        }

        auto provisional = std::find_if(
            provisionals_.begin(),
            provisionals_.end(),
            [track_id](const ProvisionalTrack& candidate) {
                return candidate.id == track_id;
            }
        );
        if (provisional != provisionals_.end()) {
            if (projected.center_outside) {
                provisional->flow_only_age = kProvisionalBufferFrames + 1;
                provisional->exiting_frame_count = 2;
                continue;
            }
            provisional->detection = blendedFlowDetection(
                provisional->detection,
                projected.track.detection,
                projected.direct_flow ? kDirectFlowBoxBlend : kGlobalFlowBoxBlend
            );
            provisional->flow_confidence *= projected.direct_flow
                ? 0.98 * projected.flow_quality
                : 0.88 * projected.flow_quality;
            provisional->last_valid_flow_frame_index = frame_index;
            provisional->exiting_frame_count = projected.boundary_clipped
                ? provisional->exiting_frame_count + 1
                : 0;
            if (projected.direct_flow) {
                ++diagnostics_.direct_flow_update_count;
            } else {
                ++diagnostics_.global_flow_update_count;
            }
        }
    }
}

void AuthorityTracker::createStableTrack(const Detection& detection, int64_t frame_index) {
    AuthorityTrack track;
    track.id = next_track_id_++;
    track.detection = detection;
    track.detector_score = detection.score;
    track.last_update_frame_index = frame_index;
    track.last_detector_frame_index = frame_index;
    track.last_high_res_frame_index = frame_index;
    tracks_.push_back(std::move(track));
}

void AuthorityTracker::createProvisionalTrack(const Detection& detection, int64_t frame_index) {
    ProvisionalTrack track;
    track.id = next_track_id_++;
    track.detection = detection;
    track.detector_score = detection.score;
    track.consecutive_low_res_hit_count = 1;
    track.last_update_frame_index = frame_index;
    track.last_detector_frame_index = frame_index;
    track.last_low_res_frame_index = frame_index;
    provisionals_.push_back(std::move(track));
    ++diagnostics_.provisional_created_count;
}

void AuthorityTracker::createConfirmedProvisionalTrack(
    const ProvisionalTrack& provisional,
    const Detection& high_res_detection,
    int64_t frame_index
) {
    AuthorityTrack track;
    track.id = provisional.id;
    track.detection = high_res_detection;
    track.detector_score = high_res_detection.score;
    track.last_update_frame_index = frame_index;
    track.last_detector_frame_index = frame_index;
    track.last_high_res_frame_index = frame_index;
    tracks_.push_back(std::move(track));
    ++diagnostics_.provisional_promoted_count;
}

bool AuthorityTracker::lowResShouldPullTrack(
    const AuthorityTrack& track,
    int64_t frame_index
) {
    return track.high_res_miss_count > 0
        || track.last_high_res_frame_index < 0
        || frame_index - track.last_high_res_frame_index
            >= kMinHighResCadenceFrames
        || track.low_res_miss_count > 0;
}

float AuthorityTracker::lowResBlendWeight(
    const AuthorityTrack& track,
    int64_t frame_index
) {
    return lowResShouldPullTrack(track, frame_index)
        ? kLowResStaleBoxBlend
        : kLowResBoxBlend;
}

bool AuthorityTracker::geometryUpdateAllowed(
    const AuthorityTrack& track,
    const Detection& candidate,
    int64_t frame_index
) {
    const float current_area = boxArea(track.detection);
    const float candidate_area = boxArea(candidate);
    if (current_area <= 0.0F || candidate_area <= 0.0F) {
        return false;
    }
    const float ratio = candidate_area / current_area;
    if (lowResShouldPullTrack(track, frame_index)) {
        return ratio >= 0.50F && ratio <= 2.00F;
    }
    return ratio >= 0.67F && ratio <= 1.50F;
}

bool AuthorityTracker::overlapsStableGeometry(const Detection& detection) const {
    return std::any_of(
        tracks_.begin(),
        tracks_.end(),
        [&detection](const AuthorityTrack& track) {
            return duplicateGeometry(track.detection, detection)
                || spatialCompatibilityScore(track.detection, detection) >= 0.50F;
        }
    );
}

void AuthorityTracker::recordLowResClassConflict(
    const Detection& detection,
    int64_t frame_index
) {
    auto conflict = std::find_if(
        tracks_.begin(),
        tracks_.end(),
        [&detection](const AuthorityTrack& track) {
            return track.detection.class_id != detection.class_id
                && (duplicateGeometry(track.detection, detection)
                    || spatialCompatibilityScore(track.detection, detection) >= 0.50F);
        }
    );
    if (conflict != tracks_.end()) {
        ++diagnostics_.low_res_class_conflict_count;
        conflict->low_res_miss_count = 0;
        conflict->last_low_res_frame_index = frame_index;
        conflict->last_detector_frame_index = frame_index;
        conflict->flow_only_age = 0;
        conflict->flow_confidence = 1.0;
        conflict->flow_age_refresh_reported = false;
        high_res_refresh_requested_ = true;
    }
}

void AuthorityTracker::eraseExpiredStableTracks(int64_t frame_index) {
    tracks_.erase(
        std::remove_if(
            tracks_.begin(),
            tracks_.end(),
            [this, frame_index](const AuthorityTrack& track) {
                const bool flow_expired = track.flow_only_age > kFlowRetentionMaxAge;
                if (flow_expired) {
                    ++diagnostics_.flow_age_expired_count;
                }
                return track.high_res_miss_count >= kHighResMissTolerance
                    || flow_expired
                    || (track.last_detector_frame_index >= 0
                        && frame_index - track.last_detector_frame_index
                            > kFlowRetentionMaxAge);
            }
        ),
        tracks_.end()
    );
}

void AuthorityTracker::pruneStaleTracks(int64_t frame_index) {
    eraseExpiredStableTracks(frame_index);
    const size_t provisional_count = provisionals_.size();
    provisionals_.erase(
        std::remove_if(
            provisionals_.begin(),
            provisionals_.end(),
            [frame_index](const ProvisionalTrack& track) {
                return track.last_detector_frame_index < 0
                    || frame_index - track.last_detector_frame_index
                        > kProvisionalBufferFrames
                    || track.flow_only_age > kProvisionalBufferFrames;
            }
        ),
        provisionals_.end()
    );
    diagnostics_.provisional_expired_count += static_cast<int64_t>(
        provisional_count - provisionals_.size()
    );
}

bool AuthorityTracker::stablePreferred(
    const AuthorityTrack& a,
    const AuthorityTrack& b
) {
    if (a.last_high_res_frame_index != b.last_high_res_frame_index) {
        return a.last_high_res_frame_index > b.last_high_res_frame_index;
    }
    if (a.last_detector_frame_index != b.last_detector_frame_index) {
        return a.last_detector_frame_index > b.last_detector_frame_index;
    }
    if (a.flow_only_age != b.flow_only_age) {
        return a.flow_only_age < b.flow_only_age;
    }
    if (a.detector_score != b.detector_score) {
        return a.detector_score > b.detector_score;
    }
    return a.id < b.id;
}

bool AuthorityTracker::provisionalPreferred(
    const ProvisionalTrack& a,
    const ProvisionalTrack& b
) {
    if (a.output_confirmed != b.output_confirmed) {
        return a.output_confirmed;
    }
    if (a.consecutive_low_res_hit_count != b.consecutive_low_res_hit_count) {
        return a.consecutive_low_res_hit_count > b.consecutive_low_res_hit_count;
    }
    if (a.detector_score != b.detector_score) {
        return a.detector_score > b.detector_score;
    }
    if (a.last_detector_frame_index != b.last_detector_frame_index) {
        return a.last_detector_frame_index > b.last_detector_frame_index;
    }
    return a.id < b.id;
}

void AuthorityTracker::mergeStableState(AuthorityTrack& winner, const AuthorityTrack& loser) {
    winner.high_res_miss_count = std::min(
        winner.high_res_miss_count,
        loser.high_res_miss_count
    );
    winner.low_res_miss_count = std::min(
        winner.low_res_miss_count,
        loser.low_res_miss_count
    );
    winner.flow_only_age = std::min(winner.flow_only_age, loser.flow_only_age);
    winner.flow_confidence = std::max(
        winner.flow_confidence,
        loser.flow_confidence
    );
    winner.last_high_res_frame_index = std::max(
        winner.last_high_res_frame_index,
        loser.last_high_res_frame_index
    );
    winner.last_low_res_frame_index = std::max(
        winner.last_low_res_frame_index,
        loser.last_low_res_frame_index
    );
    winner.last_detector_frame_index = std::max(
        winner.last_detector_frame_index,
        loser.last_detector_frame_index
    );
    winner.last_valid_flow_frame_index = std::max(
        winner.last_valid_flow_frame_index,
        loser.last_valid_flow_frame_index
    );
    winner.geometry_rejection_count = std::min(
        winner.geometry_rejection_count,
        loser.geometry_rejection_count
    );
    winner.exiting_frame_count = std::min(
        winner.exiting_frame_count,
        loser.exiting_frame_count
    );
}

void AuthorityTracker::consolidate(int64_t frame_index) {
    (void)frame_index;
    int64_t duplicates_this_update = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < tracks_.size() && !changed; ++i) {
            for (size_t j = i + 1; j < tracks_.size(); ++j) {
                if (!duplicateGeometry(tracks_[i].detection, tracks_[j].detection)) {
                    continue;
                }
                const size_t winner_index = stablePreferred(tracks_[i], tracks_[j])
                    ? i
                    : j;
                const size_t loser_index = winner_index == i ? j : i;
                AuthorityTrack winner = tracks_[winner_index];
                mergeStableState(winner, tracks_[loser_index]);
                tracks_[winner_index] = std::move(winner);
                tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(loser_index));
                ++diagnostics_.stable_stable_duplicate_count;
                ++duplicates_this_update;
                changed = true;
                break;
            }
        }
    }

    for (size_t provisional_index = 0;
         provisional_index < provisionals_.size();) {
        const bool duplicate = std::any_of(
            tracks_.begin(),
            tracks_.end(),
            [this, provisional_index](const AuthorityTrack& track) {
                return duplicateGeometry(
                    track.detection,
                    provisionals_[provisional_index].detection
                );
            }
        );
        if (!duplicate) {
            ++provisional_index;
            continue;
        }
        provisionals_.erase(
            provisionals_.begin() + static_cast<std::ptrdiff_t>(provisional_index)
        );
        ++diagnostics_.stable_provisional_duplicate_count;
        ++diagnostics_.provisional_deduplicated_count;
        ++duplicates_this_update;
    }

    changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < provisionals_.size() && !changed; ++i) {
            for (size_t j = i + 1; j < provisionals_.size(); ++j) {
                if (!duplicateGeometry(
                        provisionals_[i].detection,
                        provisionals_[j].detection)) {
                    continue;
                }
                const size_t winner_index = provisionalPreferred(
                    provisionals_[i],
                    provisionals_[j]
                ) ? i : j;
                const size_t loser_index = winner_index == i ? j : i;
                provisionals_.erase(
                    provisionals_.begin()
                        + static_cast<std::ptrdiff_t>(loser_index)
                );
                ++diagnostics_.provisional_provisional_duplicate_count;
                ++diagnostics_.provisional_deduplicated_count;
                ++duplicates_this_update;
                changed = true;
                break;
            }
        }
    }

    diagnostics_.max_stable_track_count = std::max(
        diagnostics_.max_stable_track_count,
        static_cast<int64_t>(tracks_.size())
    );
    diagnostics_.max_provisional_track_count = std::max(
        diagnostics_.max_provisional_track_count,
        static_cast<int64_t>(provisionals_.size())
    );
    if (duplicates_this_update >= 2) {
        low_res_refresh_requested_ = true;
    }
    if (duplicates_this_update >= 2) {
        high_res_refresh_requested_ = true;
    }
}

std::vector<TrackedDetection> AuthorityTracker::currentTracks(bool record_diagnostics) const {
    std::vector<TrackedDetection> output;
    output.reserve(tracks_.size() + provisionals_.size());
    auto append = [this, record_diagnostics, &output](
        int track_id,
        const Detection& detection
    ) {
        const bool duplicate = std::any_of(
            output.begin(),
            output.end(),
            [&detection](const TrackedDetection& existing) {
                return duplicateGeometry(existing.detection, detection);
            }
        );
        if (duplicate) {
            if (record_diagnostics) {
                ++diagnostics_.output_suppressed_duplicate_count;
            }
            return;
        }
        output.push_back(TrackedDetection{track_id, detection});
    };

    for (const auto& track : tracks_) {
        if (track.flow_only_age > kFlowOutputMaxAge) {
            if (record_diagnostics) {
                ++diagnostics_.flow_age_output_suppression_count;
            }
            continue;
        }
        if (track.exiting_frame_count >= 2) {
            if (record_diagnostics) {
                ++diagnostics_.exiting_track_suppression_count;
            }
            continue;
        }
        append(track.id, track.detection);
    }
    for (const auto& track : provisionals_) {
        if (track.output_confirmed
            && track.consecutive_low_res_miss_count
                < kProvisionalOutputMissTolerance
            && track.flow_only_age <= kFlowOutputMaxAge
            && track.exiting_frame_count < 2) {
            append(track.id, track.detection);
        }
    }
    std::sort(output.begin(), output.end(), [](const auto& a, const auto& b) {
        return a.track_id < b.track_id;
    });
    return output;
}
}  // namespace yolo
