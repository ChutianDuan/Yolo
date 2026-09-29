#include "yolo_engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json/json.h>
#include <onnxruntime/onnxruntime_cxx_api.h>
#if YOLO_ENABLE_OPENVINO
#include <openvino/openvino.hpp>
#include <openvino/core/version.hpp>
#include <openvino/runtime/properties.hpp>
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

#if YOLO_ENABLE_OPENVINO
void logOpenVinoRuntime(
    const ov::CompiledModel& model, const AppConfig& config, size_t pool_size
) noexcept {
    try {
        const auto version = ov::get_openvino_version();
        Json::Value json(Json::objectValue);
        json["schema_version"] = 1;
        json["component"] = "model_runtime";
        json["backend"] = "openvino";
        json["runtime_build"] = version.buildNumber ? version.buildNumber : "unavailable";
        json["runtime_description"] = version.description ? version.description : "unavailable";
        json["input_width"] = config.input_width;
        json["input_height"] = config.input_height;
        json["request_pool_size"] = Json::UInt64(pool_size);
        json["configured_cpu_pinning"] = config.openvino_cpu_pinning.has_value()
            ? Json::Value(*config.openvino_cpu_pinning) : Json::Value(Json::nullValue);
        Json::Value properties(Json::objectValue);
        for (const char* key : {
                 "NUM_STREAMS", "INFERENCE_NUM_THREADS", "PERFORMANCE_HINT_NUM_REQUESTS",
                 "ENABLE_CPU_PINNING", "ENABLE_HYPER_THREADING", "ENABLE_CPU_RESERVATION",
                 "SCHEDULING_CORE_TYPE", "INFERENCE_PRECISION_HINT", "EXECUTION_DEVICES",
                 "OPTIMAL_NUMBER_OF_INFER_REQUESTS"}) {
            try {
                std::ostringstream value;
                model.get_property(key).print(value);
                properties[key] = value.str().size() <= 256 ? value.str() : "unavailable";
            } catch (...) {
                // Optional diagnostic queries must not fail model initialization.
                properties[key] = "unavailable";
            }
        }
        json["properties"] = std::move(properties);
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        std::cout << Json::writeString(writer, json) << '\n';
    } catch (...) {
        // Diagnostics must not change model readiness or inference behavior.
    }
}
#endif

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

class RequestIndexGuard {
public:
    RequestIndexGuard(
        std::mutex& mutex,
        std::condition_variable& condition,
        std::vector<size_t>& available,
        size_t index
    ) : mutex_(mutex),
        condition_(condition),
        available_(available),
        index_(index) {}

    ~RequestIndexGuard() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            available_.push_back(index_);
        }
        condition_.notify_one();
    }

