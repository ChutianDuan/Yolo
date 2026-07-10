#include "video/video_inference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "image/image_processing.h"
#include "video/optical_flow_tracker.h"
#include "video/video_inference_detail.h"

namespace yolo {
namespace {

using video_inference_detail::applyTrackChangeToStride;
using video_inference_detail::applyWeakQualityToStride;
using video_inference_detail::addEndToEndSample;
using video_inference_detail::addInferMetrics;
using video_inference_detail::addTrackerMetrics;
using video_inference_detail::AsyncInferResult;
using video_inference_detail::AsyncInferWorker;
using video_inference_detail::DynamicStrideState;
using video_inference_detail::elapsedMs;
using video_inference_detail::fillVideoSummary;
using video_inference_detail::finalizeVideoPerformanceMetrics;
using video_inference_detail::finiteOrZero;
using video_inference_detail::frameTimestampMs;
using video_inference_detail::invalidVideoFrameMessage;
using video_inference_detail::makeDynamicStrideState;
using video_inference_detail::TrackChangeQuality;
using video_inference_detail::trackChangeQuality;
using video_inference_detail::updateTrackVelocities;
using video_inference_detail::videoFrameStride;

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
constexpr size_t kReplayBufferFrames = 60;
constexpr int kHighResCadenceMultiplier = 4;
constexpr int kMinHighResCadenceFrames = 24;

int highResCadenceFrames(int base_frame_stride) {
    if (base_frame_stride <= 1) {
        return 1;
    }
    return std::max(
        base_frame_stride * kHighResCadenceMultiplier,
        kMinHighResCadenceFrames
    );
}

bool frameDue(int64_t last_frame_index, int64_t frame_index, int stride) {
    return last_frame_index < 0 || frame_index - last_frame_index >= stride;
}

bool shouldRunLowResDetection(
    const DynamicStrideState& stride_state,
    int64_t frame_index,
    int64_t detection_count
) {
    if (detection_count == 0 || stride_state.base_stride <= 1) {
        return true;
    }
    if (stride_state.last_detection_frame_index < 0) {
        return true;
    }

    const int low_res_stride = std::max(1, stride_state.current_stride);
    return frame_index - stride_state.last_detection_frame_index >= low_res_stride;
}

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

InferResult runModel(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    const cv::Mat& frame
) {
    const auto preprocess_start = std::chrono::steady_clock::now();
    const auto input = preprocessImageMat(frame, config);
    const double preprocess_ms = elapsedMs(preprocess_start);
    if (!input.has_value()) {
        throw VideoInferError(invalidVideoFrameMessage(config), true);
    }

    InferResult result = engine->infer(input.value());
    result.preprocess_ms = preprocess_ms;
    result.timing_samples.preprocess_ms.push_back(preprocess_ms);
    result.timing_samples.queue_wait_ms.push_back(0.0);
    return result;
}

void addInferTiming(VideoInferResult& result, const InferResult& infer_result) {
    addInferMetrics(result, infer_result);
}

void updateStrideBounds(
    const DynamicStrideState& stride_state,
    int& min_stride_used,
    int& max_stride_used
) {
    min_stride_used = std::min(min_stride_used, stride_state.current_stride);
    max_stride_used = std::max(max_stride_used, stride_state.current_stride);
}

class AuthorityTracker {
public:
    std::vector<TrackedDetection> updateHighRes(
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

    std::vector<TrackedDetection> updateLowRes(
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

    std::vector<TrackedDetection> updateFlow(
        const std::vector<ProjectedTrack>& projected_tracks,
        int64_t frame_index
    ) {
        applyFlow(projected_tracks, frame_index);
        pruneStaleTracks(frame_index);
        consolidate(frame_index);
        return currentTracks(true);
    }

    std::vector<TrackedDetection> tracks() const {
        return currentTracks(false);
    }

    bool consumeLowResRefreshRequest() {
        const bool requested = low_res_refresh_requested_;
        low_res_refresh_requested_ = false;
        return requested;
    }

    bool consumeFlowAgeRefreshRequest() {
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

    bool consumeHighResRefreshRequest() {
        const bool requested = high_res_refresh_requested_;
        high_res_refresh_requested_ = false;
        return requested;
    }

    HighLowDiagnostics diagnostics() const {
        HighLowDiagnostics diagnostics = diagnostics_;
        diagnostics.stable_track_count = static_cast<int64_t>(tracks_.size());
        diagnostics.provisional_track_count =
            static_cast<int64_t>(provisionals_.size());
        return diagnostics;
    }

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

    static float highResMatchScore(
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

    std::vector<Match> stableHighResEdges(
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

    std::vector<Match> stableLowResEdges(
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

    std::vector<Match> provisionalHighResEdges(
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

    std::vector<Match> provisionalLowResEdges(
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

    void advanceFlowAge(int64_t frame_index) {
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

    void applyFlow(
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

    void createStableTrack(const Detection& detection, int64_t frame_index) {
        AuthorityTrack track;
        track.id = next_track_id_++;
        track.detection = detection;
        track.detector_score = detection.score;
        track.last_update_frame_index = frame_index;
        track.last_detector_frame_index = frame_index;
        track.last_high_res_frame_index = frame_index;
        tracks_.push_back(std::move(track));
    }

    void createProvisionalTrack(const Detection& detection, int64_t frame_index) {
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

    void createConfirmedProvisionalTrack(
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

    static bool lowResShouldPullTrack(
        const AuthorityTrack& track,
        int64_t frame_index
    ) {
        return track.high_res_miss_count > 0
            || track.last_high_res_frame_index < 0
            || frame_index - track.last_high_res_frame_index
                >= kMinHighResCadenceFrames
            || track.low_res_miss_count > 0;
    }

    static float lowResBlendWeight(
        const AuthorityTrack& track,
        int64_t frame_index
    ) {
        return lowResShouldPullTrack(track, frame_index)
            ? kLowResStaleBoxBlend
            : kLowResBoxBlend;
    }

    static bool geometryUpdateAllowed(
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

    bool overlapsStableGeometry(const Detection& detection) const {
        return std::any_of(
            tracks_.begin(),
            tracks_.end(),
            [&detection](const AuthorityTrack& track) {
                return duplicateGeometry(track.detection, detection)
                    || spatialCompatibilityScore(track.detection, detection) >= 0.50F;
            }
        );
    }

    void recordLowResClassConflict(
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

    void eraseExpiredStableTracks(int64_t frame_index) {
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

    void pruneStaleTracks(int64_t frame_index) {
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

    static bool stablePreferred(
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

    static bool provisionalPreferred(
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

    void mergeStableState(AuthorityTrack& winner, const AuthorityTrack& loser) {
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

    void consolidate(int64_t frame_index) {
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

    std::vector<TrackedDetection> currentTracks(bool record_diagnostics) const {
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

    int next_track_id_ = 1;
    std::vector<AuthorityTrack> tracks_;
    std::vector<ProvisionalTrack> provisionals_;
    mutable HighLowDiagnostics diagnostics_;
    bool low_res_refresh_requested_ = false;
    bool high_res_refresh_requested_ = false;
};

struct ReplayFrame {
    int64_t frame_index = 0;
    int image_width = 0;
    int image_height = 0;
    FrameMotion motion;
    std::vector<TrackMotion> direct_motions;
    std::vector<Detection> low_res_detections;
    std::vector<Detection> high_res_detections;
    size_t result_index = 0;
    bool allow_global_motion = false;
    bool has_low_res = false;
    bool has_high_res = false;
};

std::vector<ProjectedTrack> replayProjectedTracks(
    const ReplayFrame& frame,
    const AuthorityTracker& tracker
) {
    return projectTracks(
        tracker.tracks(),
        frame.direct_motions,
        frame.motion,
        frame.allow_global_motion,
        frame.image_width,
        frame.image_height
    );
}

std::vector<TrackedDetection> applyReplayFrame(
    const ReplayFrame& frame,
    AuthorityTracker& tracker
) {
    if (frame.has_high_res) {
        return tracker.updateHighRes(frame.high_res_detections, frame.frame_index);
    }

    const auto projected_tracks = replayProjectedTracks(frame, tracker);
    if (frame.has_low_res) {
        return tracker.updateLowRes(
            frame.low_res_detections,
            projected_tracks,
            frame.frame_index
        );
    }
    return tracker.updateFlow(projected_tracks, frame.frame_index);
}

void trimReplayBuffer(
    std::deque<ReplayFrame>& replay_buffer,
    AuthorityTracker& base_tracker,
    size_t max_frames
) {
    while (replay_buffer.size() > max_frames) {
        applyReplayFrame(replay_buffer.front(), base_tracker);
        replay_buffer.pop_front();
    }
}

void setFrameTracks(
    VideoFrameTracks& frame,
    const std::vector<TrackedDetection>& tracks,
    const char* source,
    bool detection_frame
) {
    frame.tracks = tracks;
    frame.is_detection_frame = detection_frame;
    frame.tracks_source = frame.tracks.empty() ? "empty" : source;
}

}  // namespace

VideoInferResult inferVideoFileHighLow(
    const std::shared_ptr<YoloEngine>& high_res_engine,
    const std::shared_ptr<YoloEngine>& low_res_engine,
    const AppConfig& config,
    const std::filesystem::path& video_path
) {
    if (low_res_engine == nullptr || !hasLowResModelConfig(config)) {
        return inferVideoFile(high_res_engine, config, video_path);
    }

    const AppConfig low_res_config = makeLowResAppConfig(config);
    const auto total_start = std::chrono::steady_clock::now();
    const ProcessUsageSnapshot usage_start = captureProcessUsage();

    cv::VideoCapture capture;
    if (!openVideoCapture(capture, video_path)) {
        throw VideoInferError(videoOpenFailureMessage(video_path), true);
    }

    const double source_fps = finiteOrZero(capture.get(cv::CAP_PROP_FPS));
    const double declared_frame_count = finiteOrZero(capture.get(cv::CAP_PROP_FRAME_COUNT));
    const int base_frame_stride = videoFrameStride(source_fps, config.video_detect_fps);
    DynamicStrideState stride_state = makeDynamicStrideState(base_frame_stride);
    const bool dynamic_stride_enabled = base_frame_stride > 1
        && config.video_stride_mode == "dynamic";
    const std::string stride_mode = base_frame_stride <= 1
        ? "full_model_high_low"
        : std::string(config.video_model_async ? "async_high_low_" : "sync_high_low_")
            + (dynamic_stride_enabled ? "dynamic" : "fixed");

    VideoInferResult result;
    if (declared_frame_count > 0.0) {
        result.frames.reserve(static_cast<size_t>(declared_frame_count));
    }

    AuthorityTracker tracker;
    cv::Mat frame;
    cv::Mat previous_gray;
    std::vector<TrackedDetection> previous_frame_tracks;
    std::unordered_map<int, cv::Point2f> track_velocities;

    int64_t frame_index = 0;
    int64_t source_frame_count = 0;
    int64_t readable_frame_count = 0;
    int64_t processed_frame_count = 0;
    int64_t high_res_detection_count = 0;
    int64_t forced_detection_count = 0;
    int64_t scheduled_detection_count = 0;
    int64_t skipped_detection_count = 0;
    int image_width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    int image_height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    int min_stride_used = stride_state.current_stride;
    int max_stride_used = stride_state.current_stride;
    HighLowDiagnostics runtime_diagnostics;
    HighLowDiagnostics previous_tracker_diagnostics;
    int severe_flow_streak = 0;
    bool weak_quality_degraded = false;
    int64_t last_urgent_low_res_frame_index = -1;
    int64_t last_urgent_high_res_frame_index = -1;
    int64_t last_flow_quality_refresh_frame_index = -1;
    bool pending_low_res_refresh = false;
    bool pending_high_res_refresh = false;

    auto applyDetections = [
        &tracker,
        &result,
        &previous_frame_tracks,
        &track_velocities,
        &processed_frame_count,
        &forced_detection_count,
        &scheduled_detection_count,
        &stride_state,
        &min_stride_used,
        &max_stride_used,
        dynamic_stride_enabled
    ](const std::vector<Detection>& detections,
      int64_t source_frame_index,
      bool forced,
      bool high_res,
      const std::vector<ProjectedTrack>& projected_tracks,
      VideoFrameTracks& frame_result) {
        auto tracker_start = std::chrono::steady_clock::now();
        const auto tracks_before_detection = previous_frame_tracks;
        auto detected_tracks = high_res
            ? tracker.updateHighRes(detections, source_frame_index)
            : tracker.updateLowRes(detections, projected_tracks, source_frame_index);
        const TrackChangeQuality detection_change = trackChangeQuality(
            tracks_before_detection,
            detected_tracks,
            track_velocities
        );
        if (dynamic_stride_enabled) {
            applyTrackChangeToStride(stride_state, detection_change);
            if (forced && !high_res) {
                stride_state.current_stride = stride_state.base_stride;
                stride_state.stable_frame_count = 0;
                stride_state.complex_frame_count = 0;
            }
        }
        updateTrackVelocities(
            tracks_before_detection,
            detected_tracks,
            track_velocities
        );

        frame_result.is_detection_frame = true;
        frame_result.tracks = detected_tracks;
        frame_result.tracks_source = frame_result.tracks.empty() ? "empty" : "detected";
        previous_frame_tracks = std::move(detected_tracks);

        stride_state.last_detection_frame_index = source_frame_index;
        ++processed_frame_count;
        if (forced) {
            ++forced_detection_count;
        } else {
            ++scheduled_detection_count;
        }
        updateStrideBounds(stride_state, min_stride_used, max_stride_used);
        addTrackerMetrics(result, elapsedMs(tracker_start));
    };

    auto applyWeakTracks = [
        &tracker,
        &result,
        &previous_frame_tracks,
        &track_velocities,
        &stride_state,
        &min_stride_used,
        &max_stride_used,
        dynamic_stride_enabled
    ](const std::vector<ProjectedTrack>& projected_tracks,
      VideoFrameTracks& frame_result) {
        auto tracker_start = std::chrono::steady_clock::now();
        const auto tracks_before = previous_frame_tracks;

        frame_result.tracks = tracker.updateFlow(
            projected_tracks,
            frame_result.frame_index
        );
        if (!frame_result.tracks.empty()) {
            frame_result.tracks_source = "weak_tracked";
        }

        previous_frame_tracks = frame_result.tracks;
        const TrackChangeQuality tracked_change = trackChangeQuality(
            tracks_before,
            previous_frame_tracks,
            track_velocities
        );
        if (dynamic_stride_enabled) {
            applyTrackChangeToStride(stride_state, tracked_change);
        }
        updateTrackVelocities(
            tracks_before,
            previous_frame_tracks,
            track_velocities
        );
        updateStrideBounds(stride_state, min_stride_used, max_stride_used);
        addTrackerMetrics(result, elapsedMs(tracker_start));
    };

    const int high_res_cadence = highResCadenceFrames(base_frame_stride);
    int64_t last_high_res_frame_index = -1;
    int64_t async_infer_request_count = 0;
    int64_t async_correction_count = 0;
    int64_t dropped_frame_count = 0;
    size_t max_queue_length = 0;
    std::deque<ReplayFrame> replay_buffer;
    AuthorityTracker replay_base_tracker;
    std::unique_ptr<AsyncInferWorker> high_res_worker;
    if (config.video_model_async) {
        high_res_worker = std::make_unique<AsyncInferWorker>(high_res_engine, config);
    }
    std::string async_error_message;
    bool async_error_bad_request = false;

    auto refreshVelocitiesFromReplay = [
        &track_velocities,
        &replay_buffer,
        &result
    ]() {
        track_velocities.clear();
        if (replay_buffer.size() < 2) {
            return;
        }

        const size_t previous_index = replay_buffer[replay_buffer.size() - 2].result_index;
        const size_t current_index = replay_buffer.back().result_index;
        if (previous_index < result.frames.size() && current_index < result.frames.size()) {
            updateTrackVelocities(
                result.frames[previous_index].tracks,
                result.frames[current_index].tracks,
                track_velocities
            );
        }
    };

    auto replayFrom = [
        &tracker,
        &previous_frame_tracks,
        &replay_buffer,
        &replay_base_tracker,
        &result,
        &refreshVelocitiesFromReplay
    ](size_t start_index) {
        if (start_index >= replay_buffer.size()) {
            return;
        }

        const int64_t anchor_frame_index = replay_buffer[start_index].frame_index;
        AuthorityTracker replay_tracker = replay_base_tracker;
        for (size_t i = 0; i < replay_buffer.size(); ++i) {
            ReplayFrame& frame = replay_buffer[i];
            const bool detection_frame = frame.has_high_res || frame.has_low_res;
            const char* source = detection_frame ? "detected" : "weak_tracked";
            const auto output_tracks = applyReplayFrame(frame, replay_tracker);

            if (i >= start_index && frame.result_index < result.frames.size()) {
                setFrameTracks(
                    result.frames[frame.result_index],
                    output_tracks,
                    source,
                    detection_frame
                );
                result.frames[frame.result_index].corrected_from_frame_index =
                    anchor_frame_index;
                result.frames[frame.result_index].correction_latency_frames =
                    frame.frame_index - anchor_frame_index;
            }
        }

        tracker = std::move(replay_tracker);
        previous_frame_tracks = tracker.tracks();
        refreshVelocitiesFromReplay();
    };

    auto findReplayFrame = [&replay_buffer](int64_t target_frame_index) {
        for (size_t i = 0; i < replay_buffer.size(); ++i) {
            if (replay_buffer[i].frame_index == target_frame_index) {
                return i;
            }
        }
        return replay_buffer.size();
    };

    auto processHighResResults = [
        &high_res_worker,
        &result,
        &async_correction_count,
        &dropped_frame_count,
        &async_error_message,
        &async_error_bad_request,
        &findReplayFrame,
        &replay_buffer,
        &replayFrom
    ](bool wait_for_all) {
        if (high_res_worker == nullptr) {
            return true;
        }

        while (true) {
            AsyncInferResult async_result;
            const bool has_result = wait_for_all
                ? high_res_worker->waitPopResult(async_result)
                : high_res_worker->tryPopResult(async_result);
            if (!has_result) {
                return true;
            }

            if (!async_result.ok) {
                async_error_message = async_result.error_message;
                async_error_bad_request = async_result.bad_request;
                return false;
            }

            addInferTiming(result, async_result.result);

            const size_t replay_index = findReplayFrame(async_result.frame_index);
            if (replay_index == replay_buffer.size()) {
                ++dropped_frame_count;
                continue;
            }

            replay_buffer[replay_index].has_high_res = true;
            replay_buffer[replay_index].high_res_detections =
                std::move(async_result.result.detections);
            replayFrom(replay_index);
            ++async_correction_count;
        }
    };

    while (capture.read(frame)) {
        if (!processHighResResults(false)) {
            throw VideoInferError(async_error_message, async_error_bad_request);
        }

        const auto frame_start = std::chrono::steady_clock::now();
        VideoFrameTracks frame_result;
        frame_result.frame_index = frame_index;
        frame_result.timestamp_ms = frameTimestampMs(
            frame_index,
            source_fps,
            finiteOrZero(capture.get(cv::CAP_PROP_POS_MSEC))
        );
        ++source_frame_count;

        if (frame.empty()) {
            result.frames.push_back(std::move(frame_result));
            addEndToEndSample(result, elapsedMs(frame_start));
            ++frame_index;
            continue;
        }

        ++readable_frame_count;
        image_width = frame.cols;
        image_height = frame.rows;

        cv::Mat current_gray;
        cv::cvtColor(frame, current_gray, cv::COLOR_BGR2GRAY);

        auto flow_start = std::chrono::steady_clock::now();
        const auto weak_result = weakTrackWithOpticalFlow(
            previous_gray,
            current_gray,
            previous_frame_tracks,
            image_width,
            image_height
        );
        const FrameMotion frame_motion = frameMotionForCurrentFrame(
            previous_gray,
            current_gray,
            frame_index,
            weak_result.quality,
            previous_frame_tracks
        );
        const bool severe_weak_drop = isSevereWeakQualityDrop(weak_result.quality);
        auto direct_motions = directTrackMotions(
            previous_frame_tracks,
            weak_result
        );
        const auto projected_tracks = projectTracks(
            previous_frame_tracks,
            direct_motions,
            frame_motion,
            !severe_weak_drop,
            image_width,
            image_height
        );
        const auto projected_detections = projectedDetections(projected_tracks);
        result.optical_flow_ms += elapsedMs(flow_start);

        const TrackChangeQuality flow_track_change = trackChangeQuality(
            previous_frame_tracks,
            projected_detections,
            track_velocities
        );
        const int64_t rejected_flow_tracks = static_cast<int64_t>(std::count_if(
            weak_result.track_qualities.begin(),
            weak_result.track_qualities.end(),
            [](const TrackFlowQuality& quality) {
                return !quality.accepted;
            }
        ));
        runtime_diagnostics.flow_track_count += static_cast<int64_t>(
            weak_result.track_qualities.size()
        );
        runtime_diagnostics.flow_rejected_track_count += rejected_flow_tracks;
        runtime_diagnostics.flow_low_point_rejection_count += static_cast<int64_t>(
            weak_result.quality.low_point_track_count
        );
        runtime_diagnostics.flow_invalid_ratio_rejection_count +=
            static_cast<int64_t>(weak_result.quality.invalid_ratio_track_count);
        runtime_diagnostics.flow_forward_backward_rejection_count +=
            static_cast<int64_t>(
                weak_result.quality.forward_backward_rejection_count
            );
        runtime_diagnostics.flow_motion_dispersion_rejection_count +=
            static_cast<int64_t>(
                weak_result.quality.motion_dispersion_rejection_count
            );
        runtime_diagnostics.flow_motion_jump_rejection_count +=
            static_cast<int64_t>(weak_result.quality.motion_jump_rejection_count);
        runtime_diagnostics.flow_boundary_rejection_count += static_cast<int64_t>(
            weak_result.quality.boundary_rejection_count
        );
        const bool flow_quality_bad =
            !weak_result.track_qualities.empty()
            && (rejected_flow_tracks * 5
                    > static_cast<int64_t>(weak_result.track_qualities.size())
                || severe_weak_drop);
        const bool flow_quality_urgent = flow_quality_bad
            && (!weak_quality_degraded
                || frameDue(
                    last_flow_quality_refresh_frame_index,
                    frame_index,
                    highResCadenceFrames(base_frame_stride)
                ));
        if (dynamic_stride_enabled) {
            if (flow_quality_urgent || !flow_quality_bad) {
                applyWeakQualityToStride(stride_state, weak_result.quality);
            }
            applyTrackChangeToStride(stride_state, flow_track_change);
            updateStrideBounds(stride_state, min_stride_used, max_stride_used);
        }

        weak_quality_degraded = flow_quality_bad;
        severe_flow_streak = severe_weak_drop ? severe_flow_streak + 1 : 0;
        const bool track_change_urgent = flow_track_change.scale_jump_count > 0
            || flow_track_change.velocity_jump_count > 0;
        const HighLowDiagnostics tracker_diagnostics = tracker.diagnostics();
        const int64_t duplicate_count =
            tracker_diagnostics.stable_stable_duplicate_count
            + tracker_diagnostics.stable_provisional_duplicate_count
            + tracker_diagnostics.provisional_provisional_duplicate_count;
        const int64_t previous_duplicate_count =
            previous_tracker_diagnostics.stable_stable_duplicate_count
            + previous_tracker_diagnostics.stable_provisional_duplicate_count
            + previous_tracker_diagnostics.provisional_provisional_duplicate_count;
        const bool duplicate_urgent = tracker.consumeLowResRefreshRequest()
            || duplicate_count - previous_duplicate_count >= 2;
        const bool flow_age_urgent = tracker.consumeFlowAgeRefreshRequest();
        const bool geometry_urgent =
            tracker_diagnostics.low_res_geometry_rejection_count
                > previous_tracker_diagnostics.low_res_geometry_rejection_count;
        const bool class_conflict_urgent =
            tracker_diagnostics.low_res_class_conflict_count
                > previous_tracker_diagnostics.low_res_class_conflict_count;
        previous_tracker_diagnostics = tracker_diagnostics;

        pending_low_res_refresh = pending_low_res_refresh
            || flow_quality_urgent
            || track_change_urgent
            || duplicate_urgent
            || flow_age_urgent;
        const bool urgent_low_res = pending_low_res_refresh
            && frameDue(
                last_urgent_low_res_frame_index,
                frame_index,
                severe_weak_drop
                    ? std::max(2, stride_state.urgent_stride)
                    : std::max(3, stride_state.complex_stride)
            );
        if (urgent_low_res) {
            pending_low_res_refresh = false;
            last_urgent_low_res_frame_index = frame_index;
            ++runtime_diagnostics.urgent_low_res_detection_count;
            runtime_diagnostics.urgent_flow_quality_count += flow_quality_urgent ? 1 : 0;
            runtime_diagnostics.urgent_track_change_count += track_change_urgent ? 1 : 0;
            runtime_diagnostics.urgent_duplicate_count += duplicate_urgent ? 1 : 0;
            runtime_diagnostics.urgent_flow_age_count += flow_age_urgent ? 1 : 0;
            if (flow_quality_urgent) {
                last_flow_quality_refresh_frame_index = frame_index;
            }
        }

        const bool scheduled_detection = urgent_low_res || shouldRunLowResDetection(
            stride_state,
            frame_index,
            processed_frame_count
        );
        const bool tracker_high_res_refresh = tracker.consumeHighResRefreshRequest();
        pending_high_res_refresh = pending_high_res_refresh
            || tracker_high_res_refresh
            || severe_flow_streak >= 2;
        const bool force_high_res = high_res_detection_count == 0
            || (pending_high_res_refresh
                && frameDue(
                    last_urgent_high_res_frame_index,
                    frame_index,
                    std::max(24, base_frame_stride * 3)
                ));
        if (force_high_res && high_res_detection_count > 0) {
            pending_high_res_refresh = false;
            last_urgent_high_res_frame_index = frame_index;
            ++runtime_diagnostics.urgent_high_res_detection_count;
            runtime_diagnostics.urgent_geometry_count += geometry_urgent ? 1 : 0;
            runtime_diagnostics.urgent_class_conflict_count +=
                class_conflict_urgent ? 1 : 0;
        }
        if (force_high_res && high_res_detection_count == 0) {
            pending_high_res_refresh = false;
            last_urgent_high_res_frame_index = frame_index;
        }
        const bool scheduled_high_res = scheduled_detection
            && frameDue(last_high_res_frame_index, frame_index, high_res_cadence);
        const bool request_high_res = force_high_res || scheduled_high_res;

        ReplayFrame replay_frame;
        replay_frame.frame_index = frame_index;
        replay_frame.image_width = image_width;
        replay_frame.image_height = image_height;
        replay_frame.motion = frame_motion;
        replay_frame.direct_motions = std::move(direct_motions);
        replay_frame.allow_global_motion = !severe_weak_drop;

        if (request_high_res && high_res_worker != nullptr) {
            size_t queue_length = high_res_worker->pendingCount();
            max_queue_length = std::max(max_queue_length, queue_length);
            if (high_res_worker->submit(frame.clone(), frame_index, &queue_length)) {
                ++async_infer_request_count;
                last_high_res_frame_index = frame_index;
                ++high_res_detection_count;
            } else {
                ++skipped_detection_count;
                ++dropped_frame_count;
            }
            max_queue_length = std::max(max_queue_length, queue_length);
        }

        if (request_high_res && high_res_worker == nullptr) {
            InferResult high_result = runModel(high_res_engine, config, frame);
            addInferTiming(result, high_result);
            replay_frame.has_high_res = true;
            applyDetections(
                high_result.detections,
                frame_index,
                force_high_res && high_res_detection_count > 0,
                true,
                {},
                frame_result
            );
            replay_frame.high_res_detections = std::move(high_result.detections);
            ++high_res_detection_count;
            last_high_res_frame_index = frame_index;
        } else if (scheduled_detection) {
            InferResult low_result = runModel(low_res_engine, low_res_config, frame);
            addInferTiming(result, low_result);
            replay_frame.has_low_res = true;

            applyDetections(
                low_result.detections,
                frame_index,
                urgent_low_res,
                false,
                projected_tracks,
                frame_result
            );
            replay_frame.low_res_detections = std::move(low_result.detections);
        } else {
            applyWeakTracks(projected_tracks, frame_result);
        }

        previous_gray = current_gray;
        replay_frame.result_index = result.frames.size();
        result.frames.push_back(std::move(frame_result));
        replay_buffer.push_back(std::move(replay_frame));
        trimReplayBuffer(replay_buffer, replay_base_tracker, kReplayBufferFrames);
        addEndToEndSample(result, elapsedMs(frame_start));
        if (!processHighResResults(false)) {
            throw VideoInferError(async_error_message, async_error_bad_request);
        }
        ++frame_index;
    }

    if (!processHighResResults(true)) {
        throw VideoInferError(async_error_message, async_error_bad_request);
    }

    if (readable_frame_count == 0) {
        throw VideoInferError("No readable video frames", true);
    }
    if (processed_frame_count == 0) {
        throw VideoInferError("No sampled video frames processed", true);
    }

    fillVideoSummary(
        result,
        stride_state,
        source_fps,
        config.video_detect_fps,
        stride_mode,
        high_res_worker != nullptr,
        base_frame_stride,
        min_stride_used,
        max_stride_used,
        readable_frame_count,
        processed_frame_count,
        source_frame_count,
        async_infer_request_count,
        async_correction_count,
        forced_detection_count,
        scheduled_detection_count,
        skipped_detection_count,
        static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH)),
        static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT))
    );
    result.total_elapsed_ms = elapsedMs(total_start);
    result.queue_length = high_res_worker != nullptr ? high_res_worker->pendingCount() : 0;
    result.max_queue_length = max_queue_length;
    result.dropped_frame_count = dropped_frame_count;
    result.high_low_diagnostics = tracker.diagnostics();
    result.high_low_diagnostics.flow_track_count =
        runtime_diagnostics.flow_track_count;
    result.high_low_diagnostics.flow_rejected_track_count =
        runtime_diagnostics.flow_rejected_track_count;
    result.high_low_diagnostics.flow_low_point_rejection_count =
        runtime_diagnostics.flow_low_point_rejection_count;
    result.high_low_diagnostics.flow_invalid_ratio_rejection_count =
        runtime_diagnostics.flow_invalid_ratio_rejection_count;
    result.high_low_diagnostics.flow_forward_backward_rejection_count =
        runtime_diagnostics.flow_forward_backward_rejection_count;
    result.high_low_diagnostics.flow_motion_dispersion_rejection_count =
        runtime_diagnostics.flow_motion_dispersion_rejection_count;
    result.high_low_diagnostics.flow_motion_jump_rejection_count =
        runtime_diagnostics.flow_motion_jump_rejection_count;
    result.high_low_diagnostics.flow_boundary_rejection_count =
        runtime_diagnostics.flow_boundary_rejection_count;
    result.high_low_diagnostics.urgent_low_res_detection_count =
        runtime_diagnostics.urgent_low_res_detection_count;
    result.high_low_diagnostics.urgent_high_res_detection_count =
        runtime_diagnostics.urgent_high_res_detection_count;
    result.high_low_diagnostics.urgent_flow_quality_count =
        runtime_diagnostics.urgent_flow_quality_count;
    result.high_low_diagnostics.urgent_track_change_count =
        runtime_diagnostics.urgent_track_change_count;
    result.high_low_diagnostics.urgent_duplicate_count =
        runtime_diagnostics.urgent_duplicate_count;
    result.high_low_diagnostics.urgent_flow_age_count =
        runtime_diagnostics.urgent_flow_age_count;
    result.high_low_diagnostics.urgent_geometry_count =
        runtime_diagnostics.urgent_geometry_count;
    result.high_low_diagnostics.urgent_class_conflict_count =
        runtime_diagnostics.urgent_class_conflict_count;
    const ProcessUsageSnapshot usage_end = captureProcessUsage();
    finalizeVideoPerformanceMetrics(
        result,
        result.frame_count,
        usage_start,
        usage_end
    );

    return result;
}

}  // namespace yolo
