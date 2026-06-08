#include "yolo_engine.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <json/json.h>
#include <onnxruntime/onnxruntime_cxx_api.h>

#include "image/image_processing.h"

namespace yolo {
namespace {

constexpr size_t kMaxNmsCandidates = 3000;
constexpr size_t kMaxDetections = 300;

double elapsedMs(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

int loadClassCount(const std::string& model_path) {
    const std::filesystem::path class_path =
        std::filesystem::path(model_path).parent_path() / "classes.json";

    std::ifstream file(class_path);
    if (!file.is_open()) {
        std::cerr << "Classes file not found: " << class_path << '\n';
        return 0;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        std::cerr << "Failed to parse classes file: " << errors << '\n';
        return 0;
    }

    if (root.isArray()) {
        return static_cast<int>(root.size());
    }

    if (!root.isObject()) {
        return 0;
    }

    int max_id = -1;
    for (const auto& key : root.getMemberNames()) {
        try {
            max_id = std::max(max_id, std::stoi(key));
        } catch (const std::exception&) {
            continue;
        }
    }

    return max_id + 1;
}

float boxIou(const Detection& a, const Detection& b) {
    const float inter_x1 = std::max(a.x1, b.x1);
    const float inter_y1 = std::max(a.y1, b.y1);
    const float inter_x2 = std::min(a.x2, b.x2);
    const float inter_y2 = std::min(a.y2, b.y2);

    const float inter_w = std::max(0.0F, inter_x2 - inter_x1);
    const float inter_h = std::max(0.0F, inter_y2 - inter_y1);
    const float inter_area = inter_w * inter_h;

    const float area_a = std::max(0.0F, a.x2 - a.x1) * std::max(0.0F, a.y2 - a.y1);
    const float area_b = std::max(0.0F, b.x2 - b.x1) * std::max(0.0F, b.y2 - b.y1);
    const float union_area = area_a + area_b - inter_area;

    if (union_area <= 0.0F) {
        return 0.0F;
    }
    return inter_area / union_area;
}

std::vector<Detection> nonMaxSuppression(
    std::vector<Detection> detections,
    float iou_threshold
) {
    std::sort(
        detections.begin(),
        detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.score > b.score;
        }
    );

    if (detections.size() > kMaxNmsCandidates) {
        detections.resize(kMaxNmsCandidates);
    }

    std::vector<Detection> kept;
    kept.reserve(std::min(detections.size(), kMaxDetections));
    std::vector<bool> removed(detections.size(), false);

    for (size_t i = 0; i < detections.size(); ++i) {
        if (removed[i]) {
            continue;
        }

        kept.push_back(detections[i]);
        if (kept.size() >= kMaxDetections) {
            break;
        }

        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (removed[j] || detections[i].class_id != detections[j].class_id) {
                continue;
            }
            if (boxIou(detections[i], detections[j]) > iou_threshold) {
                removed[j] = true;
            }
        }
    }

    return kept;
}

}  // namespace

class YoloEngine::Impl {
public:
    explicit Impl(const AppConfig& config)
        : env_(ORT_LOGGING_LEVEL_WARNING, "yolo_api"),
          session_(nullptr),
          conf_threshold_(config.conf_threshold),
          iou_threshold_(config.iou_threshold),
          class_count_(config.num_classes > 0
                           ? config.num_classes
                           : static_cast<int>(config.class_names.size())) {
        if (class_count_ <= 0) {
            class_count_ = loadClassCount(config.model_path);
        }

        session_options_.SetIntraOpNumThreads(config.thread_num);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        session_ = Ort::Session(env_, config.model_path.c_str(), session_options_);

        Ort::AllocatorWithDefaultOptions allocator;

        const size_t input_count = session_.GetInputCount();
        const size_t output_count = session_.GetOutputCount();

        for (size_t i = 0; i < input_count; ++i) {
            auto name = session_.GetInputNameAllocated(i, allocator);
            input_names_str_.emplace_back(name.get());
        }

        for (size_t i = 0; i < output_count; ++i) {
            auto name = session_.GetOutputNameAllocated(i, allocator);
            output_names_str_.emplace_back(name.get());
        }

        std::cout << "Model loaded: " << config.model_path << '\n';
        std::cout << "Input name: " << input_names_str_[0] << '\n';
        std::cout << "Output count: " << output_names_str_.size() << '\n';
        std::cout << "Class count: " << class_count_ << '\n';
        std::cout << "Confidence threshold: " << conf_threshold_ << '\n';
        std::cout << "IoU threshold: " << iou_threshold_ << '\n';
    }

    InferResult infer(const TensorInput& input) {
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault
        );

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            const_cast<float*>(input.values.data()),
            input.values.size(),
            input.shape.data(),
            input.shape.size()
        );

        std::vector<const char*> input_names;
        std::vector<const char*> output_names;

        for (const auto& name : input_names_str_) {
            input_names.push_back(name.c_str());
        }

        for (const auto& name : output_names_str_) {
            output_names.push_back(name.c_str());
        }

        auto onnx_start = std::chrono::steady_clock::now();
        auto output_tensors = session_.Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            output_names.size()
        );

        InferResult result;
        result.onnx_inference_ms = elapsedMs(onnx_start);
        auto postprocess_start = std::chrono::steady_clock::now();

        for (auto& output_tensor : output_tensors) {
            auto type_info = output_tensor.GetTensorTypeAndShapeInfo();
            result.output_shapes.push_back(type_info.GetShape());
        }

        if (!output_tensors.empty()) {
            auto type_info = output_tensors[0].GetTensorTypeAndShapeInfo();
            const auto shape = type_info.GetShape();
            const float* output_data = output_tensors[0].GetTensorData<float>();
            result.detections = nonMaxSuppression(
                decode(output_data, shape, input, class_count_, conf_threshold_),
                iou_threshold_
            );
        }

        result.postprocess_ms = elapsedMs(postprocess_start);
        return result;
    }

private:
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    Ort::Session session_;

    std::vector<std::string> input_names_str_;
    std::vector<std::string> output_names_str_;
    float conf_threshold_ = 0.25F;
    float iou_threshold_ = 0.45F;
    int class_count_ = 0;
};

YoloEngine::YoloEngine(const AppConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

YoloEngine::~YoloEngine() = default;

InferResult YoloEngine::infer(const TensorInput& input) {
    return impl_->infer(input);
}

}  // namespace yolo
