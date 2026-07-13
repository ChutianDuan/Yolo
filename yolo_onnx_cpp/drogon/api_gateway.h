#pragma once

#include <cstdint>
#include <memory>

#include "config/app_config.h"
#include "model/yolo_engine.h"

namespace yolo::api {

class ApiGateway {
public:
    ApiGateway(
        std::shared_ptr<YoloEngine> engine,
        std::shared_ptr<YoloEngine> low_res_engine,
        AppConfig config
    );

    void run(uint16_t port) const;

private:
    void registerRoutes() const;

    std::shared_ptr<YoloEngine> engine_;
    std::shared_ptr<YoloEngine> low_res_engine_;
    AppConfig config_;
};

}  // namespace yolo::api
