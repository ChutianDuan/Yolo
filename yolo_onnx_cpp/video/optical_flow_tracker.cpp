#include "optical_flow_tracker.h"

#include <algorithm>
#include <chrono>
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

float medianValue(std::vector<float> values) {
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    return values[middle];
}

float detectionArea(const Detection& detection) {
    return std::max(0.0F, detection.x2 - detection.x1)
        * std::max(0.0F, detection.y2 - detection.y1);
}

double visibleAreaRatio(
    const Detection& detection,
    int image_width,
    int image_height
) {
    const float original_area = detectionArea(detection);
    if (original_area <= 0.0F) {
        return 0.0;
    }
    Detection clipped_detection = detection;
    clipDetection(clipped_detection, image_width, image_height);
    return static_cast<double>(detectionArea(clipped_detection))
        / static_cast<double>(original_area);
}

FrameMotion estimateGlobalFrameMotion(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    int64_t frame_index,
    const std::vector<TrackedDetection>& previous_tracks
) {
    FrameMotion motion = emptyFrameMotion(frame_index);
    if (previous_gray.empty()
        || current_gray.empty()
        || previous_gray.size() != current_gray.size()) {
        return motion;
    }

    constexpr int kMaxCorners = 200;
    constexpr int kMinBackgroundPoints = 8;
    constexpr float kMaxForwardBackwardError = 2.0F;
    std::vector<cv::Point2f> previous_points;
    cv::Mat background_mask(previous_gray.size(), CV_8UC1, cv::Scalar(255));
    for (const auto& track : previous_tracks) {
        const cv::Rect roi = detectionRoi(
            track.detection,
            previous_gray.cols,
            previous_gray.rows
        );
        if (!roi.empty()) {
            background_mask(roi).setTo(cv::Scalar(0));
        }
    }
    cv::goodFeaturesToTrack(
        previous_gray,
        previous_points,
        kMaxCorners,
        0.01,
        8.0,
        background_mask
    );
    motion.sampled_point_count = previous_points.size();
    if (previous_points.empty()) {
        return motion;
    }

    // Share the same gray pyramids across both directions; LK still computes its source derivatives.
    std::vector<cv::Mat> previous_pyramid;
    std::vector<cv::Mat> current_pyramid;
    cv::buildOpticalFlowPyramid(previous_gray, previous_pyramid, cv::Size(21, 21), 3, false);
    cv::buildOpticalFlowPyramid(current_gray, current_pyramid, cv::Size(21, 21), 3, false);

    std::vector<cv::Point2f> current_points;
    std::vector<unsigned char> status;
    std::vector<float> error;
    cv::calcOpticalFlowPyrLK(
        previous_pyramid,
        current_pyramid,
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
        current_pyramid,
        previous_pyramid,
        current_points,
        backward_points,
        backward_status,
        backward_error,
        cv::Size(21, 21),
        3
    );

    previous_pyramid.clear();
    current_pyramid.clear();

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

    motion.valid_point_count = valid_dx.size();
    if (valid_dx.size() < kMinBackgroundPoints || valid_dy.size() < kMinBackgroundPoints) {
        return motion;
    }

    const float median_dx = medianValue(valid_dx);
    const float median_dy = medianValue(valid_dy);
    std::vector<float> residuals;
    residuals.reserve(valid_dx.size());
    for (size_t i = 0; i < valid_dx.size(); ++i) {
        residuals.push_back(std::hypot(
            valid_dx[i] - median_dx,
            valid_dy[i] - median_dy
        ));
    }
    const float median_residual = medianValue(residuals);
    const float inlier_threshold = std::max(1.5F, median_residual * 3.0F);
    const size_t inlier_count = static_cast<size_t>(std::count_if(
        residuals.begin(),
        residuals.end(),
        [inlier_threshold](float residual) {
            return residual <= inlier_threshold;
        }
    ));
    const double image_diagonal = std::hypot(
        static_cast<double>(previous_gray.cols),
        static_cast<double>(previous_gray.rows)
    );
    motion.inlier_ratio = static_cast<double>(inlier_count)
        / static_cast<double>(valid_dx.size());
    motion.displacement_spread_ratio = image_diagonal > 0.0
        ? static_cast<double>(median_residual) / image_diagonal
        : 0.0;
    if (motion.inlier_ratio < 0.60 || motion.displacement_spread_ratio > 0.015) {
        return motion;
    }

    motion.dx = median_dx;
    motion.dy = median_dy;
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

static WeakTrackResult weakTrackWithOpticalFlowImpl(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    const std::vector<TrackedDetection>& previous_tracks,
    int image_width,
    int image_height,
    const std::vector<cv::Mat>& cached_previous,
    std::vector<cv::Mat>* cached_current
) {
    if (cached_current != nullptr) {
        cached_current->clear();
    }
    WeakTrackResult result;
    result.quality.previous_track_count = previous_tracks.size();
    const auto diff_start = std::chrono::steady_clock::now();
    result.quality.mean_frame_diff = normalizedMeanAbsDiff(previous_gray, current_gray);
    result.stage_ms[0] = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - diff_start).count();
    result.track_qualities.reserve(previous_tracks.size());

    if (previous_gray.empty() || current_gray.empty() || previous_tracks.empty()) {
        return result;
    }

    constexpr int kMaxCornersPerTrack = 20;
    constexpr int kMinTrackedPoints = 4;
    constexpr double kMinValidPointRatio = 0.50;
    constexpr float kCandidateForwardBackwardError = 5.0F;
    constexpr double kMaxMedianForwardBackwardError = 2.0;
    constexpr double kMaxDisplacementSpreadRatio = 0.08;
    constexpr double kMaxMotionRatio = 0.35;
    constexpr double kMinVisibleAreaRatio = 0.50;
    const double image_diagonal = std::hypot(
        static_cast<double>(image_width),
        static_cast<double>(image_height)
    );

    std::vector<cv::Point2f> previous_points;
    std::vector<size_t> point_track_indices;
    std::vector<size_t> sampled_points_by_track(previous_tracks.size(), 0);

    const auto features_start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < previous_tracks.size(); ++i) {
        const cv::Rect roi = detectionRoi(
            previous_tracks[i].detection,
            image_width,
            image_height
        );
        if (roi.width < 4 || roi.height < 4) {
            continue;
        }
        ++result.roi_count;
        result.roi_pixels += static_cast<uint64_t>(roi.width)
            * static_cast<uint64_t>(roi.height);

        std::vector<cv::Point2f> local_points;
        cv::goodFeaturesToTrack(
            previous_gray(roi),
            local_points,
            kMaxCornersPerTrack,
            0.01,
            3.0
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
    result.stage_ms[1] = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - features_start).count();
    if (previous_points.empty()) {
        return result;
    }

    std::vector<cv::Mat> previous_pyramid = cached_previous;
    std::vector<cv::Mat> current_pyramid;
    const auto pyramid_start = std::chrono::steady_clock::now();
    if (previous_pyramid.empty()) {
        cv::buildOpticalFlowPyramid(previous_gray, previous_pyramid, cv::Size(21, 21), 3, false);
    }
    // The current derivatives serve backward LK now and forward LK on the next accepted frame.
    cv::buildOpticalFlowPyramid(current_gray, current_pyramid, cv::Size(21, 21), 3,
                               cached_current != nullptr);
    result.stage_ms[2] = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - pyramid_start).count();

    std::vector<cv::Point2f> current_points;
    std::vector<unsigned char> status;
    std::vector<float> error;
    const auto forward_start = std::chrono::steady_clock::now();
    cv::calcOpticalFlowPyrLK(
        previous_pyramid,
        current_pyramid,
        previous_points,
        current_points,
        status,
        error,
        cv::Size(21, 21),
        3
    );
    result.stage_ms[3] = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - forward_start).count();

    std::vector<cv::Point2f> backward_points;
    std::vector<unsigned char> backward_status;
    std::vector<float> backward_error;
    const auto backward_start = std::chrono::steady_clock::now();
    cv::calcOpticalFlowPyrLK(
        current_pyramid,
        previous_pyramid,
        current_points,
        backward_points,
        backward_status,
        backward_error,
        cv::Size(21, 21),
        3
    );
    result.stage_ms[4] = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - backward_start).count();

    previous_pyramid.clear();
    if (cached_current != nullptr) {
        *cached_current = std::move(current_pyramid);
    }
    current_pyramid.clear();

    const auto quality_start = std::chrono::steady_clock::now();
    std::vector<std::vector<float>> dx_by_track(previous_tracks.size());
    std::vector<std::vector<float>> dy_by_track(previous_tracks.size());
    std::vector<std::vector<float>> fb_error_by_track(previous_tracks.size());
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
        const size_t track_index = point_track_indices[i];
        // 前向跟踪再反向返回，偏离原始点过大说明匹配不可靠。
        fb_error_by_track[track_index].push_back(forward_backward_error);
        if (forward_backward_error > kCandidateForwardBackwardError) {
            continue;
        }

        const float dx = current_points[i].x - previous_points[i].x;
        const float dy = current_points[i].y - previous_points[i].y;
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
        TrackFlowQuality track_quality;
        track_quality.track_id = previous_tracks[i].track_id;
        track_quality.sampled_point_count = sampled_points_by_track[i];
        track_quality.valid_point_count = dx_by_track[i].size();
        track_quality.valid_point_ratio = sampled_points_by_track[i] > 0
            ? static_cast<double>(dx_by_track[i].size())
                / static_cast<double>(sampled_points_by_track[i])
            : 0.0;
        if (!fb_error_by_track[i].empty()) {
            track_quality.median_forward_backward_error =
                static_cast<double>(medianValue(fb_error_by_track[i]));
        }

        if (dx_by_track[i].size() < kMinTrackedPoints
            || dy_by_track[i].size() < kMinTrackedPoints) {
            if (sampled_points_by_track[i] > 0) {
                ++result.quality.low_point_track_count;
            }
            result.track_qualities.push_back(track_quality);
            continue;
        }
        if (track_quality.valid_point_ratio < kMinValidPointRatio) {
            ++result.quality.invalid_ratio_track_count;
            result.track_qualities.push_back(track_quality);
            continue;
        }
        if (track_quality.median_forward_backward_error
            > kMaxMedianForwardBackwardError) {
            ++result.quality.forward_backward_rejection_count;
            result.track_qualities.push_back(track_quality);
            continue;
        }

        // 用目标内有效点位移的中位数估计平移，减弱少量离群点的影响。
        const float dx = medianValue(dx_by_track[i]);
        const float dy = medianValue(dy_by_track[i]);
        track_quality.median_dx = dx;
        track_quality.median_dy = dy;
        const double bbox_diagonal = std::hypot(
            static_cast<double>(previous_tracks[i].detection.x2
                - previous_tracks[i].detection.x1),
            static_cast<double>(previous_tracks[i].detection.y2
                - previous_tracks[i].detection.y1)
        );
        std::vector<float> residuals;
        residuals.reserve(dx_by_track[i].size());
        for (size_t point_index = 0;
             point_index < dx_by_track[i].size();
             ++point_index) {
            residuals.push_back(std::hypot(
                dx_by_track[i][point_index] - dx,
                dy_by_track[i][point_index] - dy
            ));
        }
        track_quality.displacement_spread_ratio = bbox_diagonal > 0.0
            ? static_cast<double>(medianValue(residuals)) / bbox_diagonal
            : 0.0;
        track_quality.motion_ratio = bbox_diagonal > 0.0
            ? std::hypot(static_cast<double>(dx), static_cast<double>(dy))
                / bbox_diagonal
            : 0.0;
        if (track_quality.displacement_spread_ratio
            > kMaxDisplacementSpreadRatio) {
            ++result.quality.motion_dispersion_rejection_count;
            result.track_qualities.push_back(track_quality);
            continue;
        }
        if (track_quality.motion_ratio > kMaxMotionRatio) {
            ++result.quality.motion_jump_rejection_count;
            result.track_qualities.push_back(track_quality);
            continue;
        }

        Detection detection = previous_tracks[i].detection;
        detection.x1 += dx;
        detection.x2 += dx;
        detection.y1 += dy;
        detection.y2 += dy;
        const float center_x = (detection.x1 + detection.x2) * 0.5F;
        const float center_y = (detection.y1 + detection.y2) * 0.5F;
        track_quality.visible_area_ratio = visibleAreaRatio(
            detection,
            image_width,
            image_height
        );
        track_quality.boundary_clipped = track_quality.visible_area_ratio < 0.999;
        if (center_x < 0.0F || center_x >= static_cast<float>(image_width)
            || center_y < 0.0F || center_y >= static_cast<float>(image_height)
            || track_quality.visible_area_ratio < kMinVisibleAreaRatio) {
            ++result.quality.boundary_rejection_count;
            result.track_qualities.push_back(track_quality);
            continue;
        }

        // 光流只能延续检测，逐帧衰减分数以体现长时间未获检测校正的不确定性。
        detection.score *= 0.98F;
        clipDetection(detection, image_width, image_height);
        if (!hasValidBox(detection)) {
            ++result.quality.boundary_rejection_count;
            result.track_qualities.push_back(track_quality);
            continue;
        }

        track_quality.accepted = true;
        tracked_detections.push_back(TrackedDetection{
            previous_tracks[i].track_id,
            detection
        });
        result.track_qualities.push_back(track_quality);
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

    result.stage_ms[5] = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - quality_start).count();
    return result;
}

