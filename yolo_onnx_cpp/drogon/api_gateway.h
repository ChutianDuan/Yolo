#pragma once

#include <cstdint>
#include <memory>

#include "config/app_config.h"
#include "model/inference_scheduler.h"
#include "drogon/video_job_executor.h"
#include "model/yolo_engine.h"
#include "stream/realtime_stream_manager.h"

namespace yolo::api {

class RequestGate;

class ApiGateway {
public:
    ApiGateway(
        std::shared_ptr<YoloEngine> engine,
        std::shared_ptr<YoloEngine> low_res_engine,
        AppConfig config
    );

    void run(uint16_t port) const;

private:
    void registerRequestGate() const;
    void registerRoutes() const;

    std::shared_ptr<YoloEngine> engine_;
    std::shared_ptr<YoloEngine> low_res_engine_;
    AppConfig config_;
    std::shared_ptr<RequestGate> request_gate_;
    std::shared_ptr<VideoJobExecutor> video_job_executor_;
    std::shared_ptr<InferenceScheduler> high_res_scheduler_;
    std::shared_ptr<InferenceScheduler> low_res_scheduler_;
    std::shared_ptr<RealtimeStreamManager> stream_manager_;
};

}  // namespace yolo::api
