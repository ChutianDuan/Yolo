#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "config/app_config.h"
#include "model/yolo_engine.h"
#include "video/video_inference.h"

#ifndef YOLO_TEST_SOURCE_DIR
#define YOLO_TEST_SOURCE_DIR "."
#endif

namespace {

using yolo::AppConfig;
using yolo::YoloEngine;

struct VideoMetrics {
    int64_t frames = 0;
    int64_t processed = 0;
    int64_t detected = 0;
    int64_t forced = 0;
    int64_t skipped = 0;
    size_t track_observations = 0;
    size_t unique_tracks = 0;
    double elapsed_ms = 0.0;
    double model_ms = 0.0;
};

void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

std::string envString(const char* name);

std::filesystem::path repoRoot() {
    return std::filesystem::path(YOLO_TEST_SOURCE_DIR).parent_path();
}

std::filesystem::path defaultSourceVideo(const std::filesystem::path& root) {
    const std::filesystem::path dataset_video =
        root / "datasets" / "bdd100k_tracking_video" / "bdd100k_videos_train_00"
        / "bdd100k" / "videos" / "train" / "0000f77c-6257be58.mov";
    if (std::filesystem::exists(dataset_video)) {
        return dataset_video;
    }
    return root / "Readme" / "dynamic_onnx_flow_detections.mp4";
}

std::filesystem::path sourceVideoFromEnvOrDefault(const std::filesystem::path& root) {
    const std::string raw_path = envString("YOLO_COMPARE_SOURCE_VIDEO");
    if (raw_path.empty()) {
        return defaultSourceVideo(root);
    }

    std::filesystem::path path(raw_path);
    if (path.is_absolute()) {
        return path;
    }
    return root / path;
}

bool hasRequiredFiles(
    const std::filesystem::path& high_model,
    const std::filesystem::path& low_model,
    const std::filesystem::path& video
) {
    const bool ok = std::filesystem::exists(high_model)
        && std::filesystem::exists(low_model)
        && std::filesystem::exists(video);
    if (!ok) {
        std::cout << "video_inference_compare_test skipped: missing model or video\n"
                  << "  high_model=" << high_model << '\n'
                  << "  low_model=" << low_model << '\n'
                  << "  video=" << video << '\n';
    }
    return ok;
}

std::filesystem::path makeTempVideoPath() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / ("yolo_video_compare_" + std::to_string(now) + ".avi");
}

int envInt(const char* name, int default_value) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }

    try {
        size_t parsed = 0;
        const int value = std::stoi(raw, &parsed);
        if (parsed != std::string(raw).size() || value < 0) {
            throw std::invalid_argument("invalid");
        }
        return value;
    } catch (const std::exception&) {
        fail(std::string("Invalid integer environment value for ") + name + ": " + raw);
    }
    return default_value;
}

bool envBool(const char* name, bool default_value) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }

    const std::string value(raw);
    if (value == "1" || value == "true" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no") {
        return false;
    }

    fail(std::string("Invalid bool environment value for ") + name + ": " + raw);
    return default_value;
}

std::string envString(const char* name) {
    const char* raw = std::getenv(name);
    return raw == nullptr ? std::string() : std::string(raw);
}

std::filesystem::path makeShortVideo(
    const std::filesystem::path& source_video,
    int max_frames
) {
    cv::VideoCapture capture(source_video.string());
    if (!capture.isOpened()) {
        std::cout << "video_inference_compare_test skipped: cannot open source video "
                  << source_video << '\n';
        return {};
    }

    cv::Mat frame;
    if (!capture.read(frame) || frame.empty()) {
        std::cout << "video_inference_compare_test skipped: source video has no readable frames\n";
        return {};
    }

    double fps = capture.get(cv::CAP_PROP_FPS);
    if (!(fps > 0.0)) {
        fps = 30.0;
    }

    const std::filesystem::path output_path = makeTempVideoPath();
    cv::VideoWriter writer(
        output_path.string(),
        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
        fps,
        frame.size()
    );
    if (!writer.isOpened()) {
        std::cout << "video_inference_compare_test skipped: cannot create short video "
                  << output_path << '\n';
        return {};
    }

    int written = 0;
    do {
        writer.write(frame);
        ++written;
    } while ((max_frames == 0 || written < max_frames)
             && capture.read(frame)
             && !frame.empty());
    writer.release();

    if (written < 2) {
        std::cout << "video_inference_compare_test skipped: short video too small\n";
        std::error_code ignored;
        std::filesystem::remove(output_path, ignored);
        return {};
    }

    return output_path;
}