WeakTrackResult weakTrackWithOpticalFlow(
    const cv::Mat& previous_gray, const cv::Mat& current_gray,
    const std::vector<TrackedDetection>& previous_tracks, int image_width, int image_height
) {
    return weakTrackWithOpticalFlowImpl(previous_gray, current_gray, previous_tracks,
                                       image_width, image_height, {}, nullptr);
}

WeakTrackResult weakTrackWithOpticalFlow(
    const cv::Mat& previous_gray, const cv::Mat& current_gray,
    const std::vector<TrackedDetection>& previous_tracks, int image_width, int image_height,
    const std::vector<cv::Mat>& previous_pyramid, std::vector<cv::Mat>& current_pyramid
) {
    return weakTrackWithOpticalFlowImpl(previous_gray, current_gray, previous_tracks,
                                       image_width, image_height, previous_pyramid, &current_pyramid);
}

FrameMotion frameMotionForCurrentFrame(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    int64_t frame_index,
    const WeakTrackQuality& quality,
    const std::vector<TrackedDetection>& previous_tracks
) {
    // 目标光流已足够可靠时跳过背景运动估计，减少整帧计算开销。
    if (previous_tracks.empty() || quality.tracked_track_ratio >= 0.80) {
        return emptyFrameMotion(frame_index);
    }
    return estimateGlobalFrameMotion(
        previous_gray,
        current_gray,
        frame_index,
        previous_tracks
    );
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
