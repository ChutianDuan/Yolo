#pragma once

#include <cstddef>
#include <memory>

#include "config/app_config.h"
#include "inference_types.h"

namespace yolo {

// 封装模型后端、输出解码与 NMS；图像预处理由调用方完成。
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

    // 提供给调度器的并发容量；OpenVINO 下等于实际请求池大小。
    size_t maxConcurrency() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace yolo
