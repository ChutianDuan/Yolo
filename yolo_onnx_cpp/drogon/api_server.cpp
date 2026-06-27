#include "api_server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <drogon/drogon.h>

#include "drogon/response_json.h"
#include "image/image_processing.h"
#include "video/video_inference.h"

namespace yolo {
namespace {

std::string invalidImageMessage(const AppConfig& config) {
    (void)config;
    return "Failed to decode or preprocess image";
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool parseBoolParameter(
    const std::string& key,
    const std::string& value,
    bool& output,
    std::string& error_message
) {
    const std::string parsed = lowerAscii(value);
    if (parsed == "true" || parsed == "1" || parsed == "yes") {
        output = true;
        return true;
    }
    if (parsed == "false" || parsed == "0" || parsed == "no") {
        output = false;
        return true;
    }

    error_message = "Invalid " + key + ": " + value;
    return false;
}

bool parseSizeParameter(
    const std::string& key,
    const std::string& value,
    bool allow_zero,
    size_t& output,
    std::string& error_message
) {
    if (value.empty() || value.front() == '-' || value.front() == '+') {
        error_message = "Invalid " + key + ": " + value;
        return false;
    }

    try {
        size_t parsed = 0;
        const unsigned long long result = std::stoull(value, &parsed);
        if (parsed != value.size()) {
            throw std::invalid_argument("trailing characters");
        }
        if (result > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
            throw std::out_of_range("too large");
        }
        if (!allow_zero && result == 0ULL) {
            error_message = key + " must be positive";
            return false;
        }

        output = static_cast<size_t>(result);
        return true;
    } catch (const std::exception&) {
        error_message = "Invalid " + key + ": " + value;
        return false;
    }
}

bool parseVideoFrameJsonOptions(
    const drogon::HttpRequestPtr& req,
    VideoFrameJsonOptions& options,
    std::string& error_message
) {
    const std::string include_frames = req->getParameter("include_frames");
    if (!include_frames.empty()
        && !parseBoolParameter("include_frames", include_frames, options.include_frames, error_message)) {
        return false;
    }

    const std::string frame_offset = req->getParameter("frame_offset");
    if (!frame_offset.empty()
        && !parseSizeParameter("frame_offset", frame_offset, true, options.frame_offset, error_message)) {
        return false;
    }

    const std::string frame_limit = req->getParameter("frame_limit");
    if (!frame_limit.empty()
        && !parseSizeParameter("frame_limit", frame_limit, false, options.frame_limit, error_message)) {
        return false;
    }

    return true;
}

double elapsedMs(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
    ) / 1000.0;
}

std::string videoExtension(const drogon::HttpFile& file) {
    const std::string extension = std::filesystem::path(file.getFileName()).extension().string();
    if (extension.empty() || extension.size() > 16) {
        return ".mp4";
    }
    return extension;
}

std::filesystem::path makeTempVideoPath(const std::string& extension) {
    static std::atomic<uint64_t> next_id{0};

    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    const uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);

    return std::filesystem::temp_directory_path()
        / ("yolo_video_" + std::to_string(micros) + "_" + std::to_string(id) + extension);
}

class TempVideoFile {
public:
    TempVideoFile(std::string_view content, const std::string& extension)
        : path_(makeTempVideoPath(extension)) {
        std::ofstream output(path_, std::ios::binary);
        if (!output.is_open()) {
            throw std::runtime_error("Failed to create temp video file");
        }

        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output.good()) {
            throw std::runtime_error("Failed to write temp video file");
        }
    }