int clampedInt(float value, int low, int high) {
    return std::max(low, std::min(static_cast<int>(std::round(value)), high));
}

cv::Scalar colorForTrack(int track_id) {
    static const std::array<cv::Scalar, 12> colors = {
        cv::Scalar(56, 180, 255),
        cv::Scalar(80, 220, 120),
        cv::Scalar(255, 160, 80),
        cv::Scalar(180, 120, 255),
        cv::Scalar(80, 200, 220),
        cv::Scalar(230, 110, 140),
        cv::Scalar(180, 220, 70),
        cv::Scalar(250, 190, 60),
        cv::Scalar(120, 170, 255),
        cv::Scalar(100, 230, 190),
        cv::Scalar(220, 130, 230),
        cv::Scalar(190, 190, 190),
    };
    const int index = std::abs(track_id * 37) % static_cast<int>(colors.size());
    if (track_id < 0) {
        return cv::Scalar(0, 165, 255);
    }
    return colors[static_cast<size_t>(index)];
}

std::string classLabel(const AppConfig& config, int class_id) {
    if (class_id >= 0 && static_cast<size_t>(class_id) < config.class_names.size()) {
        return config.class_names[static_cast<size_t>(class_id)];
    }
    return "class" + std::to_string(class_id);
}

void drawFilledLabel(
    cv::Mat& frame,
    const std::string& label,
    const cv::Point& origin,
    const cv::Scalar& color
) {
    constexpr int font_face = cv::FONT_HERSHEY_SIMPLEX;
    constexpr double font_scale = 0.45;
    constexpr int thickness = 1;
    int baseline = 0;
    const cv::Size text_size = cv::getTextSize(
        label,
        font_face,
        font_scale,
        thickness,
        &baseline
    );
    const int x = std::max(0, std::min(origin.x, frame.cols - text_size.width - 6));
    const int y = std::max(text_size.height + 6, origin.y);
    const cv::Rect background(
        x,
        y - text_size.height - 6,
        std::min(text_size.width + 6, frame.cols - x),
        text_size.height + baseline + 7
    );
    cv::rectangle(frame, background, color, cv::FILLED);
    cv::putText(
        frame,
        label,
        cv::Point(x + 3, y - 4),
        font_face,
        font_scale,
        cv::Scalar(20, 20, 20),
        thickness,
        cv::LINE_AA
    );
}

void drawTrackOverlay(
    cv::Mat& frame,
    const yolo::VideoFrameTracks& frame_tracks,
    const AppConfig& config
) {
    for (const auto& track : frame_tracks.tracks) {
        const auto& detection = track.detection;
        const int x1 = clampedInt(detection.x1, 0, frame.cols - 1);
        const int y1 = clampedInt(detection.y1, 0, frame.rows - 1);
        const int x2 = clampedInt(detection.x2, 0, frame.cols - 1);
        const int y2 = clampedInt(detection.y2, 0, frame.rows - 1);
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        const cv::Scalar color = colorForTrack(track.track_id);
        cv::rectangle(frame, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), color, 2);

        std::ostringstream label;
        label << "id " << track.track_id << ' ' << classLabel(config, detection.class_id)
              << ' ' << std::lround(detection.score * 100.0F) << '%';
        drawFilledLabel(frame, label.str(), cv::Point(x1, y1), color);
    }

    std::ostringstream header;
    header << "frame " << frame_tracks.frame_index
           << " | " << frame_tracks.tracks_source
           << " | tracks " << frame_tracks.tracks.size();
    if (frame_tracks.corrected_from_frame_index >= 0) {
        header << " | replay " << frame_tracks.corrected_from_frame_index
               << "+" << frame_tracks.correction_latency_frames;
    }
    drawFilledLabel(frame, header.str(), cv::Point(12, 28), cv::Scalar(245, 245, 245));
}

