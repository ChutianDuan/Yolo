#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

#include "config/app_config.h"
#include "image/image_processing.h"
#include "model/yolo_engine.h"

namespace {

void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

}  // namespace

int main() {
    constexpr size_t kStreamCount = 4;
    constexpr int64_t kFramesPerStream = 2;

    yolo::AppConfig config;
    config.model_path =
        (std::filesystem::path(YOLO_TEST_SOURCE_DIR) / "deploy/best_640x384.onnx").string();
    config.input_width = 640;
    config.input_height = 384;
    config.num_classes = 10;
    config.thread_num = 8;
#if YOLO_ENABLE_OPENVINO
    config.model_backend = "openvino";
    config.openvino_performance_mode = "throughput";
    config.infer_request_count = 2;
#else
    config.model_backend = "onnx";
#endif

    cv::Mat image(config.input_height, config.input_width, CV_8UC3, cv::Scalar::all(0));
    const auto input = yolo::preprocessImageMat(image, config);
    expect(input.has_value(), "failed to create the concurrent inference input");

    yolo::YoloEngine engine(config);
    std::atomic<size_t> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::vector<yolo::InferResult>> results(kStreamCount);
    std::vector<std::thread> workers;
    workers.reserve(kStreamCount);

    for (size_t stream = 0; stream < kStreamCount; ++stream) {
        workers.emplace_back([&, stream]() {
            ++ready;
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int64_t frame = 0; frame < kFramesPerStream; ++frame) {
                yolo::InferenceContext context;
                context.stream_id = "stream-" + std::to_string(stream);
                context.frame_index = frame;
                context.timestamp_ms = static_cast<double>(frame) * 40.0;
                results[stream].push_back(engine.infer(*input, std::move(context)));
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != kStreamCount) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }

    for (size_t stream = 0; stream < kStreamCount; ++stream) {
        expect(
            results[stream].size() == static_cast<size_t>(kFramesPerStream),
            "missing concurrent inference result"
        );
        for (int64_t frame = 0; frame < kFramesPerStream; ++frame) {
            const auto& result = results[stream][static_cast<size_t>(frame)];
            expect(
                result.context.stream_id == "stream-" + std::to_string(stream),
                "stream context was mixed between concurrent requests"
            );
            expect(result.context.frame_index == frame, "frame context was mixed");
            expect(result.context.timestamp_ms == static_cast<double>(frame) * 40.0,
                   "timestamp context was mixed");
            expect(!result.output_shapes.empty(), "model produced no output shape");
            expect(result.model_inference_ms >= 0.0, "invalid inference timing");
        }
    }

    std::cout << "yolo_engine_concurrency_test passed\n";
    return 0;
}
