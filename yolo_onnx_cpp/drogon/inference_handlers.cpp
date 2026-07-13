#include "inference_handlers.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "drogon/request_utils.h"
#include "drogon/response_json.h"
#include "image/image_processing.h"

namespace yolo::api {
namespace {

const drogon::HttpFile* findUploadedFile(
    const std::vector<drogon::HttpFile>& files,
    std::string_view expected_field
) {
    for (const auto& file : files) {
        if (file.getItemName() == expected_field) {
            return &file;
        }
    }
    // Preserve compatibility with clients that historically relied on the first file.
    return files.empty() ? nullptr : &files.front();
}

void respondError(
    ResponseCallback& callback,
    int code,
    const std::string& message,
    drogon::HttpStatusCode status
) {
    callback(makeJsonResponse(code, message, status));
}

std::string missingUploadMessage(std::string_view media, std::string_view field) {
    return "No " + std::string(media) + " file uploaded. Use form field name: "
        + std::string(field);
}

}  // namespace

ImageInferenceHandler::ImageInferenceHandler(
    std::string route,
    std::string upload_field,
    std::shared_ptr<YoloEngine> engine,
    AppConfig config
) : route_(std::move(route)),
    upload_field_(std::move(upload_field)),
    engine_(std::move(engine)),
    config_(std::move(config)) {}

void ImageInferenceHandler::handle(
    const drogon::HttpRequestPtr& request,
    ResponseCallback callback
) const {
    const auto request_start = std::chrono::steady_clock::now();
    const ProcessUsageSnapshot usage_start = captureProcessUsage();
    drogon::MultiPartParser parser;

    if (parser.parse(request) != 0) {
        std::cerr << "[api] " << route_ << " bad_request parse_failed elapsed_ms="
                  << elapsedMs(request_start) << '\n';
        respondError(
            callback,
            400,
            "Failed to parse multipart/form-data",
            drogon::k400BadRequest
        );
        return;
    }

    const drogon::HttpFile* file = findUploadedFile(parser.getFiles(), upload_field_);
    if (file == nullptr) {
        std::cerr << "[api] " << route_ << " bad_request missing_file elapsed_ms="
                  << elapsedMs(request_start) << '\n';
        respondError(
            callback,
            400,
            missingUploadMessage("image", upload_field_),
            drogon::k400BadRequest
        );
        return;
    }

    const std::string_view content = file->fileContent();
    std::cerr << "[api] " << route_ << " start file=\"" << file->getFileName()
              << "\" bytes=" << content.size() << '\n';

    try {
        const auto preprocess_start = std::chrono::steady_clock::now();
        const auto input = preprocessImageContent(content, config_);
        const double preprocess_ms = elapsedMs(preprocess_start);
        if (!input.has_value()) {
            std::cerr << "[api] " << route_ << " bad_request decode_failed file=\""
                      << file->getFileName() << "\" bytes=" << content.size()
                      << " elapsed_ms=" << elapsedMs(request_start) << '\n';
            respondError(
                callback,
                400,
                "Failed to decode or preprocess image",
                drogon::k400BadRequest
            );
            return;
        }

        InferResult result = engine_->infer(input.value());
        result.preprocess_ms = preprocess_ms;
        result.timing_samples.preprocess_ms.push_back(preprocess_ms);
        result.timing_samples.queue_wait_ms.push_back(0.0);
        result.end_to_end_ms = elapsedMs(request_start);
        result.timing_samples.end_to_end_ms.push_back(result.end_to_end_ms);
        const ProcessUsageSnapshot usage_end = captureProcessUsage();
        result.metrics = buildPerformanceMetrics(
            result.timing_samples,
            1,
            result.end_to_end_ms,
            usage_start,
            usage_end,
            0,
            0,
            0
        );
        std::cerr << "[api] " << route_ << " ok file=\"" << file->getFileName()
                  << "\" detections=" << result.detections.size()
                  << " average_fps=" << result.metrics.average_fps
                  << " cpu_utilization_percent="
                  << result.metrics.cpu_utilization_percent
                  << " rss_memory_mb=" << result.metrics.rss_memory_mb
                  << " elapsed_ms=" << elapsedMs(request_start) << '\n';

        callback(drogon::HttpResponse::newHttpJsonResponse(
            inferResultToJson(result, config_.class_names)
        ));
    } catch (const std::exception& e) {
        std::cerr << "[api] " << route_ << " error file=\"" << file->getFileName()
                  << "\" message=\"" << e.what() << "\" elapsed_ms="
                  << elapsedMs(request_start) << '\n';
        respondError(callback, 500, e.what(), drogon::k500InternalServerError);
    }
}

VideoInferenceHandler::VideoInferenceHandler(
    std::string route,
    std::string upload_field,
    AppConfig config,
    VideoInferRunner runner
) : route_(std::move(route)),
    upload_field_(std::move(upload_field)),
    config_(std::move(config)),
    runner_(std::move(runner)) {}

void VideoInferenceHandler::handle(
    const drogon::HttpRequestPtr& request,
    ResponseCallback callback
) const {
    const auto request_start = std::chrono::steady_clock::now();
    VideoFrameJsonOptions frame_json_options;
    std::string frame_options_error;
    if (!parseVideoFrameJsonOptions(
            request->getParameter("include_frames"),
            request->getParameter("frame_offset"),
            request->getParameter("frame_limit"),
            frame_json_options,
            frame_options_error)) {
        std::cerr << "[api] " << route_
                  << " bad_request invalid_frame_options message=\""
                  << frame_options_error << "\" elapsed_ms="
                  << elapsedMs(request_start) << '\n';
        respondError(
            callback,
            400,
            frame_options_error,
            drogon::k400BadRequest
        );
        return;
    }

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        std::cerr << "[api] " << route_
                  << " bad_request parse_failed elapsed_ms="
                  << elapsedMs(request_start) << '\n';
        respondError(
            callback,
            400,
            "Failed to parse multipart/form-data",
            drogon::k400BadRequest
        );
        return;
    }

