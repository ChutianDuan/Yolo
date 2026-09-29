#pragma once

#include <cstddef>
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

    // Calls may run concurrently on the same engine instance.
    InferResult infer(const TensorInput& input);

    // The context is copied to the result for routing out-of-order stream work.
    InferResult infer(
        const TensorInput& input, InferenceContext context
    );

    size_t maxConcurrency() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace yolo
