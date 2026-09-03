#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.h>
#include <onnxruntime/onnxruntime_c_api.h>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#if YOLO_ENABLE_OPENVINO
#include <openvino/core/version.hpp>
#endif

#include "config/app_config.h"
#include "image/image_processing.h"
#include "metrics/performance_metrics.h"
#include "model/yolo_engine.h"

namespace {

struct Options {
    std::string model_path;
    std::string video_path;
    std::string backend;
    int input_width = 0;
    int input_height = 0;
    int thread_num = 0;
    int warmup_iterations = 10;
    int measured_iterations = 50;
};

void printUsage(const char* program) {
    std::cout
        << "Usage: " << program << " --model PATH --video PATH --backend onnx|openvino"
        << " --input-width N --input-height N --threads N"
        << " [--warmup N] [--iterations N]\n";
}

int parsePositiveInt(const std::string& option, const std::string& value) {
    try {
        size_t parsed = 0;
        const int number = std::stoi(value, &parsed);
        if (parsed != value.size() || number <= 0) {
            throw std::invalid_argument("not positive");
        }
        return number;
    } catch (const std::exception&) {
        throw std::runtime_error(option + " must be a positive integer, got: " + value);
    }
}

int parseNonNegativeInt(const std::string& option, const std::string& value) {
    try {
        size_t parsed = 0;
        const int number = std::stoi(value, &parsed);
        if (parsed != value.size() || number < 0) {
            throw std::invalid_argument("negative");
        }
        return number;
    } catch (const std::exception&) {
        throw std::runtime_error(option + " must be a non-negative integer, got: " + value);
    }
}

Options parseOptions(int argc, char* argv[]) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help" || option == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        }
        if (i + 1 >= argc) {
            throw std::runtime_error("Missing value for " + option);
        }

        const std::string value = argv[++i];
        if (option == "--model") {
            options.model_path = value;
        } else if (option == "--video") {
            options.video_path = value;
        } else if (option == "--backend") {
            options.backend = value;
        } else if (option == "--input-width") {
            options.input_width = parsePositiveInt(option, value);
        } else if (option == "--input-height") {
            options.input_height = parsePositiveInt(option, value);
        } else if (option == "--threads") {
            options.thread_num = parsePositiveInt(option, value);
        } else if (option == "--warmup") {
            options.warmup_iterations = parseNonNegativeInt(option, value);
        } else if (option == "--iterations") {
            options.measured_iterations = parsePositiveInt(option, value);
        } else {
            throw std::runtime_error("Unknown option: " + option);
        }
    }

    if (options.model_path.empty() || options.video_path.empty()
        || options.backend.empty() || options.input_width <= 0
        || options.input_height <= 0 || options.thread_num <= 0) {
        throw std::runtime_error("All required options must be provided");
    }
    if (options.backend != "onnx" && options.backend != "openvino") {
        throw std::runtime_error("--backend must be onnx or openvino");
    }
    return options;
}

double elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count();
}

Json::Value samplesToJson(const std::vector<double>& samples) {
    Json::Value json(Json::arrayValue);
    for (const double sample : samples) {
        json.append(sample);
    }
    return json;
}

std::string normalizedPath(const std::string& path) {
    return std::filesystem::weakly_canonical(std::filesystem::path(path)).string();
}

std::string openVinoVersion() {
#if YOLO_ENABLE_OPENVINO
    const ov::Version version = ov::get_openvino_version();
    return version.buildNumber == nullptr ? "unknown" : version.buildNumber;
#else
    return "not_compiled";
#endif
}

int run(const Options& options) {
    yolo::AppConfig config;
    config.model_path = normalizedPath(options.model_path);
    config.model_backend = options.backend;
    config.input_width = options.input_width;
    config.input_height = options.input_height;
    config.thread_num = options.thread_num;
    config.num_classes = 10;
    config.use_letterbox = true;

    cv::VideoCapture capture;
    if (!yolo::openVideoCapture(capture, options.video_path)) {
        throw std::runtime_error(yolo::videoOpenFailureMessage(options.video_path));
    }

    cv::Mat frame;
    if (!capture.read(frame) || frame.empty()) {
        throw std::runtime_error("Failed to read the first video frame");
    }

    const auto input = yolo::preprocessImageMat(frame, config);
    if (!input.has_value()) {
        throw std::runtime_error("Failed to preprocess the first video frame");
    }

    const auto load_start = std::chrono::steady_clock::now();
    yolo::YoloEngine engine(config);
    const double model_load_ms = elapsedMs(load_start);

    for (int i = 0; i < options.warmup_iterations; ++i) {
        static_cast<void>(engine.infer(*input));
    }

    std::vector<double> inference_samples;
    std::vector<double> pipeline_samples;
    inference_samples.reserve(static_cast<size_t>(options.measured_iterations));
    pipeline_samples.reserve(static_cast<size_t>(options.measured_iterations));

    size_t detection_count = 0;
    std::vector<std::vector<int64_t>> output_shapes;
    const auto usage_start = yolo::captureProcessUsage();
    for (int i = 0; i < options.measured_iterations; ++i) {
        const auto pipeline_start = std::chrono::steady_clock::now();
        const yolo::InferResult result = engine.infer(*input);
        pipeline_samples.push_back(elapsedMs(pipeline_start));
        inference_samples.push_back(result.model_inference_ms);
        detection_count = result.detections.size();
        output_shapes = result.output_shapes;
    }
    const auto usage_end = yolo::captureProcessUsage();

    Json::Value root(Json::objectValue);
    root["schema_version"] = 2;
    root["backend"] = options.backend;
    root["model_path"] = config.model_path;
    root["model_size_bytes"] = Json::UInt64(std::filesystem::file_size(config.model_path));
    root["video_path"] = normalizedPath(options.video_path);
    root["video_frame_width"] = frame.cols;
    root["video_frame_height"] = frame.rows;
    root["input_width"] = options.input_width;
    root["input_height"] = options.input_height;
    root["thread_num"] = options.thread_num;
    root["warmup_iterations"] = options.warmup_iterations;
    root["measured_iterations"] = options.measured_iterations;
    root["model_load_ms"] = model_load_ms;
    root["cpu_utilization_percent"] = yolo::cpuUtilizationPercent(usage_start, usage_end);
    root["rss_memory_mb"] = yolo::currentRssMemoryMb();
    root["detection_count"] = Json::UInt64(detection_count);
    root["onnxruntime_version"] = OrtGetApiBase()->GetVersionString();
    root["openvino_version"] = openVinoVersion();
    root["inference_ms_samples"] = samplesToJson(inference_samples);
    root["pipeline_ms_samples"] = samplesToJson(pipeline_samples);

    Json::Value shapes(Json::arrayValue);
    for (const auto& shape : output_shapes) {
        Json::Value json_shape(Json::arrayValue);
        for (const int64_t dimension : shape) {
            json_shape.append(Json::Int64(dimension));
        }
        shapes.append(std::move(json_shape));
    }
    root["output_shapes"] = std::move(shapes);

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    std::cout << "BENCHMARK_RESULT=" << Json::writeString(writer, root) << '\n';
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "onnx_thread_benchmark: " << error.what() << '\n';
        printUsage(argv[0]);
        return 1;
    }
}
