#include "stream/stream_processor.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include <opencv2/imgproc.hpp>

namespace yolo {

PreparedStreamFrame StreamProcessor::prepareFrame(
    const cv::Mat& frame, int64_t frame_index, bool estimate_frame_motion
) const {
    PreparedStreamFrame prepared;
    prepared.frame_index = frame_index;
    prepared.motion.frame_index = frame_index;
    const auto gray_start = std::chrono::steady_clock::now();
    cv::cvtColor(frame, prepared.gray, cv::COLOR_BGR2GRAY);
    prepared.preparation_ms[0] = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - gray_start).count();
    const auto flow_start = std::chrono::steady_clock::now();
    prepared.weak.quality.previous_track_count = tracks_.size();
    // With no tracks all refresh predicates are false; avoid a full-frame difference pass.
    if (!previous_gray_.empty() && !tracks_.empty()) {
        const auto weak_start = std::chrono::steady_clock::now();
        prepared.weak = weakTrackWithOpticalFlow(
            previous_gray_, prepared.gray, tracks_, frame.cols, frame.rows,
            previous_pyramid_, prepared.gray_pyramid
        );
        prepared.preparation_ms[1] = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - weak_start).count();
        if (estimate_frame_motion) {
            const auto motion_start = std::chrono::steady_clock::now();
            prepared.motion = frameMotionForCurrentFrame(
                previous_gray_, prepared.gray, frame_index, prepared.weak.quality, tracks_
            );
            prepared.preparation_ms[2] = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - motion_start).count();
        }
        if (high_low_) {
            const auto projection_start = std::chrono::steady_clock::now();
            prepared.direct_motions = directTrackMotions(tracks_, prepared.weak);
            prepared.projected_tracks = projectTracks(
                tracks_, prepared.direct_motions, prepared.motion,
                !isSevereWeakQualityDrop(prepared.weak.quality), frame.cols, frame.rows
            );
            prepared.preparation_ms[3] = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - projection_start).count();
        }
    }
    prepared.optical_flow_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - flow_start
    ).count();
    return prepared;
}

const std::vector<TrackedDetection>& StreamProcessor::applyDetections(
    const std::vector<Detection>& detections,
    int64_t frame_index,
    bool high_res,
    const std::vector<ProjectedTrack>& projected_tracks,
    const HighResRegion* high_res_region
) {
    if (high_low_) {
        if (high_res && high_res_region != nullptr) {
            tracks_ = authority_tracker_.updateHighResRegion(
                detections, projected_tracks, *high_res_region, frame_index
            );
        } else {
            tracks_ = high_res
                ? authority_tracker_.updateHighRes(detections, frame_index)
                : authority_tracker_.updateLowRes(detections, projected_tracks, frame_index);
        }
    } else {
        if (!high_res) {
            throw std::invalid_argument("single-model processor cannot apply low-model detections");
        }
        tracks_ = byte_tracker_.update(detections);
    }
    return tracks_;
}

const std::vector<TrackedDetection>& StreamProcessor::applyFlow(
    const PreparedStreamFrame& frame, bool advance_empty_single_model
) {
    if (high_low_) {
        tracks_ = authority_tracker_.updateFlow(frame.projected_tracks, frame.frame_index);
    } else if (advance_empty_single_model || !frame.weak.tracks.empty()) {
        tracks_ = byte_tracker_.updateTracked(frame.weak.tracks);
    } else {
        // Preserve the existing offline single-model rule: no ByteTracker tick on empty flow.
        tracks_.clear();
    }
    return tracks_;
}

void StreamProcessor::finishFrame(PreparedStreamFrame&& frame) {
    // 仅提交已接受帧的光流参考图；被丢弃的预处理结果不能推进参考帧。
    previous_gray_ = std::move(frame.gray);
    previous_pyramid_ = std::move(frame.gray_pyramid);
}

void StreamProcessor::restoreAuthority(AuthorityTracker tracker) {
    if (!high_low_) {
        throw std::logic_error("single-model processor has no authority replay state");
    }
    authority_tracker_ = std::move(tracker);
    tracks_ = authority_tracker_.tracks();
}

void StreamProcessor::restoreSingleModel(
    ByteTracker tracker, std::vector<TrackedDetection> tracks
) {
    if (high_low_) {
        throw std::logic_error("high-low processor cannot restore single-model state");
    }
    byte_tracker_ = std::move(tracker);
    tracks_ = std::move(tracks);
}

SingleModelStreamState StreamProcessor::singleModelState() const {
    if (high_low_) {
        throw std::logic_error("high-low processor cannot snapshot single-model state");
    }
    return {byte_tracker_, tracks_, previous_gray_};
}

}  // namespace yolo
