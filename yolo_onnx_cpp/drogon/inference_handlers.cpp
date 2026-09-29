#include "inference_handlers.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

#include "drogon/request_utils.h"
#include "drogon/response_json.h"
#include "image/image_processing.h"
#include "metrics/api_logging.h"

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
    drogon::HttpStatusCode status,
    const std::string& request_id
) {
    auto response = makeJsonResponse(code, message, status);
    response->addHeader("X-Request-ID", request_id);
    callback(response);
}

void respondSuccess(
    ResponseCallback& callback,
    drogon::HttpResponsePtr response,
    const std::string& request_id
) {
    response->addHeader("X-Request-ID", request_id);
    callback(response);
}

ApiLogRecord requestLog(
    const std::string& request_id,
    const std::string& route,
    const char* media_type,
    std::chrono::steady_clock::time_point request_start
) {
    ApiLogRecord record;
    record.request_id = request_id;
    record.route = route;
    record.media_type = media_type;
    record.elapsed_ms = elapsedMs(request_start);
    return record;
}

std::string missingUploadMessage(std::string_view media, std::string_view field) {
    return "No " + std::string(media) + " file uploaded. Use form field name: "
        + std::string(field);
}

void runVideoJob(
    const std::string& route,
    size_t content_size,
    const std::shared_ptr<TempVideoFile>& temp_video,
    const std::string& stream_id,
    const std::string& request_id,
    const VideoFrameJsonOptions& frame_json_options,
    const VideoInferRunner& runner,
    const std::vector<std::string>& class_names,
    ResponseCallback callback,
    std::chrono::steady_clock::time_point request_start
) {
    try {
        const VideoInferResult result = runner(temp_video->path(), stream_id);
        size_t track_observations = 0;
        for (const auto& frame : result.frames) {
            track_observations += frame.tracks.size();
        }

        auto response = drogon::HttpResponse::newHttpJsonResponse(
            videoInferResultToJson(result, class_names, frame_json_options)
        );
        ApiLogRecord log = requestLog(request_id, route, "video", request_start);
        log.stream_id = stream_id;
        log.status_code = 200;
        log.content_size = content_size;
        log.has_content_size = true;
        log.frame_count = result.frame_count;
        log.processed_frame_count = result.processed_frame_count;
        log.detected_frame_count = result.detected_frame_count;
        log.track_observation_count = track_observations;
        log.average_fps = result.metrics.average_fps;
        log.cpu_utilization_percent = result.metrics.cpu_utilization_percent;
        log.rss_memory_mb = result.metrics.rss_memory_mb;
        logApiEvent(ApiLogEvent::Completed, log);
        respondSuccess(callback, std::move(response), request_id);
    } catch (const VideoInferError& error) {
        const bool bad_request = error.badRequest();
        ApiLogRecord log = requestLog(request_id, route, "video", request_start);
        log.stream_id = stream_id;
        log.reason_code = bad_request ? "invalid_video" : "video_inference_failed";
        log.status_code = bad_request ? 400 : 500;
        log.content_size = content_size;
        log.has_content_size = true;
        logApiEvent(bad_request ? ApiLogEvent::Rejected : ApiLogEvent::Failed, log);
        respondError(
            callback,
            bad_request ? 400 : 500,
            error.what(),
            bad_request ? drogon::k400BadRequest : drogon::k500InternalServerError,
            request_id
        );
    } catch (const std::exception& error) {
        ApiLogRecord log = requestLog(request_id, route, "video", request_start);
        log.stream_id = stream_id;
        log.reason_code = "video_job_failed";
        log.status_code = 500;
        log.content_size = content_size;
        log.has_content_size = true;
        logApiEvent(ApiLogEvent::Failed, log);
        respondError(
            callback, 500, error.what(), drogon::k500InternalServerError, request_id
        );
    }
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
    const std::string request_id = nextApiRequestId();
    logApiEvent(
        ApiLogEvent::Started,
        requestLog(request_id, route_, "image", request_start)
    );
    const ProcessUsageSnapshot usage_start = captureProcessUsage();
    drogon::MultiPartParser parser;

    if (parser.parse(request) != 0) {
        ApiLogRecord log = requestLog(request_id, route_, "image", request_start);
        log.reason_code = "multipart_parse_failed";
        log.status_code = 400;
        logApiEvent(ApiLogEvent::Rejected, log);
        respondError(
            callback, 400, "Failed to parse multipart/form-data",
            drogon::k400BadRequest, request_id
        );
        return;
    }

    const drogon::HttpFile* file = findUploadedFile(parser.getFiles(), upload_field_);
    if (file == nullptr) {
        ApiLogRecord log = requestLog(request_id, route_, "image", request_start);
        log.reason_code = "missing_upload";
        log.status_code = 400;
        logApiEvent(ApiLogEvent::Rejected, log);
        respondError(
            callback, 400, missingUploadMessage("image", upload_field_),
            drogon::k400BadRequest, request_id
        );
        return;
    }

    const std::string_view content = file->fileContent();
    try {
        const auto preprocess_start = std::chrono::steady_clock::now();
        const auto input = preprocessImageContent(content, config_);
        const double preprocess_ms = elapsedMs(preprocess_start);
        if (!input.has_value()) {
            ApiLogRecord log = requestLog(request_id, route_, "image", request_start);
            log.reason_code = "decode_failed";
            log.status_code = 400;
            log.content_size = content.size();
            log.has_content_size = true;
            logApiEvent(ApiLogEvent::Rejected, log);
            respondError(
                callback, 400, "Failed to decode or preprocess image",
                drogon::k400BadRequest, request_id
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

        auto response = drogon::HttpResponse::newHttpJsonResponse(
            inferResultToJson(result, config_.class_names)
        );
        ApiLogRecord log = requestLog(request_id, route_, "image", request_start);
        log.status_code = 200;
        log.content_size = content.size();
        log.has_content_size = true;
        log.detection_count = result.detections.size();
        log.average_fps = result.metrics.average_fps;
        log.cpu_utilization_percent = result.metrics.cpu_utilization_percent;
        log.rss_memory_mb = result.metrics.rss_memory_mb;
        logApiEvent(ApiLogEvent::Completed, log);
        respondSuccess(callback, std::move(response), request_id);
    } catch (const std::exception& error) {
        ApiLogRecord log = requestLog(request_id, route_, "image", request_start);
        log.reason_code = "image_inference_failed";
        log.status_code = 500;
        log.content_size = content.size();
        log.has_content_size = true;
        logApiEvent(ApiLogEvent::Failed, log);
        respondError(
            callback, 500, error.what(), drogon::k500InternalServerError, request_id
        );
    }
}

VideoInferenceHandler::VideoInferenceHandler(
    std::string route,
    std::string upload_field,
    AppConfig config,
    std::shared_ptr<VideoJobExecutor> executor,
    VideoInferRunner runner
) : route_(std::move(route)),
    upload_field_(std::move(upload_field)),
    config_(std::move(config)),
    executor_(std::move(executor)),
    runner_(std::move(runner)) {}

void VideoInferenceHandler::handle(
    const drogon::HttpRequestPtr& request,
    ResponseCallback callback
) const {
    const auto request_start = std::chrono::steady_clock::now();
    const std::string request_id = nextApiRequestId();
    static std::atomic<uint64_t> next_stream_id{1};
    std::string stream_id = request->getParameter("stream_id");
    if (stream_id.empty()) {
        stream_id = "upload-" + std::to_string(next_stream_id.fetch_add(1));
    }

    ApiLogRecord start_log = requestLog(
        request_id, route_, "video", request_start
    );
    start_log.stream_id = stream_id;
    logApiEvent(ApiLogEvent::Started, start_log);

    VideoFrameJsonOptions frame_json_options;
    std::string frame_options_error;
    if (!parseVideoFrameJsonOptions(
            request->getParameter("include_frames"),
            request->getParameter("frame_offset"),
            request->getParameter("frame_limit"),
            frame_json_options,
            frame_options_error)) {
        ApiLogRecord log = requestLog(request_id, route_, "video", request_start);
        log.stream_id = stream_id;
        log.reason_code = "invalid_frame_options";
        log.status_code = 400;
        logApiEvent(ApiLogEvent::Rejected, log);
        respondError(
            callback, 400, frame_options_error, drogon::k400BadRequest, request_id
        );
        return;
    }

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        ApiLogRecord log = requestLog(request_id, route_, "video", request_start);
        log.stream_id = stream_id;
        log.reason_code = "multipart_parse_failed";
        log.status_code = 400;
        logApiEvent(ApiLogEvent::Rejected, log);
        respondError(
            callback, 400, "Failed to parse multipart/form-data",
            drogon::k400BadRequest, request_id
        );
        return;
    }

    const drogon::HttpFile* file = findUploadedFile(parser.getFiles(), upload_field_);
    if (file == nullptr) {
        ApiLogRecord log = requestLog(request_id, route_, "video", request_start);
        log.stream_id = stream_id;
        log.reason_code = "missing_upload";
        log.status_code = 400;
        logApiEvent(ApiLogEvent::Rejected, log);
        respondError(
            callback, 400, missingUploadMessage("video", upload_field_),
            drogon::k400BadRequest, request_id
        );
        return;
    }

    const std::string_view content = file->fileContent();
    const std::string extension = videoExtension(file->getFileName());
    try {
        auto temp_video = std::make_shared<TempVideoFile>(content, extension);
        ResponseCallback job_callback = callback;
        auto job = [
            route = route_,
            content_size = content.size(),
            temp_video,
            stream_id,
            request_id,
            frame_json_options,
            runner = runner_,
            class_names = config_.class_names,
            job_callback = std::move(job_callback),
            request_start
        ]() mutable {
            runVideoJob(
                route,
                content_size,
                temp_video,
                stream_id,
                request_id,
                frame_json_options,
                runner,
                class_names,
                std::move(job_callback),
                request_start
            );
        };

        if (executor_ == nullptr || !executor_->submit(std::move(job))) {
            ApiLogRecord log = requestLog(request_id, route_, "video", request_start);
            log.stream_id = stream_id;
            log.reason_code = "video_queue_full";
            log.status_code = 503;
            log.content_size = content.size();
            log.has_content_size = true;
            logApiEvent(ApiLogEvent::Rejected, log);
            respondError(
                callback, 503, "Video inference queue is full",
                drogon::k503ServiceUnavailable, request_id
            );
        }
    } catch (const std::exception&) {
        ApiLogRecord log = requestLog(request_id, route_, "video", request_start);
        log.stream_id = stream_id;
        log.reason_code = "video_staging_failed";
        log.status_code = 500;
        log.content_size = content.size();
        log.has_content_size = true;
        logApiEvent(ApiLogEvent::Failed, log);
        respondError(
            callback, 500, "Failed to stage uploaded video", drogon::k500InternalServerError, request_id
        );
    }
}

}  // namespace yolo::api
