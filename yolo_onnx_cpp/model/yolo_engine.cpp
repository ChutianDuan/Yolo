#include "yolo_engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json/json.h>
#include <onnxruntime/onnxruntime_cxx_api.h>
#if YOLO_ENABLE_OPENVINO
#include <openvino/openvino.hpp>
#endif

#include "image/image_processing.h"

namespace yolo {
namespace {

constexpr size_t kMaxNmsCandidates = 3000;
constexpr size_t kMaxDetections = 300;

enum class ModelBackend {
    OnnxRuntime,
    OpenVino,
};

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

std::string lowerExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

ModelBackend resolveBackend(const AppConfig& config) {
    if (config.model_backend == "onnx") {
        return ModelBackend::OnnxRuntime;
    }
    if (config.model_backend == "openvino") {
        return ModelBackend::OpenVino;
    }
    return lowerExtension(config.model_path) == ".xml"
        ? ModelBackend::OpenVino
        : ModelBackend::OnnxRuntime;
}

const char* backendName(ModelBackend backend) {
    return backend == ModelBackend::OpenVino ? "openvino" : "onnxruntime";
}

#if YOLO_ENABLE_OPENVINO
std::vector<int64_t> shapeToInt64(const ov::Shape& shape) {
    std::vector<int64_t> result;
    result.reserve(shape.size());
    for (size_t dim : shape) {
        result.push_back(static_cast<int64_t>(dim));
    }
    return result;
}
#endif

}  // namespace

class YoloEngine::Impl {
public:
    explicit Impl(const AppConfig& config)
        : env_(ORT_LOGGING_LEVEL_WARNING, "yolo_api"),
          session_(nullptr),
          memory_info_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
          backend_(resolveBackend(config)),
          conf_threshold_(config.conf_threshold),
          iou_threshold_(config.iou_threshold),
          class_count_(config.num_classes > 0
                           ? config.num_classes
                           : static_cast<int>(config.class_names.size())) {
        if (class_count_ <= 0) {
            class_count_ = loadClassCount(config.model_path);
        }

        if (backend_ == ModelBackend::OpenVino) {
            initOpenVino(config);
        } else {
            initOnnxRuntime(config);
        }
    }

    InferResult infer(const TensorInput& input) {
        return backend_ == ModelBackend::OpenVino
            ? inferOpenVino(input)
            : inferOnnxRuntime(input);
    }

private:
    void initOnnxRuntime(const AppConfig& config) {
        session_options_.SetIntraOpNumThreads(config.thread_num);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        session_ = Ort::Session(env_, config.model_path.c_str(), session_options_);

        Ort::AllocatorWithDefaultOptions allocator;

        const size_t input_count = session_.GetInputCount();
        const size_t output_count = session_.GetOutputCount();
        if (input_count != 1) {
            throw std::runtime_error(
                "ONNX Runtime model must have exactly one input, got "
                + std::to_string(input_count)
            );
        }
        if (output_count == 0) {
            throw std::runtime_error("ONNX Runtime model must have at least one output");
        }

        for (size_t i = 0; i < input_count; ++i) {
            auto name = session_.GetInputNameAllocated(i, allocator);
            input_names_str_.emplace_back(name.get());
        }

        for (size_t i = 0; i < output_count; ++i) {
            auto name = session_.GetOutputNameAllocated(i, allocator);
            output_names_str_.emplace_back(name.get());
        }
        input_name_ptrs_.reserve(input_names_str_.size());
        for (const auto& name : input_names_str_) {
            input_name_ptrs_.push_back(name.c_str());
        }
        output_name_ptrs_.reserve(output_names_str_.size());
        for (const auto& name : output_names_str_) {
            output_name_ptrs_.push_back(name.c_str());
        }

        std::cout << "Model loaded: " << config.model_path << '\n';
        std::cout << "Model backend: " << backendName(backend_) << '\n';
        std::cout << "Input name: " << input_names_str_[0] << '\n';
        std::cout << "Output count: " << output_names_str_.size() << '\n';
        std::cout << "Class count: " << class_count_ << '\n';
        std::cout << "Confidence threshold: " << conf_threshold_ << '\n';
        std::cout << "IoU threshold: " << iou_threshold_ << '\n';
    }

    InferResult inferOnnxRuntime(const TensorInput& input) {
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_,
            const_cast<float*>(input.values.data()),
            input.values.size(),
            input.shape.data(),
            input.shape.size()
        );

        auto infer_start = std::chrono::steady_clock::now();
        auto output_tensors = session_.Run(
            Ort::RunOptions{nullptr},
            input_name_ptrs_.data(),
            &input_tensor,
            1,
            output_name_ptrs_.data(),
            output_name_ptrs_.size()
        );

        InferResult result;
        result.model_inference_ms = elapsedMs(infer_start);
        result.onnx_inference_ms = result.model_inference_ms;
        result.infer_ms = result.model_inference_ms;
        result.timing_samples.infer_ms.push_back(result.infer_ms);

        auto shape_start = std::chrono::steady_clock::now();
        result.output_shapes.reserve(output_tensors.size());

        for (auto& output_tensor : output_tensors) {
            auto type_info = output_tensor.GetTensorTypeAndShapeInfo();
            result.output_shapes.push_back(type_info.GetShape());
        }
        result.postprocess_ms += elapsedMs(shape_start);

