#include "video/video_inference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "image/image_processing.h"
#include "tracking/authority_tracker.h"
#include "stream/high_res_roi.h"
#include "stream/authority_replay.h"
#include "stream/stream_processor.h"
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

constexpr size_t kReplayBufferFrames = 60;
constexpr int kHighResCadenceMultiplier = 4;
// The minimum cadence is shared with the authority tracker's age policy.

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

InferResult runModel(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    const cv::Mat& frame,
    const std::shared_ptr<InferenceScheduler>& scheduler,
    const std::string& stream_id,
    int64_t frame_index,
    bool urgent
) {
    const auto preprocess_start = std::chrono::steady_clock::now();
    auto input = preprocessImageMat(frame, config);
    const double preprocess_ms = elapsedMs(preprocess_start);
    if (!input.has_value()) {
        throw VideoInferError(invalidVideoFrameMessage(config), true);
    }

    InferenceContext context;
    context.stream_id = stream_id;
    context.frame_index = frame_index;
    InferResult result = scheduler != nullptr
        ? scheduler->infer(std::move(input.value()), std::move(context), urgent)
        : engine->infer(input.value(), std::move(context));
    result.preprocess_ms = preprocess_ms;
    result.timing_samples.preprocess_ms.push_back(preprocess_ms);
    if (scheduler == nullptr) {
        result.timing_samples.queue_wait_ms.push_back(0.0);
    }
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

using ReplayFrame = AuthorityReplayFrame;

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
    const std::filesystem::path& video_path,
    std::shared_ptr<InferenceScheduler> high_res_scheduler,
    std::shared_ptr<InferenceScheduler> low_res_scheduler,
    std::string stream_id
) {
    if (low_res_engine == nullptr || !hasLowResModelConfig(config)) {
        return inferVideoFile(
            high_res_engine, config, video_path, high_res_scheduler, std::move(stream_id)
        );
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

    StreamProcessor processor(true);
    auto& tracker = processor.authority();
    cv::Mat frame;
    const auto& previous_frame_tracks = processor.tracks();
    std::unordered_map<int, cv::Point2f> track_velocities;

    int64_t frame_index = 0;
    int64_t source_frame_count = 0;
    int64_t readable_frame_count = 0;
    int64_t processed_frame_count = 0;
    int64_t high_res_detection_count = 0;
    int64_t roi_high_res_detection_count = 0;
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
        &processor,
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
      const HighResRegion* high_res_region,
      VideoFrameTracks& frame_result) {
        auto tracker_start = std::chrono::steady_clock::now();
        const auto tracks_before_detection = previous_frame_tracks;
        const auto& detected_tracks = processor.applyDetections(
            detections, source_frame_index, high_res, projected_tracks,
            high_res_region
        );
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
        &processor,
        &result,
        &previous_frame_tracks,
        &track_velocities,
        &stride_state,
        &min_stride_used,
        &max_stride_used,
        dynamic_stride_enabled
    ](const PreparedStreamFrame& prepared,
      VideoFrameTracks& frame_result) {
        auto tracker_start = std::chrono::steady_clock::now();
        const auto tracks_before = previous_frame_tracks;

        frame_result.tracks = processor.applyFlow(prepared);
        if (!frame_result.tracks.empty()) {
            frame_result.tracks_source = "weak_tracked";
        }

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
        high_res_worker = std::make_unique<AsyncInferWorker>(
            high_res_engine, config, high_res_scheduler, stream_id
        );
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
        &processor,
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
        // 从历史基准重建跟踪器；迟到结果所在帧及之后的输出会被重新计算。
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

        processor.restoreAuthority(std::move(replay_tracker));
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
        &roi_high_res_detection_count,
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
            // 源帧已不在历史窗口内时无法可靠回放，该结果仅计入丢弃统计。
            if (replay_index == replay_buffer.size()) {
                ++dropped_frame_count;
                continue;
            }

            ReplayFrame& replay_frame = replay_buffer[replay_index];
            replay_frame.has_high_res = true;
            if (replay_frame.high_res_is_roi) {
                ++roi_high_res_detection_count;
                async_result.result.detections = translateHighResRoiDetections(
                    async_result.result.detections, replay_frame.high_res_crop,
                    cv::Size(replay_frame.image_width, replay_frame.image_height)
                );
            }
            replay_frame.high_res_detections =
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

        auto prepared = processor.prepareFrame(frame, frame_index, true);
        const auto& weak_result = prepared.weak;
        const auto& frame_motion = prepared.motion;
        auto& direct_motions = prepared.direct_motions;
        const auto& projected_tracks = prepared.projected_tracks;
        const bool severe_weak_drop = isSevereWeakQualityDrop(weak_result.quality);
        const auto projection_start = std::chrono::steady_clock::now();
        const auto projected_detections = projectedDetections(projected_tracks);
        result.timing.optical_flow_ms += prepared.optical_flow_ms + elapsedMs(projection_start);

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
        runtime_diagnostics.flow.flow_track_count += static_cast<int64_t>(
            weak_result.track_qualities.size()
        );
        runtime_diagnostics.flow.flow_rejected_track_count += rejected_flow_tracks;
        runtime_diagnostics.flow.flow_low_point_rejection_count += static_cast<int64_t>(
            weak_result.quality.low_point_track_count
        );
        runtime_diagnostics.flow.flow_invalid_ratio_rejection_count +=
            static_cast<int64_t>(weak_result.quality.invalid_ratio_track_count);
        runtime_diagnostics.flow.flow_forward_backward_rejection_count +=
            static_cast<int64_t>(
                weak_result.quality.forward_backward_rejection_count
            );
        runtime_diagnostics.flow.flow_motion_dispersion_rejection_count +=
            static_cast<int64_t>(
                weak_result.quality.motion_dispersion_rejection_count
            );
        runtime_diagnostics.flow.flow_motion_jump_rejection_count +=
            static_cast<int64_t>(weak_result.quality.motion_jump_rejection_count);
        runtime_diagnostics.flow.flow_boundary_rejection_count += static_cast<int64_t>(
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
            tracker_diagnostics.duplicates.stable_stable_duplicate_count
            + tracker_diagnostics.duplicates.stable_provisional_duplicate_count
            + tracker_diagnostics.duplicates.provisional_provisional_duplicate_count;
        const int64_t previous_duplicate_count =
            previous_tracker_diagnostics.duplicates.stable_stable_duplicate_count
            + previous_tracker_diagnostics.duplicates.stable_provisional_duplicate_count
            + previous_tracker_diagnostics.duplicates.provisional_provisional_duplicate_count;
        const bool duplicate_urgent = tracker.consumeLowResRefreshRequest()
            || duplicate_count - previous_duplicate_count >= 2;
        const bool flow_age_urgent = tracker.consumeFlowAgeRefreshRequest();
        const bool geometry_urgent =
            tracker_diagnostics.low_res.low_res_geometry_rejection_count
                > previous_tracker_diagnostics.low_res.low_res_geometry_rejection_count;
        const bool class_conflict_urgent =
            tracker_diagnostics.low_res.low_res_class_conflict_count
                > previous_tracker_diagnostics.low_res.low_res_class_conflict_count;
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
            ++runtime_diagnostics.urgent.urgent_low_res_detection_count;
            runtime_diagnostics.urgent.urgent_flow_quality_count += flow_quality_urgent ? 1 : 0;
            runtime_diagnostics.urgent.urgent_track_change_count += track_change_urgent ? 1 : 0;
            runtime_diagnostics.urgent.urgent_duplicate_count += duplicate_urgent ? 1 : 0;
            runtime_diagnostics.urgent.urgent_flow_age_count += flow_age_urgent ? 1 : 0;
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
            ++runtime_diagnostics.urgent.urgent_high_res_detection_count;
            runtime_diagnostics.urgent.urgent_geometry_count += geometry_urgent ? 1 : 0;
            runtime_diagnostics.urgent.urgent_class_conflict_count +=
                class_conflict_urgent ? 1 : 0;
        }
        if (force_high_res && high_res_detection_count == 0) {
            pending_high_res_refresh = false;
            last_urgent_high_res_frame_index = frame_index;
        }
        const bool scheduled_high_res = scheduled_detection
            && frameDue(last_high_res_frame_index, frame_index, high_res_cadence);
        const bool request_high_res = force_high_res || scheduled_high_res;
        const auto roi_selection = request_high_res
            ? selectHighResRoi(
                config, frame.size(), force_high_res, high_res_detection_count
            )
            : std::optional<HighResRoiSelection>{};

        ReplayFrame replay_frame;
        replay_frame.frame_index = frame_index;
        replay_frame.image_width = image_width;
        replay_frame.image_height = image_height;
        replay_frame.motion = frame_motion;
        replay_frame.direct_motions = std::move(direct_motions);
        replay_frame.allow_global_motion = !severe_weak_drop;
        if (roi_selection.has_value()) {
            replay_frame.high_res_is_roi = true;
            replay_frame.high_res_crop = roi_selection->crop;
            replay_frame.high_res_region = roi_selection->authority_region;
        }

        if (request_high_res && high_res_worker != nullptr) {
            size_t queue_length = high_res_worker->pendingCount();
            max_queue_length = std::max(max_queue_length, queue_length);
            const cv::Mat high_res_frame = roi_selection.has_value()
                ? frame(roi_selection->crop)
                : frame;
            if (high_res_worker->submit(
                    high_res_frame.clone(), frame_index, &queue_length)) {
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
            const cv::Mat high_res_frame = roi_selection.has_value()
                ? frame(roi_selection->crop)
                : frame;
            InferResult high_result = runModel(
                high_res_engine, config, high_res_frame, high_res_scheduler,
                stream_id, frame_index, force_high_res
            );
            addInferTiming(result, high_result);
            replay_frame.has_high_res = true;
            if (roi_selection.has_value()) {
                ++roi_high_res_detection_count;
                high_result.detections = translateHighResRoiDetections(
                    high_result.detections, roi_selection->crop, frame.size()
                );
            }
            applyDetections(
                high_result.detections,
                frame_index,
                force_high_res && high_res_detection_count > 0,
                true,
                projected_tracks,
                roi_selection.has_value()
                    ? &roi_selection->authority_region : nullptr,
                frame_result
            );
            replay_frame.high_res_detections = std::move(high_result.detections);
            ++high_res_detection_count;
            last_high_res_frame_index = frame_index;
        } else if (scheduled_detection) {
            InferResult low_result = runModel(
                low_res_engine, low_res_config, frame, low_res_scheduler,
                stream_id, frame_index, urgent_low_res
            );
            addInferTiming(result, low_result);
            replay_frame.has_low_res = true;

            applyDetections(
                low_result.detections,
                frame_index,
                urgent_low_res,
                false,
                projected_tracks,
                nullptr,
                frame_result
            );
            replay_frame.low_res_detections = std::move(low_result.detections);
        } else {
            applyWeakTracks(prepared, frame_result);
        }

        processor.finishFrame(std::move(prepared));
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
    result.timing.total_elapsed_ms = elapsedMs(total_start);
    result.queue.queue_length = high_res_worker != nullptr ? high_res_worker->pendingCount() : 0;
    result.queue.max_queue_length = max_queue_length;
    result.queue.dropped_frame_count = dropped_frame_count;
    result.detection_counts.roi_high_res_detection_count = roi_high_res_detection_count;
    result.high_low_diagnostics = tracker.diagnostics();
    result.high_low_diagnostics.flow.flow_track_count =
        runtime_diagnostics.flow.flow_track_count;
    result.high_low_diagnostics.flow.flow_rejected_track_count =
        runtime_diagnostics.flow.flow_rejected_track_count;
    result.high_low_diagnostics.flow.flow_low_point_rejection_count =
        runtime_diagnostics.flow.flow_low_point_rejection_count;
    result.high_low_diagnostics.flow.flow_invalid_ratio_rejection_count =
        runtime_diagnostics.flow.flow_invalid_ratio_rejection_count;
    result.high_low_diagnostics.flow.flow_forward_backward_rejection_count =
        runtime_diagnostics.flow.flow_forward_backward_rejection_count;
    result.high_low_diagnostics.flow.flow_motion_dispersion_rejection_count =
        runtime_diagnostics.flow.flow_motion_dispersion_rejection_count;
    result.high_low_diagnostics.flow.flow_motion_jump_rejection_count =
        runtime_diagnostics.flow.flow_motion_jump_rejection_count;
    result.high_low_diagnostics.flow.flow_boundary_rejection_count =
        runtime_diagnostics.flow.flow_boundary_rejection_count;
    result.high_low_diagnostics.urgent.urgent_low_res_detection_count =
        runtime_diagnostics.urgent.urgent_low_res_detection_count;
    result.high_low_diagnostics.urgent.urgent_high_res_detection_count =
        runtime_diagnostics.urgent.urgent_high_res_detection_count;
    result.high_low_diagnostics.urgent.urgent_flow_quality_count =
        runtime_diagnostics.urgent.urgent_flow_quality_count;
    result.high_low_diagnostics.urgent.urgent_track_change_count =
        runtime_diagnostics.urgent.urgent_track_change_count;
    result.high_low_diagnostics.urgent.urgent_duplicate_count =
        runtime_diagnostics.urgent.urgent_duplicate_count;
    result.high_low_diagnostics.urgent.urgent_flow_age_count =
        runtime_diagnostics.urgent.urgent_flow_age_count;
    result.high_low_diagnostics.urgent.urgent_geometry_count =
        runtime_diagnostics.urgent.urgent_geometry_count;
    result.high_low_diagnostics.urgent.urgent_class_conflict_count =
        runtime_diagnostics.urgent.urgent_class_conflict_count;
    const ProcessUsageSnapshot usage_end = captureProcessUsage();
    finalizeVideoPerformanceMetrics(
        result,
        result.frame_counts.frame_count,
        usage_start,
        usage_end
    );

    return result;
}

}  // namespace yolo
