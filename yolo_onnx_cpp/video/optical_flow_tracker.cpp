#include "optical_flow_tracker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

namespace yolo {
namespace {

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

}  // namespace

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

}  // namespace yolo