    ~TempVideoFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void registerInferHandler(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config
) {
    drogon::app().registerHandler(
        "/infer",
        [engine, config](const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto request_start = std::chrono::steady_clock::now();
            drogon::MultiPartParser parser;

            if (parser.parse(req) != 0) {
                std::cerr << "[api] /infer bad_request parse_failed elapsed_ms="
                          << elapsedMs(request_start) << '\n';
                callback(makeJsonResponse(
                    400,
                    "Failed to parse multipart/form-data",
                    drogon::k400BadRequest
                ));
                return;
            }

            const auto& files = parser.getFiles();

            if (files.empty()) {
                std::cerr << "[api] /infer bad_request missing_file elapsed_ms="
                          << elapsedMs(request_start) << '\n';
                callback(makeJsonResponse(
                    400,
                    "No image file uploaded. Use form field name: image",
                    drogon::k400BadRequest
                ));
                return;
            }

            const auto& file = files[0];
            const auto content = file.fileContent();
            std::cerr << "[api] /infer start file=\"" << file.getFileName()
                      << "\" bytes=" << content.size() << '\n';

            try {
                const auto input = preprocessImageContent(content, config);
                if (!input.has_value()) {
                    std::cerr << "[api] /infer bad_request decode_failed file=\""
                              << file.getFileName() << "\" bytes=" << content.size()
                              << " elapsed_ms=" << elapsedMs(request_start) << '\n';
                    callback(makeJsonResponse(
                        400,
                        invalidImageMessage(config),
                        drogon::k400BadRequest
                    ));
                    return;
                }

                const InferResult result = engine->infer(input.value());
                std::cerr << "[api] /infer ok file=\"" << file.getFileName()
                          << "\" detections=" << result.detections.size()
                          << " elapsed_ms=" << elapsedMs(request_start) << '\n';

                callback(drogon::HttpResponse::newHttpJsonResponse(
                    inferResultToJson(result, config.class_names)
                ));
            } catch (const std::exception& e) {
                std::cerr << "[api] /infer error file=\"" << file.getFileName()
                          << "\" message=\"" << e.what() << "\" elapsed_ms="
                          << elapsedMs(request_start) << '\n';
                callback(makeJsonResponse(
                    500,
                    e.what(),
                    drogon::k500InternalServerError
                ));
            }
        },
        {drogon::Post}
    );
}

void registerVideoInferHandler(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config
) {
    drogon::app().registerHandler(
        "/infer_video",
        [engine, config](const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto request_start = std::chrono::steady_clock::now();
            VideoFrameJsonOptions frame_json_options;
            std::string frame_options_error;
            if (!parseVideoFrameJsonOptions(req, frame_json_options, frame_options_error)) {
                std::cerr << "[api] /infer_video bad_request invalid_frame_options message=\""
                          << frame_options_error << "\" elapsed_ms="
                          << elapsedMs(request_start) << '\n';
                callback(makeJsonResponse(
                    400,
                    frame_options_error,
                    drogon::k400BadRequest
                ));
                return;
            }

            drogon::MultiPartParser parser;

            if (parser.parse(req) != 0) {
                std::cerr << "[api] /infer_video bad_request parse_failed elapsed_ms="
                          << elapsedMs(request_start) << '\n';
                callback(makeJsonResponse(
                    400,
                    "Failed to parse multipart/form-data",
                    drogon::k400BadRequest
                ));
                return;
            }

            const auto& files = parser.getFiles();

            if (files.empty()) {
                std::cerr << "[api] /infer_video bad_request missing_file elapsed_ms="
                          << elapsedMs(request_start) << '\n';
                callback(makeJsonResponse(
                    400,
                    "No video file uploaded. Use form field name: video",
                    drogon::k400BadRequest
                ));
                return;
            }

            const auto& file = files[0];
            const auto content = file.fileContent();
            const std::string extension = videoExtension(file);
            std::cerr << "[api] /infer_video start file=\"" << file.getFileName()
                      << "\" bytes=" << content.size()
                      << " extension=\"" << extension << "\"\n";

            try {
                TempVideoFile temp_video(content, extension);
                const VideoInferResult result = inferVideoFile(engine, config, temp_video.path());
                size_t track_observations = 0;
                for (const auto& frame : result.frames) {
                    track_observations += frame.tracks.size();
                }
                std::cerr << "[api] /infer_video ok file=\"" << file.getFileName()
                          << "\" bytes=" << content.size()
                          << " width=" << result.width
                          << " height=" << result.height
                          << " frame_count=" << result.frame_count
                          << " display_frame_count=" << result.display_frame_count
                          << " processed_frame_count=" << result.processed_frame_count
                          << " detected_frame_count=" << result.detected_frame_count
                          << " track_observations=" << track_observations
                          << " tracking_status=\"" << result.tracking_status << "\""
                          << " elapsed_ms=" << elapsedMs(request_start)
                          << " profiled_ms=" << result.total_elapsed_ms << '\n';
                callback(drogon::HttpResponse::newHttpJsonResponse(
                    videoInferResultToJson(result, config.class_names, frame_json_options)
                ));
            } catch (const VideoInferError& e) {
                const bool bad_request = e.badRequest();
                std::cerr << "[api] /infer_video "
                          << (bad_request ? "bad_request" : "error")
                          << " file=\"" << file.getFileName()
                          << "\" bytes=" << content.size()
                          << " message=\"" << e.what() << "\" elapsed_ms="
                          << elapsedMs(request_start) << '\n';
                callback(makeJsonResponse(
                    bad_request ? 400 : 500,
                    e.what(),
                    bad_request ? drogon::k400BadRequest : drogon::k500InternalServerError
                ));
            } catch (const std::exception& e) {
                std::cerr << "[api] /infer_video error file=\"" << file.getFileName()
                          << "\" bytes=" << content.size()
                          << " message=\"" << e.what() << "\" elapsed_ms="
                          << elapsedMs(request_start) << '\n';
                callback(makeJsonResponse(
                    500,
                    e.what(),
                    drogon::k500InternalServerError
                ));
            }
        },
        {drogon::Post}
    );
}

}  // namespace

void runApiServer(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    uint16_t port
) {
    registerInferHandler(engine, config);
    registerVideoInferHandler(engine, config);

    constexpr size_t kBytesPerMegabyte = 1024 * 1024;
    const size_t max_body_size = static_cast<size_t>(config.client_max_body_mb)
        * kBytesPerMegabyte;

    drogon::app()
        .addListener("0.0.0.0", port)
        .setThreadNum(config.thread_num)
        .setClientMaxBodySize(max_body_size)
        .setClientMaxMemoryBodySize(max_body_size)
        .run();
}

}  // namespace yolo
