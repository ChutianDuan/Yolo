#include "profiled_video.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "image/image_processing.h"
#include "stream/stream_processor.h"
#include "video/optical_flow_tracker.h"
#include "video/video_inference_detail.h"

namespace yolo {

VideoInferError::VideoInferError(std::string message, bool bad_request)
    : std::runtime_error(std::move(message)),
      bad_request_(bad_request) {}

bool VideoInferError::badRequest() const {
    return bad_request_;
}

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
using video_inference_detail::shouldRunScheduledDetection;
using video_inference_detail::TrackChangeQuality;
using video_inference_detail::trackChangeQuality;
using video_inference_detail::updateTrackVelocities;
using video_inference_detail::videoFrameStride;

ProfiledVideoResult profiledInferVideoFile(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    const std::filesystem::path& video_path,
    std::shared_ptr<InferenceScheduler> scheduler,
    std::string stream_id
) {
    StageProfile profile;
    if (config.video_model_async || config.video_stride_mode != "fixed" || scheduler) {
        throw std::runtime_error("Stage experiment requires sync fixed inference without scheduler");
    }
    const auto total_start = std::chrono::steady_clock::now();
    const ProcessUsageSnapshot usage_start = captureProcessUsage();
    cv::VideoCapture capture;
    if (!openVideoCapture(capture, video_path)) {
        throw VideoInferError(videoOpenFailureMessage(video_path), true);
    }
    // 读取帧数信息，并初始化默认抽帧间隔 stride_state
    const double source_fps = finiteOrZero(capture.get(cv::CAP_PROP_FPS));
    const double declared_frame_count = finiteOrZero(capture.get(cv::CAP_PROP_FRAME_COUNT));
    const int base_frame_stride = videoFrameStride(source_fps, config.video_detect_fps);
    DynamicStrideState stride_state = makeDynamicStrideState(base_frame_stride);
    const bool async_enabled = config.video_model_async && base_frame_stride > 1;
    const bool dynamic_stride_enabled = base_frame_stride > 1
        && config.video_stride_mode == "dynamic";
    const std::string stride_mode = base_frame_stride <= 1
        ? "full_model"
        : std::string(async_enabled ? "async_" : "sync_")
            + (dynamic_stride_enabled ? "dynamic" : "fixed");

    VideoInferResult result;
    std::vector<FrameMotion> frame_motions;
    if (declared_frame_count > 0.0) {
        const auto reserve_frame_count = static_cast<size_t>(declared_frame_count);
        result.frames.reserve(reserve_frame_count);
        frame_motions.reserve(reserve_frame_count);
    }
    StreamProcessor processor(false);
    std::unique_ptr<AsyncInferWorker> async_worker;
    if (async_enabled) {
        async_worker = std::make_unique<AsyncInferWorker>(
            engine, config, scheduler, stream_id
        );
    }
    cv::Mat frame;
    const auto& previous_frame_tracks = processor.tracks();
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
    int64_t dropped_frame_count = 0;
    size_t max_queue_length = 0;
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
        &processor,
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

        addInferMetrics(result, async_result.result);

        auto tracker_start = std::chrono::steady_clock::now();
        // 异步结果属于较早的源帧，先用累计运动补偿到当前帧，再交给跟踪器。
        const auto corrected_detections = motionCompensatedDetections(
            async_result.result.detections,
            async_result.frame_index,
            current_frame_index,
            frame_motions,
            current_image_width,
            current_image_height
        );

        const auto tracks_before_detection = previous_frame_tracks;
        const auto& corrected_tracks = processor.applyDetections(
            corrected_detections, current_frame_index, true
        );

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

        if (target_frame != nullptr) {
            target_frame->tracks = corrected_tracks;
            target_frame->tracks_source = "async_corrected";
            target_frame->corrected_from_frame_index = async_result.frame_index;
            target_frame->correction_latency_frames =
                current_frame_index - async_result.frame_index;
        }

        ++processed_frame_count;
        ++async_correction_count;
        addTrackerMetrics(result, elapsedMs(tracker_start));
        return true;
    };

    auto processReadyInferences = [
        &async_worker,
        &applyAsyncInferResult
    ](bool wait_for_all,
      int64_t current_frame_index,
      int current_image_width,
      int current_image_height,
      VideoFrameTracks* target_frame,
      bool* applied_result) {
        if (async_worker == nullptr) {
            return true;
        }

        while (true) {
            AsyncInferResult async_result;
            const bool has_result = wait_for_all
                ? async_worker->waitPopResult(async_result)
                : async_worker->tryPopResult(async_result);
            if (!has_result) {
                return true;
            }

            if (!applyAsyncInferResult(
                    std::move(async_result),
                    current_frame_index,
                    current_image_width,
                    current_image_height,
                    target_frame)) {
                return false;
            }
            if (applied_result != nullptr) {
                *applied_result = true;
            }
        }
    };