        if (!output_tensors.empty()) {
            auto type_info = output_tensors[0].GetTensorTypeAndShapeInfo();
            const auto shape = type_info.GetShape();
            const float* output_data = output_tensors[0].GetTensorData<float>();
            auto decode_start = std::chrono::steady_clock::now();
            std::vector<Detection> decoded = decode(
                output_data,
                shape,
                input,
                class_count_,
                conf_threshold_
            );
            result.decode_ms = elapsedMs(decode_start);
            result.timing_samples.decode_ms.push_back(result.decode_ms);

            auto nms_start = std::chrono::steady_clock::now();
            result.detections = nonMaxSuppression(std::move(decoded), iou_threshold_);
            result.postprocess_ms += elapsedMs(nms_start);
        }

        result.timing_samples.postprocess_ms.push_back(result.postprocess_ms);
        return result;
    }

    void initOpenVino(const AppConfig& config) {
#if YOLO_ENABLE_OPENVINO
        auto model = ov_core_.read_model(config.model_path);
        ov_compiled_model_ = ov_core_.compile_model(model, config.openvino_device);
        ov_infer_request_ = ov_compiled_model_.create_infer_request();

        const auto inputs = ov_compiled_model_.inputs();
        const auto outputs = ov_compiled_model_.outputs();
        if (inputs.size() != 1 || outputs.empty()) {
            throw std::runtime_error(
                "OpenVINO model must have exactly one input and at least one output"
            );
        }
        ov_input_name_ = inputs[0].get_any_name();

        std::cout << "Model loaded: " << config.model_path << '\n';
        std::cout << "Model backend: " << backendName(backend_) << '\n';
        std::cout << "OpenVINO device: " << config.openvino_device << '\n';
        std::cout << "Input name: " << ov_input_name_ << '\n';
        std::cout << "Output count: " << outputs.size() << '\n';
        std::cout << "Class count: " << class_count_ << '\n';
        std::cout << "Confidence threshold: " << conf_threshold_ << '\n';
        std::cout << "IoU threshold: " << iou_threshold_ << '\n';
#else
        (void)config;
        throw std::runtime_error(
            "OpenVINO backend requested but yolo_api was built without "
            "YOLO_ENABLE_OPENVINO=ON"
        );
#endif
    }

    InferResult inferOpenVino(const TensorInput& input) {
#if YOLO_ENABLE_OPENVINO
        std::lock_guard<std::mutex> lock(ov_infer_mutex_);
        ov::Shape input_shape;
        input_shape.reserve(input.shape.size());
        for (int64_t dim : input.shape) {
            if (dim < 0) {
                throw std::runtime_error("OpenVINO input shape cannot contain negative dims");
            }
            input_shape.push_back(static_cast<size_t>(dim));
        }

        ov::Tensor input_tensor(ov::element::f32, input_shape);
        std::copy(input.values.begin(), input.values.end(), input_tensor.data<float>());
        ov_infer_request_.set_tensor(ov_input_name_, input_tensor);

        auto infer_start = std::chrono::steady_clock::now();
        ov_infer_request_.infer();

        InferResult result;
        result.model_inference_ms = elapsedMs(infer_start);
        result.onnx_inference_ms = result.model_inference_ms;
        result.infer_ms = result.model_inference_ms;
        result.timing_samples.infer_ms.push_back(result.infer_ms);

        auto shape_start = std::chrono::steady_clock::now();
        const auto outputs = ov_compiled_model_.outputs();
        result.output_shapes.reserve(outputs.size());
        for (size_t i = 0; i < outputs.size(); ++i) {
            result.output_shapes.push_back(
                shapeToInt64(ov_infer_request_.get_output_tensor(i).get_shape())
            );
        }
        result.postprocess_ms += elapsedMs(shape_start);

        if (!outputs.empty()) {
            const ov::Tensor output_tensor = ov_infer_request_.get_output_tensor(0);
            const float* output_data = output_tensor.data<const float>();
            const std::vector<int64_t> shape = shapeToInt64(output_tensor.get_shape());

            auto decode_start = std::chrono::steady_clock::now();
            std::vector<Detection> decoded = decode(
                output_data,
                shape,
                input,
                class_count_,
                conf_threshold_
            );
            result.decode_ms = elapsedMs(decode_start);
            result.timing_samples.decode_ms.push_back(result.decode_ms);

            auto nms_start = std::chrono::steady_clock::now();
            result.detections = nonMaxSuppression(std::move(decoded), iou_threshold_);
            result.postprocess_ms += elapsedMs(nms_start);
        }

        result.timing_samples.postprocess_ms.push_back(result.postprocess_ms);
        return result;
#else
        (void)input;
        throw std::runtime_error("OpenVINO backend is not compiled");
#endif
    }

    Ort::Env env_;
    Ort::SessionOptions session_options_;
    Ort::Session session_;
    Ort::MemoryInfo memory_info_;
#if YOLO_ENABLE_OPENVINO
    ov::Core ov_core_;
    ov::CompiledModel ov_compiled_model_;
    ov::InferRequest ov_infer_request_;
    std::mutex ov_infer_mutex_;
    std::string ov_input_name_;
#endif

    std::vector<std::string> input_names_str_;
    std::vector<std::string> output_names_str_;
    std::vector<const char*> input_name_ptrs_;
    std::vector<const char*> output_name_ptrs_;
    ModelBackend backend_ = ModelBackend::OnnxRuntime;
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