bool openVideoWriter(
    cv::VideoWriter& writer,
    const std::filesystem::path& output_path,
    double fps,
    cv::Size frame_size,
    std::string& codec_name
) {
    const std::string extension = output_path.extension().string();
    std::vector<std::pair<int, std::string>> candidates;
    if (extension == ".mp4" || extension == ".m4v") {
        candidates.emplace_back(cv::VideoWriter::fourcc('a', 'v', 'c', '1'), "avc1");
        candidates.emplace_back(cv::VideoWriter::fourcc('H', '2', '6', '4'), "H264");
        candidates.emplace_back(cv::VideoWriter::fourcc('m', 'p', '4', 'v'), "mp4v");
    } else if (extension == ".avi") {
        candidates.emplace_back(cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), "MJPG");
        candidates.emplace_back(cv::VideoWriter::fourcc('X', 'V', 'I', 'D'), "XVID");
    } else {
        candidates.emplace_back(cv::VideoWriter::fourcc('m', 'p', '4', 'v'), "mp4v");
        candidates.emplace_back(cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), "MJPG");
    }

    for (const auto& [fourcc, name] : candidates) {
        writer.open(output_path.string(), fourcc, fps, frame_size);
        if (writer.isOpened()) {
            codec_name = name;
            return true;
        }
    }
    return false;
}

void writeVisualizedVideo(
    const std::filesystem::path& source_video,
    const yolo::VideoInferResult& result,
    const AppConfig& config,
    const std::filesystem::path& output_path
) {
    cv::VideoCapture capture(source_video.string());
    if (!capture.isOpened()) {
        fail("Cannot reopen video for visualization: " + source_video.string());
    }

    cv::Mat frame;
    if (!capture.read(frame) || frame.empty()) {
        fail("Cannot read first frame for visualization: " + source_video.string());
    }

    double fps = capture.get(cv::CAP_PROP_FPS);
    if (!(fps > 0.0)) {
        fps = result.source_fps > 0.0 ? result.source_fps : 30.0;
    }

    std::error_code ec;
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path(), ec);
        if (ec) {
            fail("Cannot create output directory: " + output_path.parent_path().string());
        }
    }

    cv::VideoWriter writer;
    std::string codec_name;
    if (!openVideoWriter(writer, output_path, fps, frame.size(), codec_name)) {
        fail("Cannot create visualization video: " + output_path.string());
    }

    size_t written = 0;
    do {
        if (written >= result.frames.size()) {
            break;
        }
        drawTrackOverlay(frame, result.frames[written], config);
        writer.write(frame);
        ++written;
    } while (capture.read(frame) && !frame.empty());

    writer.release();
    if (written != result.frames.size()) {
        fail("Visualization frame count mismatch: wrote " + std::to_string(written)
             + ", result has " + std::to_string(result.frames.size()));
    }

    std::cout << "visualization_video path=" << output_path
              << " codec=" << codec_name
              << " frames=" << written
              << " fps=" << fps << '\n';
}

AppConfig makeCompareConfig(
    const std::filesystem::path& high_model,
    const std::filesystem::path& low_model
) {
    AppConfig config;
    config.model_path = high_model.string();
    config.input_width = 1280;
    config.input_height = 736;
    config.conf_threshold = 0.25F;
    config.iou_threshold = 0.45F;
    config.low_res_model_path = low_model.string();
    config.low_res_input_width = 640;
    config.low_res_input_height = 384;
    config.low_res_conf_threshold = 0.25F;
    config.low_res_iou_threshold = config.iou_threshold;
    config.num_classes = 10;
    config.thread_num = 4;
    config.use_letterbox = true;
    config.video_detect_fps = 4.0F;
    config.video_stride_mode = "dynamic";
    config.video_model_async = envBool("YOLO_COMPARE_ASYNC", true);
    config.video_onnx_async = config.video_model_async;
    return config;
}

VideoMetrics collectMetrics(const yolo::VideoInferResult& result) {
    VideoMetrics metrics;
    metrics.frames = result.frame_count;
    metrics.processed = result.processed_frame_count;
    metrics.detected = result.detected_frame_count;
    metrics.forced = result.forced_detection_count;
    metrics.skipped = result.skipped_detection_count;
    metrics.elapsed_ms = result.total_elapsed_ms;
    metrics.model_ms = result.model_inference_ms;

    std::set<int> unique_track_ids;
    for (const auto& frame : result.frames) {
        metrics.track_observations += frame.tracks.size();
        for (const auto& track : frame.tracks) {
            unique_track_ids.insert(track.track_id);
        }
    }
    metrics.unique_tracks = unique_track_ids.size();
    return metrics;
}