    const drogon::HttpFile* file = findUploadedFile(parser.getFiles(), upload_field_);
    if (file == nullptr) {
        std::cerr << "[api] " << route_
                  << " bad_request missing_file elapsed_ms="
                  << elapsedMs(request_start) << '\n';
        respondError(
            callback,
            400,
            missingUploadMessage("video", upload_field_),
            drogon::k400BadRequest
        );
        return;
    }

    const std::string_view content = file->fileContent();
    const std::string extension = videoExtension(file->getFileName());
    std::cerr << "[api] " << route_ << " start file=\"" << file->getFileName()
              << "\" bytes=" << content.size()
              << " extension=\"" << extension << "\"\n";

    try {
        TempVideoFile temp_video(content, extension);
        const VideoInferResult result = runner_(temp_video.path());
        size_t track_observations = 0;
        for (const auto& frame : result.frames) {
            track_observations += frame.tracks.size();
        }
        std::cerr << "[api] " << route_ << " ok file=\"" << file->getFileName()
                  << "\" bytes=" << content.size()
                  << " width=" << result.width
                  << " height=" << result.height
                  << " frame_count=" << result.frame_count
                  << " display_frame_count=" << result.display_frame_count
                  << " processed_frame_count=" << result.processed_frame_count
                  << " detected_frame_count=" << result.detected_frame_count
                  << " track_observations=" << track_observations
                  << " tracking_status=\"" << result.tracking_status << "\""
                  << " average_fps=" << result.metrics.average_fps
                  << " cpu_utilization_percent="
                  << result.metrics.cpu_utilization_percent
                  << " rss_memory_mb=" << result.metrics.rss_memory_mb
                  << " queue_length=" << result.metrics.queue_length
                  << " dropped_frame_count=" << result.metrics.dropped_frame_count
                  << " elapsed_ms=" << elapsedMs(request_start)
                  << " profiled_ms=" << result.total_elapsed_ms << '\n';
        callback(drogon::HttpResponse::newHttpJsonResponse(
            videoInferResultToJson(result, config_.class_names, frame_json_options)
        ));
    } catch (const VideoInferError& e) {
        const bool bad_request = e.badRequest();
        std::cerr << "[api] " << route_ << ' '
                  << (bad_request ? "bad_request" : "error")
                  << " file=\"" << file->getFileName()
                  << "\" bytes=" << content.size()
                  << " message=\"" << e.what() << "\" elapsed_ms="
                  << elapsedMs(request_start) << '\n';
        respondError(
            callback,
            bad_request ? 400 : 500,
            e.what(),
            bad_request ? drogon::k400BadRequest : drogon::k500InternalServerError
        );
    } catch (const std::exception& e) {
        std::cerr << "[api] " << route_ << " error file=\"" << file->getFileName()
                  << "\" bytes=" << content.size()
                  << " message=\"" << e.what() << "\" elapsed_ms="
                  << elapsedMs(request_start) << '\n';
        respondError(callback, 500, e.what(), drogon::k500InternalServerError);
    }
}

}  // namespace yolo::api