    auto scheduleAsyncDetection = [
        &async_worker,
        &stride_state,
        &async_infer_request_count,
        &forced_detection_count,
        &scheduled_detection_count,
        &skipped_detection_count,
        &dropped_frame_count,
        &max_queue_length,
        &rememberStride
    ](const cv::Mat& source_frame,
      int64_t source_frame_index,
      bool forced,
      VideoFrameTracks& frame_result) {
        if (async_worker == nullptr) {
            return;
        }

        size_t queue_length = async_worker->pendingCount();
        max_queue_length = std::max(max_queue_length, queue_length);
        if (queue_length > 0) {
            ++skipped_detection_count;
            ++dropped_frame_count;
            return;
        }

        cv::Mat async_frame = source_frame.clone();
        if (!async_worker->submit(std::move(async_frame), source_frame_index, &queue_length)) {
            max_queue_length = std::max(max_queue_length, queue_length);
            ++skipped_detection_count;
            ++dropped_frame_count;
            return;
        }
        max_queue_length = std::max(max_queue_length, queue_length);

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
        &profile,
        &engine,
        &config,
        &scheduler,
        &stream_id,
        &processor,
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
        const auto preprocess_start = std::chrono::steady_clock::now();
        auto input = preprocessImageMat(source_frame, config);
        const double preprocess_ms = elapsedMs(preprocess_start);
        if (!input.has_value()) {
            throw VideoInferError(invalidVideoFrameMessage(config), true);
        }

        InferenceContext context;
        context.stream_id = stream_id;
        context.frame_index = source_frame_index;
        InferResult infer_result = scheduler != nullptr
            ? scheduler->infer(
                std::move(input.value()), std::move(context), forced
            )
            : engine->infer(input.value(), std::move(context));
        infer_result.preprocess_ms = preprocess_ms;
        infer_result.timing_samples.preprocess_ms.push_back(preprocess_ms);
        if (scheduler == nullptr) {
            infer_result.timing_samples.queue_wait_ms.push_back(0.0);
        }
        addInferMetrics(result, infer_result);

        auto tracker_start = std::chrono::steady_clock::now();
        const auto tracks_before_detection = previous_frame_tracks;
        const auto byte_start = std::chrono::steady_clock::now();
        const auto& detected_tracks = processor.applyDetections(
            infer_result.detections, source_frame_index, true
        );
        profile.byte_track_ms.push_back(elapsedMs(byte_start));
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
        rememberStride();
        addTrackerMetrics(result, elapsedMs(tracker_start));
    };

    while (true) {
        const auto decode_start = std::chrono::steady_clock::now();
        const bool decoded = capture.read(frame);
        const double decode_ms = elapsedMs(decode_start);
        if (!decoded) break;
        profile.video_decode_ms.push_back(decode_ms);
        const auto frame_start = std::chrono::steady_clock::now();
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
            addEndToEndSample(result, elapsedMs(frame_start));
            ++frame_index;
            continue;
        }

        ++readable_frame_count;
        image_width = frame.cols;
        image_height = frame.rows;
        const auto tracks_before_flow = previous_frame_tracks;
        const auto flow_start = std::chrono::steady_clock::now();
        auto prepared = processor.prepareFrame(frame, frame_index, true);
        profile.lk_flow_ms.push_back(elapsedMs(flow_start));
        const auto& weak_result = prepared.weak;
        frame_motions.push_back(prepared.motion);
        result.timing.optical_flow_ms += prepared.optical_flow_ms;

        bool async_result_applied = false;
        if (!processReadyInferences(
                false,
                frame_index,
                image_width,
                image_height,
                &frame_result,
                &async_result_applied)) {
            throw VideoInferError(async_error_message, async_error_bad_request);
        }

        if (!async_result_applied) {
            auto tracker_start = std::chrono::steady_clock::now();
            const auto byte_start = std::chrono::steady_clock::now();
            const auto& flow_tracks = processor.applyFlow(prepared, false);
            profile.byte_track_ms.push_back(elapsedMs(byte_start));
            frame_result.tracks = flow_tracks;
            if (!frame_result.tracks.empty()) {
                frame_result.tracks_source = "weak_tracked";
            }

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
            addTrackerMetrics(result, elapsedMs(tracker_start));
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

        processor.finishFrame(std::move(prepared));
        result.frames.push_back(std::move(frame_result));
        addEndToEndSample(result, elapsedMs(frame_start));
        ++frame_index;
    }

    if (async_worker != nullptr && async_worker->hasPending() && readable_frame_count > 0) {
        VideoFrameTracks* correction_frame = result.frames.empty()
            ? nullptr
            : &result.frames.back();
        const int64_t current_frame_index = frame_index > 0 ? frame_index - 1 : 0;
        if (!processReadyInferences(
                true,
                current_frame_index,
                image_width,
                image_height,
                correction_frame,
                nullptr)) {
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
    result.timing.total_elapsed_ms = elapsedMs(total_start);
    result.queue.queue_length = async_worker != nullptr ? async_worker->pendingCount() : 0;
    result.queue.max_queue_length = max_queue_length;
    result.queue.dropped_frame_count = dropped_frame_count;
    const ProcessUsageSnapshot usage_end = captureProcessUsage();
    finalizeVideoPerformanceMetrics(
        result,
        result.frame_counts.frame_count,
        usage_start,
        usage_end
    );

    profile.cpu_seconds = usage_end.cpu_seconds - usage_start.cpu_seconds;
    profile.cpu_wall_ms = std::chrono::duration<double, std::milli>(
        usage_end.wall_time - usage_start.wall_time).count();
    return {std::move(result), std::move(profile)};
}

}  // namespace yolo