std::string outputShapesText(const std::vector<std::vector<int64_t>>& shapes) {
    std::string text;
    for (const auto& shape : shapes) {
        if (!text.empty()) {
            text += ",";
        }
        text += "[";
        for (size_t i = 0; i < shape.size(); ++i) {
            if (i > 0) {
                text += "x";
            }
            text += std::to_string(shape[i]);
        }
        text += "]";
    }
    return text.empty() ? "[]" : text;
}

std::map<std::string, int64_t> trackSourceCounts(const yolo::VideoInferResult& result) {
    std::map<std::string, int64_t> counts;
    for (const auto& frame : result.frames) {
        ++counts[frame.tracks_source];
    }
    return counts;
}

void printMetrics(const std::string& name, const VideoMetrics& metrics) {
    const double avg_tracks = metrics.frames > 0
        ? static_cast<double>(metrics.track_observations) / static_cast<double>(metrics.frames)
        : 0.0;

    std::cout << name
              << " frames=" << metrics.frames
              << " processed=" << metrics.processed
              << " detected=" << metrics.detected
              << " forced=" << metrics.forced
              << " skipped=" << metrics.skipped
              << " track_observations=" << metrics.track_observations
              << " unique_tracks=" << metrics.unique_tracks
              << " avg_tracks_per_frame=" << avg_tracks
              << " elapsed_ms=" << metrics.elapsed_ms
              << " model_ms=" << metrics.model_ms << '\n';
}