private:
    std::mutex& mutex_;
    std::condition_variable& condition_;
    std::vector<size_t>& available_;
    size_t index_;
};

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
          class_conf_thresholds_(config.class_conf_thresholds),
          iou_threshold_(config.iou_threshold),
          class_count_(config.num_classes > 0
                           ? config.num_classes
                           : static_cast<int>(config.class_names.size())),
          max_concurrency_(std::max(config.infer_request_count, 1)) {
        if (class_count_ <= 0) {
            class_count_ = loadClassCount(config.model_path);
        }

        if (!class_conf_thresholds_.empty()) {
            if (class_count_ <= 0
                || class_conf_thresholds_.size() != static_cast<size_t>(class_count_)) {
                throw std::invalid_argument("class threshold list size must match model class count");
            }
            for (float threshold : class_conf_thresholds_) {
                if (!std::isfinite(threshold)
                    || (threshold != -1.0F && (threshold < 0.0F || threshold > 1.0F))) {
                    throw std::invalid_argument("class thresholds must be -1 or in [0, 1]");
                }
            }
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

    size_t maxConcurrency() const {
        return max_concurrency_;
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
                conf_threshold_,
                class_conf_thresholds_
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
        ov::AnyMap properties{
            {ov::inference_num_threads.name(), config.thread_num},
            {
                ov::hint::performance_mode.name(),
                config.openvino_performance_mode == "throughput"
                    ? ov::hint::PerformanceMode::THROUGHPUT
                    : ov::hint::PerformanceMode::LATENCY
            },
        };
        if (config.infer_request_count > 0) {
            properties.emplace(
                ov::hint::num_requests.name(),
                config.infer_request_count
            );
        }
        if (config.openvino_cpu_pinning.has_value()) {
            properties.emplace(ov::hint::enable_cpu_pinning.name(), *config.openvino_cpu_pinning);
        }
        ov_compiled_model_ = ov_core_.compile_model(
            model,
            config.openvino_device,
            properties
        );

        const size_t request_count = config.infer_request_count > 0
            ? static_cast<size_t>(config.infer_request_count)
            : static_cast<size_t>(
                ov_compiled_model_.get_property(ov::optimal_number_of_infer_requests)
            );
        ov_infer_requests_.reserve(std::max<size_t>(request_count, 1));
        max_concurrency_ = std::max<size_t>(request_count, 1);
        ov_available_requests_.reserve(std::max<size_t>(request_count, 1));
        for (size_t i = 0; i < std::max<size_t>(request_count, 1); ++i) {
            ov_infer_requests_.push_back(ov_compiled_model_.create_infer_request());
            ov_available_requests_.push_back(i);
        }

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
        std::cout << "OpenVINO inference threads: "
                  << ov_compiled_model_.get_property(ov::inference_num_threads) << '\n';
        std::cout << "OpenVINO performance mode: "
                  << config.openvino_performance_mode << '\n';
        std::cout << "OpenVINO infer request pool: "
                  << ov_infer_requests_.size() << '\n';
        logOpenVinoRuntime(ov_compiled_model_, config, ov_infer_requests_.size());
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
        size_t request_index = 0;
        {
            std::unique_lock<std::mutex> lock(ov_pool_mutex_);
            ov_pool_condition_.wait(lock, [this]() {
                return !ov_available_requests_.empty();
            });
            request_index = ov_available_requests_.back();
            ov_available_requests_.pop_back();
        }
        RequestIndexGuard request_guard(
            ov_pool_mutex_, ov_pool_condition_, ov_available_requests_, request_index
        );
        ov::InferRequest& infer_request = ov_infer_requests_[request_index];
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
        infer_request.set_tensor(ov_input_name_, input_tensor);

        auto infer_start = std::chrono::steady_clock::now();
        infer_request.infer();

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
                shapeToInt64(infer_request.get_output_tensor(i).get_shape())
            );
        }
        result.postprocess_ms += elapsedMs(shape_start);

        if (!outputs.empty()) {
            const ov::Tensor output_tensor = infer_request.get_output_tensor(0);
            const float* output_data = output_tensor.data<const float>();
            const std::vector<int64_t> shape = shapeToInt64(output_tensor.get_shape());

            auto decode_start = std::chrono::steady_clock::now();
            std::vector<Detection> decoded = decode(
                output_data,
                shape,
                input,
                class_count_,
                conf_threshold_,
                class_conf_thresholds_
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
    std::vector<ov::InferRequest> ov_infer_requests_;
    std::vector<size_t> ov_available_requests_;
    std::mutex ov_pool_mutex_;
    std::condition_variable ov_pool_condition_;
    std::string ov_input_name_;
#endif

    std::vector<std::string> input_names_str_;
    std::vector<std::string> output_names_str_;
    std::vector<const char*> input_name_ptrs_;
    std::vector<const char*> output_name_ptrs_;
    ModelBackend backend_ = ModelBackend::OnnxRuntime;
    float conf_threshold_ = 0.25F;
    std::vector<float> class_conf_thresholds_;
    float iou_threshold_ = 0.45F;
    int class_count_ = 0;
    size_t max_concurrency_ = 1;
};

YoloEngine::YoloEngine(const AppConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

YoloEngine::~YoloEngine() = default;

InferResult YoloEngine::infer(const TensorInput& input) {
    return infer(input, InferenceContext{});
}

InferResult YoloEngine::infer(const TensorInput& input, InferenceContext context) {
    InferResult result = impl_->infer(input);
    result.context = std::move(context);
    return result;
}

size_t YoloEngine::maxConcurrency() const {
    return impl_->maxConcurrency();
}

}  // namespace yolo
