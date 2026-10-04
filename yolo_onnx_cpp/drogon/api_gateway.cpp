#include "api_gateway.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include <drogon/drogon.h>

#include "drogon/api_contract.h"
#include "drogon/inference_handlers.h"
#include "drogon/request_gate.h"
#include "drogon/realtime_stream_handlers.h"
#include "drogon/response_json.h"
#include "video/video_inference.h"

namespace yolo::api {
namespace {

std::string loadBearerToken(const AppConfig& config) {
    if (config.api_bearer_token_env.empty()) {
        return {};
    }
    const char* value = std::getenv(config.api_bearer_token_env.c_str());
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(
            "API bearer token environment variable is unset or empty: "
            + config.api_bearer_token_env
        );
    }
    return value;
}

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
    config_(std::move(config)),
    request_gate_(std::make_shared<RequestGate>(
        loadBearerToken(config_),
        static_cast<double>(config_.api_rate_limit_requests_per_second),
        static_cast<size_t>(config_.api_rate_limit_burst)
    )),
    video_job_executor_(std::make_shared<VideoJobExecutor>(
        static_cast<size_t>(config_.video_job_threads),
        static_cast<size_t>(config_.video_job_queue_depth)
    )),
    high_res_scheduler_(std::make_shared<InferenceScheduler>(
        engine_,
        engine_->maxConcurrency(),
        static_cast<size_t>(config_.per_stream_queue_depth),
        std::chrono::milliseconds(config_.max_request_age_ms)
    )),
    low_res_scheduler_(low_res_engine_ != nullptr
        ? std::make_shared<InferenceScheduler>(
            low_res_engine_,
            low_res_engine_->maxConcurrency(),
            static_cast<size_t>(config_.per_stream_queue_depth),
            std::chrono::milliseconds(config_.max_request_age_ms)
        )
        : nullptr),
    stream_manager_(std::make_shared<RealtimeStreamManager>(
        engine_,
        low_res_engine_,
        high_res_scheduler_,
        low_res_scheduler_,
        config_
    )) {}

void ApiGateway::run(uint16_t port) const {
    registerRequestGate();
    registerRoutes();

    constexpr size_t kBytesPerMegabyte = 1024 * 1024;
    const size_t max_body_size = static_cast<size_t>(config_.client_max_body_mb)
        * kBytesPerMegabyte;
    const size_t max_memory_body_size =
        static_cast<size_t>(config_.client_max_memory_body_mb) * kBytesPerMegabyte;

    drogon::app()
        .addListener(
            "0.0.0.0",
            port,
            !config_.tls_certificate_path.empty(),
            config_.tls_certificate_path,
            config_.tls_private_key_path
        )
        .setThreadNum(serverIoThreadCount(config_))
        .setClientMaxBodySize(max_body_size)
        .setClientMaxMemoryBodySize(max_memory_body_size)
        .run();
}

void ApiGateway::registerRequestGate() const {
    // 在路由执行前统一鉴权和限流；健康与就绪探针可直接访问。
    const auto gate = request_gate_;
    drogon::app().registerPreRoutingAdvice(
        [gate](const drogon::HttpRequestPtr& request,
               drogon::AdviceCallback&& callback,
               drogon::AdviceChainCallback&& chain_callback) {
            const std::string& path = request->path();
            if (path == "/health" || path == "/ready") {
                chain_callback();
                return;
            }

            const RequestGateDecision decision = gate->check(
                request->getHeader("Authorization"),
                request->peerAddr().toIp()
            );
            if (decision.status == RequestGateStatus::Allowed) {
                chain_callback();
                return;
            }

            drogon::HttpResponsePtr response;
            if (decision.status == RequestGateStatus::Unauthorized) {
                response = makeJsonResponse(
                    401, "missing or invalid bearer token",
                    drogon::k401Unauthorized
                );
                response->addHeader("WWW-Authenticate", "Bearer");
            } else {
                response = makeJsonResponse(
                    429, "request rate limit exceeded",
                    drogon::k429TooManyRequests
                );
                response->addHeader(
                    "Retry-After",
                    std::to_string(decision.retry_after_seconds)
                );
            }
            callback(response);
        }
    );
}

void ApiGateway::registerRoutes() const {
    // 视频上传与实时流共享模型调度器，统一约束引擎的并发与排队。
    registerRealtimeStreamRoutes(
        stream_manager_,
        high_res_scheduler_,
        low_res_scheduler_,
        video_job_executor_,
        request_gate_,
        config_.class_names
    );

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
            video_job_executor_,
            [engine = engine_, scheduler = high_res_scheduler_, config = config_](
                const std::filesystem::path& video_path,
                const std::string& stream_id
            ) {
                return inferVideoFile(
                    engine, config, video_path, scheduler, stream_id
                );
            }
        )
    );

    registerPostHandler(
        kHighLowVideoInferenceRoute,
        std::make_shared<VideoInferenceHandler>(
            std::string(kHighLowVideoInferenceRoute.path),
            std::string(kHighLowVideoInferenceRoute.upload_field),
            config_,
            video_job_executor_,
            [engine = engine_, low_res_engine = low_res_engine_,
             high_scheduler = high_res_scheduler_,
             low_scheduler = low_res_scheduler_, config = config_](
                const std::filesystem::path& video_path,
                const std::string& stream_id
            ) {
                return inferVideoFileHighLow(
                    engine, low_res_engine, config, video_path,
                    high_scheduler, low_scheduler, stream_id
                );
            }
        )
    );
}

}  // namespace yolo::api
