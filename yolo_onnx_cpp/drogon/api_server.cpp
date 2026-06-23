#include "api_server.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
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
            drogon::MultiPartParser parser;

            if (parser.parse(req) != 0) {
                callback(makeJsonResponse(
                    400,
                    "Failed to parse multipart/form-data",
                    drogon::k400BadRequest
                ));
                return;
            }

            const auto& files = parser.getFiles();

            if (files.empty()) {
                callback(makeJsonResponse(
                    400,
                    "No image file uploaded. Use form field name: image",
                    drogon::k400BadRequest
                ));
                return;
            }

            const auto content = files[0].fileContent();

            try {
                const auto input = preprocessImageContent(content, config);
                if (!input.has_value()) {
                    callback(makeJsonResponse(
                        400,
                        invalidImageMessage(config),
                        drogon::k400BadRequest
                    ));
                    return;
                }

                const InferResult result = engine->infer(input.value());

                callback(drogon::HttpResponse::newHttpJsonResponse(
                    inferResultToJson(result, config.class_names)
                ));
            } catch (const std::exception& e) {
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
            drogon::MultiPartParser parser;

            if (parser.parse(req) != 0) {
                callback(makeJsonResponse(
                    400,
                    "Failed to parse multipart/form-data",
                    drogon::k400BadRequest
                ));
                return;
            }

            const auto& files = parser.getFiles();

            if (files.empty()) {
                callback(makeJsonResponse(
                    400,
                    "No video file uploaded. Use form field name: video",
                    drogon::k400BadRequest
                ));
                return;
            }

            try {
                TempVideoFile temp_video(files[0].fileContent(), videoExtension(files[0]));
                const VideoInferResult result = inferVideoFile(engine, config, temp_video.path());
                callback(drogon::HttpResponse::newHttpJsonResponse(
                    videoInferResultToJson(result, config.class_names)
                ));
            } catch (const VideoInferError& e) {
                const bool bad_request = e.badRequest();
                callback(makeJsonResponse(
                    bad_request ? 400 : 500,
                    e.what(),
                    bad_request ? drogon::k400BadRequest : drogon::k500InternalServerError
                ));
            } catch (const std::exception& e) {
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
