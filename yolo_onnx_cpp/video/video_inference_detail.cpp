#include "video/video_inference_detail.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "image/image_processing.h"

namespace yolo::video_inference_detail {
namespace {

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

AsyncInferResult runAsyncInfer(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    cv::Mat frame,
    int64_t frame_index
) {
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

}  // namespace

double elapsedMs(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
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
    (void)config;
    return "Failed to preprocess video frame";
}

AsyncInferWorker::AsyncInferWorker(std::shared_ptr<YoloEngine> engine, AppConfig config)
    : engine_(std::move(engine)),
      config_(std::move(config)),
      worker_(&AsyncInferWorker::run, this) {}

AsyncInferWorker::~AsyncInferWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    request_ready_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool AsyncInferWorker::submit(cv::Mat frame, int64_t frame_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || hasPendingLocked()) {
        return false;
    }

    requests_.push_back(Request{frame_index, std::move(frame)});
    request_ready_.notify_one();
    return true;
}

bool AsyncInferWorker::tryPopResult(AsyncInferResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.empty()) {
        return false;
    }

    result = std::move(results_.front());
    results_.pop_front();
    return true;
}

bool AsyncInferWorker::waitPopResult(AsyncInferResult& result) {
    std::unique_lock<std::mutex> lock(mutex_);
    result_ready_.wait(lock, [this]() {
        return !results_.empty() || !hasPendingLocked();
    });
    if (results_.empty()) {
        return false;
    }

    result = std::move(results_.front());
    results_.pop_front();
    return true;
}

bool AsyncInferWorker::hasPending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasPendingLocked();
}

bool AsyncInferWorker::hasPendingLocked() const {
    return in_flight_ || !requests_.empty() || !results_.empty();
}

void AsyncInferWorker::run() {
    while (true) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            request_ready_.wait(lock, [this]() {
                return stopping_ || !requests_.empty();
            });
            if (requests_.empty()) {
                if (stopping_) {
                    return;
                }
                continue;
            }

            request = std::move(requests_.front());
            requests_.pop_front();
            in_flight_ = true;
        }

        AsyncInferResult result = runAsyncInfer(
            engine_,
            config_,
            std::move(request.frame),
            request.frame_index
        );

        {
            std::lock_guard<std::mutex> lock(mutex_);
            results_.push_back(std::move(result));
            in_flight_ = false;
        }
        result_ready_.notify_all();
    }
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

}  // namespace yolo::video_inference_detail
