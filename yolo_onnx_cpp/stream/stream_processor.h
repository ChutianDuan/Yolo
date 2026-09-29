#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <opencv2/core.hpp>

#include "tracking/authority_tracker.h"
#include "tracking/byte_tracker.h"
#include "video/optical_flow_tracker.h"

namespace yolo {

struct PreparedStreamFrame {
    int64_t frame_index = 0;
    cv::Mat gray;
    // Committed together with gray; discarded preparations cannot advance the cache.
    std::vector<cv::Mat> gray_pyramid;
    WeakTrackResult weak;
    FrameMotion motion;
    std::vector<TrackMotion> direct_motions;
    std::vector<ProjectedTrack> projected_tracks;
    double optical_flow_ms = 0.0;
    // A skipped conditional stage must not be reported as a zero-duration call.
    std::array<std::optional<double>, 4> preparation_ms{};
};

struct SingleModelStreamState {
    ByteTracker tracker;
    std::vector<TrackedDetection> tracks;
    cv::Mat gray;
};

// One instance per stream, confined to its processing thread.
// Preparation is read-only; only accepted frames update tracks and commit their gray image.
class StreamProcessor {
public:
    explicit StreamProcessor(bool high_low) : high_low_(high_low) {}

    PreparedStreamFrame prepareFrame(
        const cv::Mat& frame, int64_t frame_index, bool estimate_frame_motion
    ) const;

    const std::vector<TrackedDetection>& applyDetections(
        const std::vector<Detection>& detections,
        int64_t frame_index,
        bool high_res,
        const std::vector<ProjectedTrack>& projected_tracks = {},
        const HighResRegion* high_res_region = nullptr
    );

    const std::vector<TrackedDetection>& applyFlow(
        const PreparedStreamFrame& frame,
        bool advance_empty_single_model = true
    );

    void finishFrame(PreparedStreamFrame&& frame);
    const std::vector<TrackedDetection>& tracks() const { return tracks_; }

    // Policy diagnostics/refresh flags; use restoreAuthority for replay replacement.
    AuthorityTracker& authority() { return authority_tracker_; }
    void restoreAuthority(AuthorityTracker tracker);
    void restoreSingleModel(ByteTracker tracker, std::vector<TrackedDetection> tracks);
    SingleModelStreamState singleModelState() const;

private:
    bool high_low_;
    ByteTracker byte_tracker_;
    AuthorityTracker authority_tracker_;
    cv::Mat previous_gray_;
    std::vector<cv::Mat> previous_pyramid_;
    std::vector<TrackedDetection> tracks_;
};

}  // namespace yolo
