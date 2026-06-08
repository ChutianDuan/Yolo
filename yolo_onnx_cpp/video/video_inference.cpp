#include "video_inference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <future>
#include <unordered_map>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/videoio.hpp>

#include "image/image_processing.h"
#include "tracking/byte_tracker.h"

namespace yolo {

VideoInferError::VideoInferError(std::string message, bool bad_request)
    : std::runtime_error(std::move(message)),
      bad_request_(bad_request) {}

bool VideoInferError::badRequest() const {
    return bad_request_;
}

namespace {

struct WeakTrackQuality {
    size_t previous_track_count = 0;
    size_t sampled_point_count = 0;
    size_t valid_point_count = 0;
    size_t low_point_track_count = 0;
    size_t tracked_track_count = 0;
    double valid_point_ratio = 0.0;
    double tracked_track_ratio = 0.0;
    double median_flow_ratio = 0.0;
    double median_forward_backward_error = 0.0;
    float median_dx = 0.0F;
    float median_dy = 0.0F;
    double mean_frame_diff = 0.0;
    float min_score = 1.0F;
    bool has_flow = false;
};

struct WeakTrackResult {
    std::vector<TrackedDetection> tracks;
    WeakTrackQuality quality;
};

struct TrackChangeQuality {
    size_t previous_track_count = 0;
    size_t current_track_count = 0;
    size_t new_track_count = 0;
    size_t lost_track_count = 0;
    size_t velocity_jump_count = 0;
    size_t scale_jump_count = 0;
};

struct DynamicStrideState {
    int base_stride = 1;
    int current_stride = 1;
    int urgent_stride = 1;
    int complex_stride = 1;
    int stable_stride = 1;
    int stable_frame_count = 0;
    int complex_frame_count = 0;
    int64_t last_detection_frame_index = -1;
};

struct FrameMotion {
    int64_t frame_index = 0;
    float dx = 0.0F;
    float dy = 0.0F;
    bool valid = false;
};

struct AsyncInferResult {
    int64_t frame_index = 0;
    InferResult result;
    std::string error_message;
    bool bad_request = false;
    bool ok = false;
};

struct AsyncInferTask {
    int64_t frame_index = 0;
    std::future<AsyncInferResult> future;
};

double elapsedMs(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

std::unordered_map<int, TrackedDetection> tracksById(
    const std::vector<TrackedDetection>& tracks
) {
    std::unordered_map<int, TrackedDetection> result;
    result.reserve(tracks.size());
    for (const auto& track : tracks) {
        result.emplace(track.track_id, track);
    }
    return result;
}

float boxWidth(const Detection& detection) {
    return std::max(0.0F, detection.x2 - detection.x1);
}

float boxHeight(const Detection& detection) {
    return std::max(0.0F, detection.y2 - detection.y1);
}

float boxArea(const Detection& detection) {
    return boxWidth(detection) * boxHeight(detection);
}

float boxDiagonal(const Detection& detection) {
    return std::hypot(boxWidth(detection), boxHeight(detection));
}

cv::Point2f boxCenter(const Detection& detection) {
    return cv::Point2f(
        (detection.x1 + detection.x2) * 0.5F,
        (detection.y1 + detection.y2) * 0.5F
    );
}

double normalizedMeanAbsDiff(const cv::Mat& previous_gray, const cv::Mat& current_gray) {
    if (previous_gray.empty()
        || current_gray.empty()
        || previous_gray.size() != current_gray.size()) {
        return 0.0;
    }

    cv::Mat diff;
    cv::absdiff(previous_gray, current_gray, diff);
    return cv::mean(diff)[0] / 255.0;
}

FrameMotion emptyFrameMotion(int64_t frame_index) {
    FrameMotion motion;
    motion.frame_index = frame_index;
    return motion;
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

Detection interpolateDetection(
    const Detection& previous,
    const Detection& current,
    double alpha
) {
    auto lerp = [alpha](float a, float b) {
        return static_cast<float>(static_cast<double>(a)
            + (static_cast<double>(b) - static_cast<double>(a)) * alpha);
    };

    Detection detection;
    detection.class_id = previous.class_id;
    detection.score = lerp(previous.score, current.score);
    detection.x1 = lerp(previous.x1, current.x1);
    detection.y1 = lerp(previous.y1, current.y1);
    detection.x2 = lerp(previous.x2, current.x2);
    detection.y2 = lerp(previous.y2, current.y2);
    return detection;
}

std::vector<TrackedDetection> interpolatedTracks(
    const VideoFrameTracks& previous,
    const VideoFrameTracks& current,
    const VideoFrameTracks& target
) {
    const int64_t span = current.frame_index - previous.frame_index;
    if (span <= 1 || previous.tracks.empty()) {
        return {};
    }

    const double alpha = static_cast<double>(target.frame_index - previous.frame_index)
        / static_cast<double>(span);
    const auto current_by_id = tracksById(current.tracks);

    std::vector<TrackedDetection> tracks;
    tracks.reserve(previous.tracks.size());
    for (const auto& previous_track : previous.tracks) {
        auto matched = current_by_id.find(previous_track.track_id);
        if (matched == current_by_id.end()) {
            tracks.push_back(previous_track);
            continue;
        }

        tracks.push_back(TrackedDetection{
            previous_track.track_id,
            interpolateDetection(
                previous_track.detection,
                matched->second.detection,
                alpha
            )
        });
    }

    std::sort(
        tracks.begin(),
        tracks.end(),
        [](const TrackedDetection& a, const TrackedDetection& b) {
            return a.track_id < b.track_id;
        }
    );
    return tracks;
}

bool isTrackInterpolationAnchor(const VideoFrameTracks& frame) {
    return frame.tracks_source == "detected" || frame.tracks_source == "async_corrected";
}

void fillInterpolatedFrameTracks(std::vector<VideoFrameTracks>& frames) {
    size_t previous_detection_index = frames.size();

    for (size_t i = 0; i < frames.size(); ++i) {
        if (!isTrackInterpolationAnchor(frames[i])) {
            continue;
        }

        if (previous_detection_index != frames.size()) {
            for (size_t j = previous_detection_index + 1; j < i; ++j) {
                if (!frames[j].tracks.empty()) {
                    continue;
                }

                frames[j].tracks = interpolatedTracks(
                    frames[previous_detection_index],
                    frames[i],
                    frames[j]
                );
                if (!frames[j].tracks.empty()) {
                    frames[j].tracks_source = "interpolated";
                }
            }
        }

        previous_detection_index = i;
    }

    if (previous_detection_index == frames.size()) {
        return;
    }

    for (size_t i = previous_detection_index + 1; i < frames.size(); ++i) {
        if (!frames[i].tracks.empty()) {
            continue;
        }

        frames[i].tracks = frames[previous_detection_index].tracks;
        if (!frames[i].tracks.empty()) {
            frames[i].tracks_source = "interpolated";
        }
    }
}

cv::Rect detectionRoi(const Detection& detection, int image_width, int image_height) {
    const int x1 = std::max(0, static_cast<int>(std::floor(detection.x1)));
    const int y1 = std::max(0, static_cast<int>(std::floor(detection.y1)));
    const int x2 = std::min(image_width, static_cast<int>(std::ceil(detection.x2)));
    const int y2 = std::min(image_height, static_cast<int>(std::ceil(detection.y2)));
    if (x2 <= x1 || y2 <= y1) {
        return {};
    }
    return cv::Rect(x1, y1, x2 - x1, y2 - y1);
}

float medianValue(std::vector<float>& values) {
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    return values[middle];
}

WeakTrackResult weakTrackWithOpticalFlow(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    const std::vector<TrackedDetection>& previous_tracks,
    int image_width,
    int image_height
) {
    WeakTrackResult result;
    result.quality.previous_track_count = previous_tracks.size();
    result.quality.mean_frame_diff = normalizedMeanAbsDiff(previous_gray, current_gray);

    if (previous_gray.empty() || current_gray.empty() || previous_tracks.empty()) {
        return result;
    }

    constexpr int kMaxCornersPerTrack = 16;
    constexpr int kMinTrackedPoints = 3;
    constexpr float kMaxMotionRatio = 0.25F;
    constexpr float kMaxForwardBackwardError = 4.0F;
    const float max_dx = static_cast<float>(image_width) * kMaxMotionRatio;
    const float max_dy = static_cast<float>(image_height) * kMaxMotionRatio;
    const double image_diagonal = std::hypot(
        static_cast<double>(image_width),
        static_cast<double>(image_height)
    );

    std::vector<cv::Point2f> previous_points;
    std::vector<size_t> point_track_indices;
    std::vector<size_t> sampled_points_by_track(previous_tracks.size(), 0);

    for (size_t i = 0; i < previous_tracks.size(); ++i) {
        const cv::Rect roi = detectionRoi(
            previous_tracks[i].detection,
            image_width,
            image_height
        );
        if (roi.width < 4 || roi.height < 4) {
            continue;
        }

        std::vector<cv::Point2f> local_points;
        cv::goodFeaturesToTrack(
            previous_gray(roi),
            local_points,
            kMaxCornersPerTrack,
            0.01,
            4.0
        );

        for (const auto& local_point : local_points) {
            previous_points.push_back(cv::Point2f(
                local_point.x + static_cast<float>(roi.x),
                local_point.y + static_cast<float>(roi.y)
            ));
            point_track_indices.push_back(i);
            ++sampled_points_by_track[i];
        }
    }

    result.quality.sampled_point_count = previous_points.size();
    if (previous_points.empty()) {
        return result;
    }

    std::vector<cv::Point2f> current_points;
    std::vector<unsigned char> status;
    std::vector<float> error;
    cv::calcOpticalFlowPyrLK(
        previous_gray,
        current_gray,
        previous_points,
        current_points,
        status,
        error,
        cv::Size(21, 21),
        3
    );

    std::vector<cv::Point2f> backward_points;
    std::vector<unsigned char> backward_status;
    std::vector<float> backward_error;
    cv::calcOpticalFlowPyrLK(
        current_gray,
        previous_gray,
        current_points,
        backward_points,
        backward_status,
        backward_error,
        cv::Size(21, 21),
        3
    );

    std::vector<std::vector<float>> dx_by_track(previous_tracks.size());
    std::vector<std::vector<float>> dy_by_track(previous_tracks.size());
    std::vector<float> flow_magnitudes;
    std::vector<float> forward_backward_errors;
    std::vector<float> valid_dx;
    std::vector<float> valid_dy;

    for (size_t i = 0; i < current_points.size(); ++i) {
        if (status[i] == 0) {
            continue;
        }
        if (i >= backward_status.size() || backward_status[i] == 0) {
            continue;
        }
        if (current_points[i].x < 0.0F
            || current_points[i].y < 0.0F
            || current_points[i].x >= static_cast<float>(image_width)
            || current_points[i].y >= static_cast<float>(image_height)) {
            continue;
        }

        const float forward_backward_error = std::hypot(
            backward_points[i].x - previous_points[i].x,
            backward_points[i].y - previous_points[i].y
        );
        if (forward_backward_error > kMaxForwardBackwardError) {
            continue;
        }

        const float dx = current_points[i].x - previous_points[i].x;
        const float dy = current_points[i].y - previous_points[i].y;
        if (std::fabs(dx) > max_dx || std::fabs(dy) > max_dy) {
            continue;
        }

        const size_t track_index = point_track_indices[i];
        dx_by_track[track_index].push_back(dx);
        dy_by_track[track_index].push_back(dy);
        flow_magnitudes.push_back(std::hypot(dx, dy));
        forward_backward_errors.push_back(forward_backward_error);
        valid_dx.push_back(dx);
        valid_dy.push_back(dy);
    }

    std::vector<TrackedDetection> tracked_detections;
    tracked_detections.reserve(previous_tracks.size());

    for (size_t i = 0; i < previous_tracks.size(); ++i) {
        if (dx_by_track[i].size() < kMinTrackedPoints
            || dy_by_track[i].size() < kMinTrackedPoints) {
            if (sampled_points_by_track[i] > 0) {
                ++result.quality.low_point_track_count;
            }
            continue;
        }

        const float dx = medianValue(dx_by_track[i]);
        const float dy = medianValue(dy_by_track[i]);
        Detection detection = previous_tracks[i].detection;
        detection.x1 += dx;
        detection.x2 += dx;
        detection.y1 += dy;
        detection.y2 += dy;
        detection.score *= 0.98F;
        clipDetection(detection, image_width, image_height);
        if (!hasValidBox(detection)) {
            continue;
        }

        tracked_detections.push_back(TrackedDetection{
            previous_tracks[i].track_id,
            detection
        });
        result.quality.min_score = std::min(result.quality.min_score, detection.score);
    }

    result.tracks = std::move(tracked_detections);
    result.quality.valid_point_count = flow_magnitudes.size();
    result.quality.tracked_track_count = result.tracks.size();
    result.quality.has_flow = result.quality.sampled_point_count > 0;
    result.quality.valid_point_ratio = result.quality.sampled_point_count > 0
        ? static_cast<double>(result.quality.valid_point_count)
            / static_cast<double>(result.quality.sampled_point_count)
        : 0.0;
    result.quality.tracked_track_ratio = result.quality.previous_track_count > 0
        ? static_cast<double>(result.quality.tracked_track_count)
            / static_cast<double>(result.quality.previous_track_count)
        : 0.0;
    if (!flow_magnitudes.empty() && image_diagonal > 0.0) {
        result.quality.median_flow_ratio =
            static_cast<double>(medianValue(flow_magnitudes)) / image_diagonal;
    }
    if (!forward_backward_errors.empty()) {
        result.quality.median_forward_backward_error =
            static_cast<double>(medianValue(forward_backward_errors));
    }
    if (!valid_dx.empty()) {
        result.quality.median_dx = medianValue(valid_dx);
    }
    if (!valid_dy.empty()) {
        result.quality.median_dy = medianValue(valid_dy);
    }
    if (result.tracks.empty()) {
        result.quality.min_score = 0.0F;
    }

    return result;
}

FrameMotion frameMotionFromWeakQuality(int64_t frame_index, const WeakTrackQuality& quality) {
    FrameMotion motion = emptyFrameMotion(frame_index);
    if (quality.valid_point_count == 0) {
        return motion;
    }

    motion.dx = quality.median_dx;
    motion.dy = quality.median_dy;
    motion.valid = true;
    return motion;
}

FrameMotion estimateGlobalFrameMotion(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    int64_t frame_index
) {
    FrameMotion motion = emptyFrameMotion(frame_index);
    if (previous_gray.empty()
        || current_gray.empty()
        || previous_gray.size() != current_gray.size()) {
        return motion;
    }

    constexpr int kMaxCorners = 200;
    constexpr float kMaxForwardBackwardError = 4.0F;
    std::vector<cv::Point2f> previous_points;
    cv::goodFeaturesToTrack(previous_gray, previous_points, kMaxCorners, 0.01, 8.0);
    if (previous_points.empty()) {
        return motion;
    }

    std::vector<cv::Point2f> current_points;
    std::vector<unsigned char> status;
    std::vector<float> error;
    cv::calcOpticalFlowPyrLK(
        previous_gray,
        current_gray,
        previous_points,
        current_points,
        status,
        error,
        cv::Size(21, 21),
        3
    );

    std::vector<cv::Point2f> backward_points;
    std::vector<unsigned char> backward_status;
    std::vector<float> backward_error;
    cv::calcOpticalFlowPyrLK(
        current_gray,
        previous_gray,
        current_points,
        backward_points,
        backward_status,
        backward_error,
        cv::Size(21, 21),
        3
    );

    std::vector<float> valid_dx;
    std::vector<float> valid_dy;
    for (size_t i = 0; i < current_points.size(); ++i) {
        if (status[i] == 0 || i >= backward_status.size() || backward_status[i] == 0) {
            continue;
        }

        const float forward_backward_error = std::hypot(
            backward_points[i].x - previous_points[i].x,
            backward_points[i].y - previous_points[i].y
        );
        if (forward_backward_error > kMaxForwardBackwardError) {
            continue;
        }

        valid_dx.push_back(current_points[i].x - previous_points[i].x);
        valid_dy.push_back(current_points[i].y - previous_points[i].y);
    }

    if (valid_dx.empty() || valid_dy.empty()) {
        return motion;
    }

    motion.dx = medianValue(valid_dx);
    motion.dy = medianValue(valid_dy);
    motion.valid = true;
    return motion;
}

FrameMotion frameMotionForCurrentFrame(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    int64_t frame_index,
    const WeakTrackQuality& quality
) {
    FrameMotion motion = frameMotionFromWeakQuality(frame_index, quality);
    if (motion.valid) {
        return motion;
    }
    return estimateGlobalFrameMotion(previous_gray, current_gray, frame_index);
}

cv::Point2f accumulatedMotion(
    int64_t from_frame_index,
    int64_t to_frame_index,
    const std::vector<FrameMotion>& frame_motions
) {
    cv::Point2f delta(0.0F, 0.0F);
    if (to_frame_index <= from_frame_index) {
        return delta;
    }

    for (const auto& motion : frame_motions) {
        if (!motion.valid
            || motion.frame_index <= from_frame_index
            || motion.frame_index > to_frame_index) {
            continue;
        }
        delta.x += motion.dx;
        delta.y += motion.dy;
    }
    return delta;
}

std::vector<Detection> motionCompensatedDetections(
    const std::vector<Detection>& detections,
    int64_t detection_frame_index,
    int64_t current_frame_index,
    const std::vector<FrameMotion>& frame_motions,
    int image_width,
    int image_height
) {
    const cv::Point2f delta = accumulatedMotion(
        detection_frame_index,
        current_frame_index,
        frame_motions
    );

    std::vector<Detection> compensated;
    compensated.reserve(detections.size());
    for (auto detection : detections) {
        detection.x1 += delta.x;
        detection.x2 += delta.x;
        detection.y1 += delta.y;
        detection.y2 += delta.y;
        clipDetection(detection, image_width, image_height);
        if (hasValidBox(detection)) {
            compensated.push_back(detection);
        }
    }
    return compensated;
}

bool isSevereWeakQualityDrop(const WeakTrackQuality& quality) {
    if (quality.previous_track_count == 0) {
        return false;
    }
    if (!quality.has_flow || quality.tracked_track_count == 0) {
        return true;
    }

    return quality.valid_point_ratio < 0.35
        || quality.tracked_track_ratio < 0.55
        || quality.low_point_track_count * 2 > quality.previous_track_count
        || quality.median_forward_backward_error > 3.0
        || quality.mean_frame_diff > 0.16
        || quality.median_flow_ratio > 0.08
        || quality.min_score < 0.18F;
}

bool isComplexWeakQuality(const WeakTrackQuality& quality) {
    if (quality.previous_track_count == 0) {
        return false;
    }
    if (isSevereWeakQualityDrop(quality)) {
        return true;
    }

    return quality.valid_point_ratio < 0.55
        || quality.tracked_track_ratio < 0.80
        || quality.low_point_track_count > 0
        || quality.median_forward_backward_error > 2.0
        || quality.mean_frame_diff > 0.08
        || quality.median_flow_ratio > 0.04
        || quality.min_score < 0.30F;
}

bool isStableWeakQuality(const WeakTrackQuality& quality) {
    return quality.previous_track_count > 0
        && quality.tracked_track_ratio >= 0.90
        && quality.valid_point_ratio >= 0.70
        && quality.low_point_track_count == 0
        && quality.median_forward_backward_error <= 1.2
        && quality.mean_frame_diff <= 0.04
        && quality.median_flow_ratio <= 0.02
        && quality.min_score >= 0.35F;
}

TrackChangeQuality trackChangeQuality(
    const std::vector<TrackedDetection>& previous_tracks,
    const std::vector<TrackedDetection>& current_tracks,
    const std::unordered_map<int, cv::Point2f>& previous_velocities
) {
    TrackChangeQuality quality;
    quality.previous_track_count = previous_tracks.size();
    quality.current_track_count = current_tracks.size();

    const auto previous_by_id = tracksById(previous_tracks);
    const auto current_by_id = tracksById(current_tracks);

    for (const auto& current_track : current_tracks) {
        auto previous = previous_by_id.find(current_track.track_id);
        if (previous == previous_by_id.end()) {
            ++quality.new_track_count;
            continue;
        }

        const cv::Point2f current_motion =
            boxCenter(current_track.detection) - boxCenter(previous->second.detection);
        auto previous_velocity = previous_velocities.find(current_track.track_id);
        if (previous_velocity != previous_velocities.end()) {
            const float velocity_delta = std::hypot(
                current_motion.x - previous_velocity->second.x,
                current_motion.y - previous_velocity->second.y
            );
            const float previous_diag = std::max(10.0F, boxDiagonal(previous->second.detection));
            const float motion = std::hypot(current_motion.x, current_motion.y);
            if (velocity_delta > std::max(12.0F, previous_diag * 0.35F)
                && motion > std::max(6.0F, previous_diag * 0.08F)) {
                ++quality.velocity_jump_count;
            }
        }

        const float previous_area = boxArea(previous->second.detection);
        const float current_area = boxArea(current_track.detection);
        if (previous_area > 1.0F && current_area > 1.0F) {
            const float scale_ratio = current_area / previous_area;
            if (scale_ratio < 0.60F || scale_ratio > 1.60F) {
                ++quality.scale_jump_count;
            }
        }
    }

    for (const auto& previous_track : previous_tracks) {
        if (current_by_id.find(previous_track.track_id) == current_by_id.end()) {
            ++quality.lost_track_count;
        }
    }

    return quality;
}

void updateTrackVelocities(
    const std::vector<TrackedDetection>& previous_tracks,
    const std::vector<TrackedDetection>& current_tracks,
    std::unordered_map<int, cv::Point2f>& track_velocities
) {
    std::unordered_map<int, cv::Point2f> next_velocities;
    const auto previous_by_id = tracksById(previous_tracks);

    for (const auto& current_track : current_tracks) {
        auto previous = previous_by_id.find(current_track.track_id);
        if (previous == previous_by_id.end()) {
            continue;
        }

        next_velocities.emplace(
            current_track.track_id,
            boxCenter(current_track.detection) - boxCenter(previous->second.detection)
        );
    }

    track_velocities = std::move(next_velocities);
}

bool isComplexTrackChange(const TrackChangeQuality& quality) {
    if (quality.previous_track_count == 0) {
        return quality.current_track_count >= 3;
    }

    const size_t target_change_threshold =
        std::max<size_t>(2, quality.previous_track_count / 3 + 1);
    return quality.new_track_count >= target_change_threshold
        || quality.lost_track_count >= target_change_threshold
        || quality.velocity_jump_count > 0
        || quality.scale_jump_count > 0;
}

void markComplexStride(DynamicStrideState& state, bool urgent) {
    if (state.base_stride <= 1) {
        return;
    }

    state.stable_frame_count = 0;
    ++state.complex_frame_count;
    state.current_stride = urgent ? state.urgent_stride : state.complex_stride;
}

void applyWeakQualityToStride(DynamicStrideState& state, const WeakTrackQuality& quality) {
    if (state.base_stride <= 1) {
        return;
    }

    if (isComplexWeakQuality(quality)) {
        markComplexStride(state, isSevereWeakQualityDrop(quality));
        return;
    }

    if (isStableWeakQuality(quality)) {
        ++state.stable_frame_count;
        state.complex_frame_count = 0;
        state.current_stride = state.stable_frame_count >= 3
            ? state.stable_stride
            : state.base_stride;
        return;
    }

    state.stable_frame_count = 0;
    state.complex_frame_count = 0;
    state.current_stride = state.base_stride;
}

void applyTrackChangeToStride(
    DynamicStrideState& state,
    const TrackChangeQuality& quality
) {
    if (isComplexTrackChange(quality)) {
        markComplexStride(state, true);
    }
}

std::string invalidVideoFrameMessage(const AppConfig& config) {
    if (config.use_letterbox) {
        return "Failed to preprocess video frame";
    }

    return "Failed to preprocess video frame or frame size is not "
        + std::to_string(config.input_width) + "x" + std::to_string(config.input_height);
}

std::future<AsyncInferResult> launchAsyncInfer(
    const std::shared_ptr<YoloEngine>& engine,
    AppConfig config,
    cv::Mat frame,
    int64_t frame_index
) {
    return std::async(
        std::launch::async,
        [engine, config = std::move(config), frame = std::move(frame), frame_index]() mutable {
            AsyncInferResult async_result;
            async_result.frame_index = frame_index;

            try {
                const auto input = preprocessImageMat(frame, config);
                if (!input.has_value()) {
                    async_result.bad_request = true;
                    async_result.error_message = invalidVideoFrameMessage(config);
                    return async_result;
                }

                async_result.result = engine->infer(input.value());
                async_result.ok = true;
                return async_result;
            } catch (const std::exception& e) {
                async_result.error_message = e.what();
                return async_result;
            }
        }
    );
}

double finiteOrZero(double value) {
    return std::isfinite(value) ? value : 0.0;
}

double frameTimestampMs(
    int64_t frame_index,
    double source_fps,
    double capture_timestamp_ms
) {
    if (source_fps > 0.0) {
        return static_cast<double>(frame_index) * 1000.0 / source_fps;
    }
    return capture_timestamp_ms;
}

int videoFrameStride(double source_fps, float target_fps) {
    if (source_fps <= 0.0 || target_fps <= 0.0F) {
        return 1;
    }
    if (source_fps <= static_cast<double>(target_fps)) {
        return 1;
    }
    return std::max(
        1,
        static_cast<int>(std::ceil(source_fps / static_cast<double>(target_fps)))
    );
}

DynamicStrideState makeDynamicStrideState(int base_stride) {
    DynamicStrideState state;
    state.base_stride = std::max(1, base_stride);
    state.current_stride = state.base_stride;
    state.urgent_stride = std::max(1, std::min(state.base_stride, 2));
    state.complex_stride = std::max(state.urgent_stride, std::min(state.base_stride, 3));

    if (state.base_stride <= 1) {
        state.stable_stride = 1;
        return state;
    }

    const int stable_extra = state.base_stride >= 6 ? 4 : 2;
    const int stable_cap = std::max(state.base_stride, 10);
    state.stable_stride = std::min(stable_cap, state.base_stride + stable_extra);
    return state;
}

bool shouldRunScheduledDetection(
    const DynamicStrideState& stride_state,
    int64_t frame_index,
    int64_t scheduled_request_count
) {
    if (scheduled_request_count == 0 || stride_state.base_stride <= 1) {
        return true;
    }
    if (stride_state.last_detection_frame_index < 0) {
        return true;
    }
    return frame_index - stride_state.last_detection_frame_index
        >= stride_state.current_stride;
}

void fillVideoSummary(
    VideoInferResult& result,
    const DynamicStrideState& stride_state,
    double source_fps,
    float target_detect_fps,
    const std::string& stride_mode,
    bool onnx_async,
    int base_frame_stride,
    int min_stride_used,
    int max_stride_used,
    int64_t readable_frame_count,
    int64_t processed_frame_count,
    int64_t source_frame_count,
    int64_t async_infer_request_count,
    int64_t async_correction_count,
    int64_t forced_detection_count,
    int64_t scheduled_detection_count,
    int64_t skipped_detection_count,
    int width,
    int height
) {
    auto interpolation_start = std::chrono::steady_clock::now();
    fillInterpolatedFrameTracks(result.frames);
    result.tracking_postprocess_ms += elapsedMs(interpolation_start);

    for (const auto& frame_result : result.frames) {
        if (frame_result.is_detection_frame) {
            ++result.detected_frame_count;
        }

        if (frame_result.tracks_source == "async_corrected") {
            ++result.async_corrected_frame_count;
        } else if (frame_result.tracks_source == "weak_tracked") {
            ++result.weak_tracked_frame_count;
        } else if (frame_result.tracks_source == "interpolated") {
            ++result.interpolated_frame_count;
        } else if (frame_result.tracks_source == "empty") {
            ++result.empty_frame_count;
        }
    }

    result.fps = source_fps;
    result.source_fps = source_fps;
    result.target_detect_fps = target_detect_fps;
    result.frame_stride = processed_frame_count > 0
        ? static_cast<double>(readable_frame_count)
            / static_cast<double>(processed_frame_count)
        : 0.0;
    result.effective_detect_fps = source_fps > 0.0 && readable_frame_count > 0
        ? source_fps * static_cast<double>(processed_frame_count)
            / static_cast<double>(readable_frame_count)
        : 0.0;
    result.stride_mode = stride_mode;
    result.onnx_async = onnx_async;
    result.base_frame_stride = base_frame_stride;
    result.min_frame_stride_used = min_stride_used;
    result.max_frame_stride_used = max_stride_used;
    result.final_frame_stride = stride_state.current_stride;
    result.width = width;
    result.height = height;
    result.frame_count = static_cast<int64_t>(result.frames.size());
    result.source_frame_count = source_frame_count;
    result.processed_frame_count = processed_frame_count;
    result.display_frame_count = result.frame_count;
    result.async_infer_request_count = async_infer_request_count;
    result.async_correction_count = async_correction_count;
    result.forced_detection_count = forced_detection_count;
    result.scheduled_detection_count = scheduled_detection_count;
    result.skipped_detection_count = skipped_detection_count;
}

}  // namespace

VideoInferResult inferVideoFile(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    const std::filesystem::path& video_path
) {
    const auto total_start = std::chrono::steady_clock::now();
    cv::VideoCapture capture(video_path.string());
    if (!capture.isOpened()) {
        throw VideoInferError("Failed to open video", true);
    }
    // 读取帧数信息，并初始化默认抽帧间隔 stride_state
    const double source_fps = finiteOrZero(capture.get(cv::CAP_PROP_FPS));
    const int base_frame_stride = videoFrameStride(source_fps, config.video_detect_fps);
    DynamicStrideState stride_state = makeDynamicStrideState(base_frame_stride);
    const bool async_enabled = config.video_onnx_async && base_frame_stride > 1;
    const bool dynamic_stride_enabled = base_frame_stride > 1
        && config.video_stride_mode == "dynamic";
    const std::string stride_mode = base_frame_stride <= 1
        ? "full_onnx"
        : std::string(async_enabled ? "async_" : "sync_")
            + (dynamic_stride_enabled ? "dynamic" : "fixed");

    VideoInferResult result;
    ByteTracker tracker;
    std::deque<AsyncInferTask> pending_infers;
    std::vector<FrameMotion> frame_motions;
    cv::Mat frame;
    cv::Mat previous_gray;
    std::vector<TrackedDetection> previous_frame_tracks;
    std::unordered_map<int, cv::Point2f> track_velocities;
    int64_t frame_index = 0;
    int64_t source_frame_count = 0;
    int64_t readable_frame_count = 0;
    int64_t processed_frame_count = 0;
    int64_t async_infer_request_count = 0;
    int64_t forced_detection_count = 0;
    int64_t scheduled_detection_count = 0;
    int64_t skipped_detection_count = 0;
    int64_t async_correction_count = 0;
    int image_width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    int image_height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    int min_stride_used = stride_state.current_stride;
    int max_stride_used = stride_state.current_stride;
    auto rememberStride = [&stride_state, &min_stride_used, &max_stride_used]() {
        min_stride_used = std::min(min_stride_used, stride_state.current_stride);
        max_stride_used = std::max(max_stride_used, stride_state.current_stride);
    };

    std::string async_error_message;
    bool async_error_bad_request = false;

    auto applyAsyncInferResult = [
        &tracker,
        &result,
        &frame_motions,
        &previous_frame_tracks,
        &track_velocities,
        &stride_state,
        &processed_frame_count,
        &async_correction_count,
        &rememberStride,
        dynamic_stride_enabled,
        &async_error_message,
        &async_error_bad_request
    ](AsyncInferResult&& async_result,
      int64_t current_frame_index,
      int current_image_width,
      int current_image_height,
      VideoFrameTracks* target_frame) {
        if (!async_result.ok) {
            async_error_message = async_result.error_message;
            async_error_bad_request = async_result.bad_request;
            return false;
        }

        if (processed_frame_count == 0) {
            result.output_shapes = async_result.result.output_shapes;
        }
        result.onnx_inference_ms += async_result.result.onnx_inference_ms;
        result.onnx_postprocess_ms += async_result.result.postprocess_ms;

        auto postprocess_start = std::chrono::steady_clock::now();
        const auto corrected_detections = motionCompensatedDetections(
            async_result.result.detections,
            async_result.frame_index,
            current_frame_index,
            frame_motions,
            current_image_width,
            current_image_height
        );

        const auto tracks_before_detection = previous_frame_tracks;
        auto corrected_tracks = tracker.update(corrected_detections);

        const TrackChangeQuality track_change = trackChangeQuality(
            tracks_before_detection,
            corrected_tracks,
            track_velocities
        );
        if (dynamic_stride_enabled && processed_frame_count > 0) {
            applyTrackChangeToStride(stride_state, track_change);
        }
        updateTrackVelocities(
            tracks_before_detection,
            corrected_tracks,
            track_velocities
        );
        rememberStride();

        previous_frame_tracks = corrected_tracks;
        if (target_frame != nullptr) {
            target_frame->tracks = std::move(corrected_tracks);
            target_frame->tracks_source = "async_corrected";
            target_frame->corrected_from_frame_index = async_result.frame_index;
            target_frame->correction_latency_frames =
                current_frame_index - async_result.frame_index;
        }

        ++processed_frame_count;
        ++async_correction_count;
        result.tracking_postprocess_ms += elapsedMs(postprocess_start);
        return true;
    };

    auto processReadyInferences = [
        &pending_infers,
        &applyAsyncInferResult
    ](bool wait_for_all,
      int64_t current_frame_index,
      int current_image_width,
      int current_image_height,
      VideoFrameTracks* target_frame) {
        for (auto it = pending_infers.begin(); it != pending_infers.end();) {
            if (!wait_for_all
                && it->future.wait_for(std::chrono::seconds(0))
                    != std::future_status::ready) {
                ++it;
                continue;
            }

            AsyncInferResult async_result = it->future.get();
            it = pending_infers.erase(it);
            if (!applyAsyncInferResult(
                    std::move(async_result),
                    current_frame_index,
                    current_image_width,
                    current_image_height,
                    target_frame)) {
                return false;
            }
        }
        return true;
    };

    auto scheduleAsyncDetection = [
        &engine,
        &config,
        &pending_infers,
        &stride_state,
        &async_infer_request_count,
        &forced_detection_count,
        &scheduled_detection_count,
        &skipped_detection_count,
        &rememberStride
    ](const cv::Mat& source_frame,
      int64_t source_frame_index,
      bool forced,
      VideoFrameTracks& frame_result) {
        constexpr size_t kMaxPendingAsyncInferences = 1;
        if (pending_infers.size() >= kMaxPendingAsyncInferences) {
            ++skipped_detection_count;
            return;
        }

        cv::Mat async_frame = source_frame.clone();
        pending_infers.push_back(AsyncInferTask{
            source_frame_index,
            launchAsyncInfer(engine, config, std::move(async_frame), source_frame_index)
        });
        stride_state.last_detection_frame_index = source_frame_index;
        frame_result.is_detection_frame = true;
        ++async_infer_request_count;
        if (forced) {
            ++forced_detection_count;
        } else {
            ++scheduled_detection_count;
        }
        rememberStride();
    };

    auto runSyncDetection = [
        &engine,
        &config,
        &tracker,
        &result,
        &previous_frame_tracks,
        &track_velocities,
        &stride_state,
        &processed_frame_count,
        &forced_detection_count,
        &scheduled_detection_count,
        &rememberStride
    ](const cv::Mat& source_frame,
      int64_t source_frame_index,
      bool forced,
      VideoFrameTracks& frame_result) {
        const auto input = preprocessImageMat(source_frame, config);
        if (!input.has_value()) {
            throw VideoInferError(invalidVideoFrameMessage(config), true);
        }

        const InferResult infer_result = engine->infer(input.value());
        result.onnx_inference_ms += infer_result.onnx_inference_ms;
        result.onnx_postprocess_ms += infer_result.postprocess_ms;
        if (processed_frame_count == 0) {
            result.output_shapes = infer_result.output_shapes;
        }

        auto postprocess_start = std::chrono::steady_clock::now();
        const auto tracks_before_detection = previous_frame_tracks;
        auto detected_tracks = tracker.update(infer_result.detections);
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
        rememberStride();
        result.tracking_postprocess_ms += elapsedMs(postprocess_start);
    };

    while (capture.read(frame)) {
        VideoFrameTracks frame_result;
        frame_result.frame_index = frame_index;
        frame_result.timestamp_ms = frameTimestampMs(
            frame_index,
            source_fps,
            finiteOrZero(capture.get(cv::CAP_PROP_POS_MSEC))
        );
        ++source_frame_count;
        // 当前帧如果没有图像，跳过但仍然记录一个空结果，以保持帧索引和时间戳的连续性
        if (frame.empty()) {
            result.frames.push_back(std::move(frame_result));
            ++frame_index;
            continue;
        }

        ++readable_frame_count;
        image_width = frame.cols;
        image_height = frame.rows;
        cv::Mat current_gray;
        cv::cvtColor(frame, current_gray, cv::COLOR_BGR2GRAY);

        const auto tracks_before_flow = previous_frame_tracks;
        auto flow_start = std::chrono::steady_clock::now();
        const auto weak_result = weakTrackWithOpticalFlow(
            previous_gray,
            current_gray,
            previous_frame_tracks,
            image_width,
            image_height
        );
        frame_motions.push_back(frameMotionForCurrentFrame(
            previous_gray,
            current_gray,
            frame_index,
            weak_result.quality
        ));
        result.optical_flow_ms += elapsedMs(flow_start);

        auto postprocess_start = std::chrono::steady_clock::now();
        if (!weak_result.tracks.empty()) {
            frame_result.tracks = tracker.updateTracked(weak_result.tracks);
            if (!frame_result.tracks.empty()) {
                frame_result.tracks_source = "weak_tracked";
            }
        }

        previous_frame_tracks = frame_result.tracks;
        const TrackChangeQuality flow_track_change = trackChangeQuality(
            tracks_before_flow,
            previous_frame_tracks,
            track_velocities
        );
        if (dynamic_stride_enabled) {
            applyWeakQualityToStride(stride_state, weak_result.quality);
            applyTrackChangeToStride(stride_state, flow_track_change);
        }
        updateTrackVelocities(
            tracks_before_flow,
            previous_frame_tracks,
            track_velocities
        );
        rememberStride();
        result.tracking_postprocess_ms += elapsedMs(postprocess_start);

        if (!processReadyInferences(
                false,
                frame_index,
                image_width,
                image_height,
                &frame_result)) {
            throw VideoInferError(async_error_message, async_error_bad_request);
        }

        const bool force_detection = dynamic_stride_enabled
            && isSevereWeakQualityDrop(weak_result.quality);
        const int64_t detection_request_count = async_enabled
            ? async_infer_request_count
            : processed_frame_count;
        const bool should_schedule_detection = force_detection
            || shouldRunScheduledDetection(
                stride_state,
                frame_index,
                detection_request_count
            );
        if (should_schedule_detection) {
            if (async_enabled) {
                scheduleAsyncDetection(
                    frame,
                    frame_index,
                    force_detection,
                    frame_result
                );
            } else {
                runSyncDetection(
                    frame,
                    frame_index,
                    force_detection,
                    frame_result
                );
            }
        }

        previous_gray = current_gray;
        result.frames.push_back(std::move(frame_result));
        ++frame_index;
    }

    if (!pending_infers.empty() && readable_frame_count > 0) {
        VideoFrameTracks* correction_frame = result.frames.empty()
            ? nullptr
            : &result.frames.back();
        const int64_t current_frame_index = frame_index > 0 ? frame_index - 1 : 0;
        if (!processReadyInferences(
                true,
                current_frame_index,
                image_width,
                image_height,
                correction_frame)) {
            throw VideoInferError(async_error_message, async_error_bad_request);
        }
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
        async_enabled,
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

    return result;
}

}  // namespace yolo
