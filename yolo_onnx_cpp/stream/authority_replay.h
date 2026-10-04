#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include <opencv2/core.hpp>

#include "tracking/authority_tracker.h"
#include "video/optical_flow_tracker.h"

namespace yolo {

// 保存一帧的运动和检测事实；异步检测补齐后可按源帧顺序重建跟踪状态。
struct AuthorityReplayFrame {
    int64_t frame_index = 0;
    int image_width = 0;
    int image_height = 0;
    FrameMotion motion;
    std::vector<TrackMotion> direct_motions;
    std::vector<Detection> low_res_detections;
    std::vector<Detection> high_res_detections;
    cv::Rect high_res_crop;
    HighResRegion high_res_region;
    size_t result_index = 0;
    bool allow_global_motion = false;
    bool has_low_res = false;
    bool has_high_res = false;
    bool high_res_is_roi = false;
};

inline std::vector<ProjectedTrack> replayProjectedTracks(
    const AuthorityReplayFrame& frame,
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

inline std::vector<TrackedDetection> applyReplayFrame(
    const AuthorityReplayFrame& frame,
    AuthorityTracker& tracker
) {
    // 同帧高分辨率结果优先；没有检测结果的帧才完全依赖光流。
    if (frame.has_high_res) {
        if (!frame.high_res_is_roi) {
            return tracker.updateHighRes(
                frame.high_res_detections, frame.frame_index
            );
        }
        return tracker.updateHighResRegion(
            frame.high_res_detections, replayProjectedTracks(frame, tracker),
            frame.high_res_region, frame.frame_index
        );
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

inline void trimReplayBuffer(
    std::deque<AuthorityReplayFrame>& replay_buffer,
    AuthorityTracker& base_tracker,
    size_t max_frames
) {
    // 丢弃历史前先推进基准跟踪器，使基准始终对应缓冲区首帧之前的状态。
    while (replay_buffer.size() > max_frames) {
        applyReplayFrame(replay_buffer.front(), base_tracker);
        replay_buffer.pop_front();
    }
}

}  // namespace yolo
