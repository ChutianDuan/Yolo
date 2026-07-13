#include "api_gateway.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

#include <drogon/drogon.h>

#include "drogon/api_contract.h"
#include "drogon/inference_handlers.h"
#include "video/video_inference.h"

namespace yolo::api {
namespace {

void registerPostHandler(
    const RouteContract& route,
    const std::shared_ptr<ImageInferenceHandler>& handler
) {
    drogon::app().registerHandler(
        std::string(route.path),
        [handler](const drogon::HttpRequestPtr& request,
                  ResponseCallback&& callback) {
            handler->handle(request, std::move(callback));
        },
        {drogon::Post}
    );
}

void registerPostHandler(
    const RouteContract& route,
    const std::shared_ptr<VideoInferenceHandler>& handler
) {
    drogon::app().registerHandler(
        std::string(route.path),
        [handler](const drogon::HttpRequestPtr& request,
                  ResponseCallback&& callback) {
            handler->handle(request, std::move(callback));
        },
        {drogon::Post}
    );
}

}  // namespace

ApiGateway::ApiGateway(
    std::shared_ptr<YoloEngine> engine,
    std::shared_ptr<YoloEngine> low_res_engine,
    AppConfig config
) : engine_(std::move(engine)),
    low_res_engine_(std::move(low_res_engine)),
    config_(std::move(config)) {}

void ApiGateway::run(uint16_t port) const {
    registerRoutes();

    constexpr size_t kBytesPerMegabyte = 1024 * 1024;
    const size_t max_body_size = static_cast<size_t>(config_.client_max_body_mb)
        * kBytesPerMegabyte;

    drogon::app()
        .addListener("0.0.0.0", port)
        .setThreadNum(config_.thread_num)
        .setClientMaxBodySize(max_body_size)
        .setClientMaxMemoryBodySize(max_body_size)
        .run();
}

void ApiGateway::registerRoutes() const {
    registerPostHandler(
        kImageInferenceRoute,
        std::make_shared<ImageInferenceHandler>(
            std::string(kImageInferenceRoute.path),
            std::string(kImageInferenceRoute.upload_field),
            engine_,
            config_
        )
    );

    registerPostHandler(
        kVideoInferenceRoute,
        std::make_shared<VideoInferenceHandler>(
            std::string(kVideoInferenceRoute.path),
            std::string(kVideoInferenceRoute.upload_field),
            config_,
            [engine = engine_, config = config_](const std::filesystem::path& video_path) {
                return inferVideoFile(engine, config, video_path);
            }
        )
    );

    registerPostHandler(
        kHighLowVideoInferenceRoute,
        std::make_shared<VideoInferenceHandler>(
            std::string(kHighLowVideoInferenceRoute.path),
            std::string(kHighLowVideoInferenceRoute.upload_field),
            config_,
            [engine = engine_, low_res_engine = low_res_engine_, config = config_](
                const std::filesystem::path& video_path
            ) {
                return inferVideoFileHighLow(engine, low_res_engine, config, video_path);
            }
        )
    );
}

}  // namespace yolo::api
