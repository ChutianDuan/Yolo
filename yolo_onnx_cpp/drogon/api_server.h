#pragma once

#include <cstdint>
#include <memory>

#include "config/app_config.h"
#include "model/yolo_engine.h"

namespace yolo {

void runApiServer(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    uint16_t port
);

}  // namespace yolo