void printDetailedMetrics(
    const std::string& name,
    const yolo::VideoInferResult& result,
    const VideoMetrics& metrics
) {
    const double avg_tracks = metrics.frames > 0
        ? static_cast<double>(metrics.track_observations) / static_cast<double>(metrics.frames)
        : 0.0;
    const double profiled_ms = result.decode_ms
        + result.preprocess_ms
        + result.infer_ms
        + result.postprocess_ms
        + result.tracker_ms
        + result.queue_wait_ms
        + result.optical_flow_ms;
    const double other_ms = result.total_elapsed_ms > profiled_ms
        ? result.total_elapsed_ms - profiled_ms
        : 0.0;

    std::cout << name << "_detail\n";
    std::cout << "  frames=" << result.frame_count
              << " source_frames=" << result.source_frame_count
              << " display_frames=" << result.display_frame_count
              << " size=" << result.width << "x" << result.height
              << " source_fps=" << result.source_fps
              << " target_detect_fps=" << result.target_detect_fps
              << " effective_detect_fps=" << result.effective_detect_fps
              << " frame_stride=" << result.frame_stride
              << " stride_mode=" << result.stride_mode
              << " model_async=" << (result.model_async ? "true" : "false") << '\n';
    std::cout << "  stride base=" << result.base_frame_stride
              << " min=" << result.min_frame_stride_used
              << " max=" << result.max_frame_stride_used
              << " final=" << result.final_frame_stride << '\n';
    std::cout << "  detection processed=" << result.processed_frame_count
              << " detected_frames=" << result.detected_frame_count
              << " forced=" << result.forced_detection_count
              << " scheduled=" << result.scheduled_detection_count
              << " skipped=" << result.skipped_detection_count
              << " async_requests=" << result.async_infer_request_count
              << " async_corrections=" << result.async_correction_count
              << " async_corrected_frames=" << result.async_corrected_frame_count
              << " weak_tracked_frames=" << result.weak_tracked_frame_count
              << " interpolated_frames=" << result.interpolated_frame_count
              << " empty_frames=" << result.empty_frame_count << '\n';
    std::cout << "  tracks observations=" << metrics.track_observations
              << " unique=" << metrics.unique_tracks
              << " avg_per_frame=" << avg_tracks << '\n';
    std::cout << "  model infer_samples=" << result.timing_samples.infer_ms.size()
              << " preprocess_samples=" << result.timing_samples.preprocess_ms.size()
              << " end_to_end_samples=" << result.timing_samples.end_to_end_ms.size()
              << " output_shapes=" << outputShapesText(result.output_shapes) << '\n';
    std::cout << "  timing_total_ms total=" << result.total_elapsed_ms
              << " decode=" << result.decode_ms
              << " preprocess=" << result.preprocess_ms
              << " infer=" << result.infer_ms
              << " model_inference=" << result.model_inference_ms
              << " postprocess=" << result.postprocess_ms
              << " tracker=" << result.tracker_ms
              << " optical_flow=" << result.optical_flow_ms
              << " queue_wait=" << result.queue_wait_ms
              << " tracking_postprocess=" << result.tracking_postprocess_ms
              << " profiled=" << profiled_ms
              << " other=" << other_ms << '\n';
    std::cout << "  timing_ratio preprocess="
              << (profiled_ms > 0.0 ? result.preprocess_ms / profiled_ms : 0.0)
              << " infer=" << (profiled_ms > 0.0 ? result.infer_ms / profiled_ms : 0.0)
              << " decode=" << (profiled_ms > 0.0 ? result.decode_ms / profiled_ms : 0.0)
              << " postprocess="
              << (profiled_ms > 0.0 ? result.postprocess_ms / profiled_ms : 0.0)
              << " tracker=" << (profiled_ms > 0.0 ? result.tracker_ms / profiled_ms : 0.0)
              << " optical_flow="
              << (profiled_ms > 0.0 ? result.optical_flow_ms / profiled_ms : 0.0)
              << " queue_wait="
              << (profiled_ms > 0.0 ? result.queue_wait_ms / profiled_ms : 0.0)
              << '\n';
    std::cout << "  latency_p50_ms decode=" << result.metrics.decode_percentiles_ms.p50
              << " preprocess=" << result.metrics.preprocess_percentiles_ms.p50
              << " infer=" << result.metrics.infer_percentiles_ms.p50
              << " postprocess=" << result.metrics.postprocess_percentiles_ms.p50
              << " tracker=" << result.metrics.tracker_percentiles_ms.p50
              << " queue_wait=" << result.metrics.queue_wait_percentiles_ms.p50
              << " end_to_end=" << result.metrics.end_to_end_percentiles_ms.p50 << '\n';
    std::cout << "  latency_p95_ms decode=" << result.metrics.decode_percentiles_ms.p95
              << " preprocess=" << result.metrics.preprocess_percentiles_ms.p95
              << " infer=" << result.metrics.infer_percentiles_ms.p95
              << " postprocess=" << result.metrics.postprocess_percentiles_ms.p95
              << " tracker=" << result.metrics.tracker_percentiles_ms.p95
              << " queue_wait=" << result.metrics.queue_wait_percentiles_ms.p95
              << " end_to_end=" << result.metrics.end_to_end_percentiles_ms.p95 << '\n';
    std::cout << "  latency_p99_ms decode=" << result.metrics.decode_percentiles_ms.p99
              << " preprocess=" << result.metrics.preprocess_percentiles_ms.p99
              << " infer=" << result.metrics.infer_percentiles_ms.p99
              << " postprocess=" << result.metrics.postprocess_percentiles_ms.p99
              << " tracker=" << result.metrics.tracker_percentiles_ms.p99
              << " queue_wait=" << result.metrics.queue_wait_percentiles_ms.p99
              << " end_to_end=" << result.metrics.end_to_end_percentiles_ms.p99 << '\n';
    std::cout << "  runtime average_fps=" << result.metrics.average_fps
              << " cpu_utilization_percent=" << result.metrics.cpu_utilization_percent
              << " rss_memory_mb=" << result.metrics.rss_memory_mb
              << " queue_length=" << result.metrics.queue_length
              << " max_queue_length=" << result.metrics.max_queue_length
              << " dropped_frames=" << result.metrics.dropped_frame_count << '\n';

    std::cout << "  track_sources";
    for (const auto& [source, count] : trackSourceCounts(result)) {
        std::cout << ' ' << source << '=' << count;
    }
    std::cout << '\n';
}

