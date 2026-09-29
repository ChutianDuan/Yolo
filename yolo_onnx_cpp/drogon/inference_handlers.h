#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <drogon/drogon.h>

#include "config/app_config.h"
#include "model/yolo_engine.h"
#include "drogon/video_job_executor.h"
#include "video/video_inference.h"

namespace yolo::api {

using ResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;
using VideoInferRunner = std::function<VideoInferResult(
    const std::filesystem::path&, const std::string&
)>;

class ImageInferenceHandler {
public:
    ImageInferenceHandler(
        std::string route,
        std::string upload_field,
        std::shared_ptr<YoloEngine> engine,
        AppConfig config
    );

    void handle(
        const drogon::HttpRequestPtr& request,
        ResponseCallback callback
    ) const;

private:
    std::string route_;
    std::string upload_field_;
    std::shared_ptr<YoloEngine> engine_;
    AppConfig config_;
};

class VideoInferenceHandler {
public:
    VideoInferenceHandler(
        std::string route,
        std::string upload_field,
        AppConfig config,
        std::shared_ptr<VideoJobExecutor> executor,
        VideoInferRunner runner
    );

    void handle(
        const drogon::HttpRequestPtr& request,
        ResponseCallback callback
    ) const;

private:
    std::string route_;
    std::string upload_field_;
    AppConfig config_;
    std::shared_ptr<VideoJobExecutor> executor_;
    VideoInferRunner runner_;
};

}  // namespace yolo::api
