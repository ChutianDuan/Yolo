#pragma once

#include <memory>

#include "config/app_config.h"
#include "inference_types.h"

namespace yolo {

class YoloEngine {
public:
    explicit YoloEngine(const AppConfig& config);
    ~YoloEngine();

    YoloEngine(const YoloEngine&) = delete;
    YoloEngine& operator=(const YoloEngine&) = delete;

    InferResult infer(const TensorInput& input);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace yolo