void printComparison(
    const VideoMetrics& single_metrics,
    const VideoMetrics& high_low_metrics,
    const yolo::VideoInferResult& single_result,
    const yolo::VideoInferResult& high_low_result
) {
    auto pct = [](double baseline, double candidate) {
        return baseline != 0.0 ? (candidate - baseline) * 100.0 / baseline : 0.0;
    };

    std::cout << "comparison high_low_vs_single\n";
    std::cout << "  elapsed_delta_ms="
              << high_low_result.total_elapsed_ms - single_result.total_elapsed_ms
              << " elapsed_delta_percent="
              << pct(single_result.total_elapsed_ms, high_low_result.total_elapsed_ms)
              << " fps_delta="
              << high_low_result.metrics.average_fps - single_result.metrics.average_fps
              << '\n';
    std::cout << "  infer_delta_ms="
              << high_low_result.infer_ms - single_result.infer_ms
              << " model_inference_delta_ms="
              << high_low_result.model_inference_ms - single_result.model_inference_ms
              << " preprocess_delta_ms="
              << high_low_result.preprocess_ms - single_result.preprocess_ms
              << " tracker_delta_ms="
              << high_low_result.tracker_ms - single_result.tracker_ms
              << " optical_flow_delta_ms="
              << high_low_result.optical_flow_ms - single_result.optical_flow_ms
              << '\n';
    std::cout << "  processed_delta="
              << high_low_result.processed_frame_count - single_result.processed_frame_count
              << " detected_frame_delta="
              << high_low_result.detected_frame_count - single_result.detected_frame_count
              << " track_observation_delta="
              << static_cast<int64_t>(high_low_metrics.track_observations)
                    - static_cast<int64_t>(single_metrics.track_observations)
              << " unique_track_delta="
              << static_cast<int64_t>(high_low_metrics.unique_tracks)
                    - static_cast<int64_t>(single_metrics.unique_tracks)
              << '\n';
}

}  // namespace

int main() {
    const std::filesystem::path root = repoRoot();
    const std::filesystem::path high_model =
        root / "yolo_onnx_cpp" / "deploy" / "best.onnx";
    const std::filesystem::path low_model =
        root / "yolo_onnx_cpp" / "deploy" / "best_640x384.onnx";
    const std::filesystem::path source_video = sourceVideoFromEnvOrDefault(root);

    if (!hasRequiredFiles(high_model, low_model, source_video)) {
        return 0;
    }

    const int max_frames = envInt("YOLO_COMPARE_MAX_FRAMES", 10);
    const std::filesystem::path short_video = makeShortVideo(source_video, max_frames);
    if (short_video.empty()) {
        return 0;
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "video_inference_compare_test source_video=" << source_video
              << " sample_video=" << short_video
              << " max_frames=" << max_frames
              << " high_res_async="
              << (envBool("YOLO_COMPARE_ASYNC", true) ? "true" : "false")
              << '\n';

    const AppConfig config = makeCompareConfig(high_model, low_model);
    const AppConfig low_res_config = makeLowResAppConfig(config);
    const auto high_engine = std::make_shared<YoloEngine>(config);
    const auto low_engine = std::make_shared<YoloEngine>(low_res_config);

    const yolo::VideoInferResult single_result =
        yolo::inferVideoFile(high_engine, config, short_video);
    const yolo::VideoInferResult high_low_result =
        yolo::inferVideoFileHighLow(high_engine, low_engine, config, short_video);

    const VideoMetrics single_metrics = collectMetrics(single_result);
    const VideoMetrics high_low_metrics = collectMetrics(high_low_result);
    printMetrics("single_infer", single_metrics);
    printMetrics("high_low_infer", high_low_metrics);
    printDetailedMetrics("single_infer", single_result, single_metrics);
    printDetailedMetrics("high_low_infer", high_low_result, high_low_metrics);
    printComparison(single_metrics, high_low_metrics, single_result, high_low_result);

    const std::string output_video = envString("YOLO_COMPARE_OUTPUT_VIDEO");
    if (!output_video.empty()) {
        writeVisualizedVideo(short_video, high_low_result, config, output_video);
    }

    expect(single_result.frame_count == high_low_result.frame_count, "frame_count mismatch");
    expect(single_result.source_frame_count == high_low_result.source_frame_count,
           "source_frame_count mismatch");
    expect(single_result.width == high_low_result.width, "width mismatch");
    expect(single_result.height == high_low_result.height, "height mismatch");
    expect(single_result.processed_frame_count > 0, "single infer processed no frames");
    expect(high_low_result.processed_frame_count > 0, "high-low infer processed no frames");
    expect(single_metrics.track_observations > 0, "single infer produced no tracks");
    expect(high_low_metrics.track_observations > 0, "high-low infer produced no tracks");

    std::error_code ignored;
    std::filesystem::remove(short_video, ignored);
    std::cout << "video_inference_compare_test passed\n";
    return 0;
}
