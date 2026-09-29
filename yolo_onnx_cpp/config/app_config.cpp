#include "app_config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace yolo {
namespace {

std::string trim(const std::string& text) {
    auto begin = text.begin();
    while (begin != text.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    auto end = text.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }

    return std::string(begin, end);
}

std::string stripComment(const std::string& text) {
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (ch == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (ch == '#' && !in_single_quote && !in_double_quote) {
            return text.substr(0, i);
        }
    }

    return text;
}

std::string unquote(const std::string& value) {
    const std::string trimmed = trim(value);
    if (trimmed.size() >= 2) {
        const char first = trimmed.front();
        const char last = trimmed.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return trimmed.substr(1, trimmed.size() - 2);
        }
    }
    return trimmed;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int parseInt(const std::string& key, const std::string& value) {
    try {
        size_t parsed = 0;
        const int result = std::stoi(value, &parsed);
        if (parsed != value.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer for " + key + ": " + value);
    }
}

float parseFloat(const std::string& key, const std::string& value) {
    try {
        size_t parsed = 0;
        const float result = std::stof(value, &parsed);
        if (parsed != value.size() || !std::isfinite(result)) {
            throw std::invalid_argument("expected a finite float");
        }
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid float for " + key + ": " + value);
    }
}

bool parseBool(const std::string& key, std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }

    throw std::runtime_error("Invalid bool for " + key + ": " + value);
}

bool invalidOptionalThreshold(float value) {
    return (value < 0.0F && value != -1.0F) || value > 1.0F;
}

bool isValidEnvironmentVariableName(const std::string& name) {
    if (name.empty()) {
        return true;
    }
    const auto valid_first = [](unsigned char ch) {
        return std::isalpha(ch) || ch == '_';
    };
    const auto valid_rest = [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    };
    return valid_first(static_cast<unsigned char>(name.front()))
        && std::all_of(name.begin() + 1, name.end(), valid_rest);
}

std::vector<std::string> parseInlineList(const std::string& value) {
    const std::string trimmed = trim(value);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
        return {unquote(trimmed)};
    }

    std::vector<std::string> items;
    std::stringstream stream(trimmed.substr(1, trimmed.size() - 2));
    std::string item;
    while (std::getline(stream, item, ',')) {
        const std::string parsed = unquote(item);
        if (!parsed.empty()) {
            items.push_back(parsed);
        }
    }
    return items;
}

std::vector<float> parseThresholdList(const std::string& key, const std::string& value) {
    const std::string trimmed = trim(value);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
        throw std::runtime_error(key + " must be a list");
    }

    const std::string content = trim(trimmed.substr(1, trimmed.size() - 2));
    if (content.empty()) {
        return {};
    }
    if (content.back() == ',') {
        throw std::runtime_error(key + " cannot contain an empty item");
    }

    std::vector<float> thresholds;
    std::stringstream stream(content);
    std::string item;
    while (std::getline(stream, item, ',')) {
        // Do not skip empty items: that would silently shift class IDs.
        thresholds.push_back(parseFloat(key, unquote(item)));
    }
    return thresholds;
}

void setScalar(AppConfig& config, const std::string& key, const std::string& value) {
    const std::string parsed = unquote(value);

    if (key == "model_path") {
        config.model_path = parsed;
    } else if (key == "model_backend") {
        config.model_backend = toLower(parsed);
    } else if (key == "openvino_device") {
        config.openvino_device = parsed;
    } else if (key == "input_width") {
        config.input_width = parseInt(key, parsed);
    } else if (key == "input_height") {
        config.input_height = parseInt(key, parsed);
    } else if (key == "conf_threshold") {
        config.conf_threshold = parseFloat(key, parsed);
    } else if (key == "iou_threshold") {
        config.iou_threshold = parseFloat(key, parsed);
    } else if (key == "low_res_model_path") {
        config.low_res_model_path = parsed;
    } else if (key == "low_res_input_width") {
        config.low_res_input_width = parseInt(key, parsed);
    } else if (key == "low_res_input_height") {
        config.low_res_input_height = parseInt(key, parsed);
    } else if (key == "low_res_conf_threshold") {
        config.low_res_conf_threshold = parseFloat(key, parsed);
    } else if (key == "low_res_iou_threshold") {
        config.low_res_iou_threshold = parseFloat(key, parsed);
    } else if (key == "num_classes") {
        config.num_classes = parseInt(key, parsed);
    } else if (key == "thread_num") {
        config.thread_num = parseInt(key, parsed);
    } else if (key == "server_io_threads") {
        config.server_io_threads = parseInt(key, parsed);
    } else if (key == "opencv_threads") {
        config.opencv_threads = parseInt(key, parsed);
    } else if (key == "high_model_threads") {
        config.high_model_threads = parseInt(key, parsed);
    } else if (key == "low_model_threads") {
        config.low_model_threads = parseInt(key, parsed);
    } else if (key == "infer_request_count") {
        config.infer_request_count = parseInt(key, parsed);
    } else if (key == "low_res_infer_request_count") {
        config.low_res_infer_request_count = parseInt(key, parsed);
    } else if (key == "openvino_performance_mode") {
        config.openvino_performance_mode = toLower(parsed);
    } else if (key == "openvino_cpu_pinning") {
        config.openvino_cpu_pinning = parseBool(key, parsed);
    } else if (key == "max_streams") {
        config.max_streams = parseInt(key, parsed);
    } else if (key == "per_stream_queue_depth") {
        config.per_stream_queue_depth = parseInt(key, parsed);
    } else if (key == "max_request_age_ms") {
        config.max_request_age_ms = parseInt(key, parsed);
    } else if (key == "max_result_age_ms") {
        config.max_result_age_ms = parseInt(key, parsed);
    } else if (key == "max_stream_duration_seconds") {
        config.max_stream_duration_seconds = parseInt(key, parsed);
    } else if (key == "stream_max_consecutive_errors") {
        config.stream_max_consecutive_errors = parseInt(key, parsed);
    } else if (key == "video_job_threads") {
        config.video_job_threads = parseInt(key, parsed);
    } else if (key == "video_job_queue_depth") {
        config.video_job_queue_depth = parseInt(key, parsed);
    } else if (key == "use_letterbox") {
        config.use_letterbox = parseBool(key, parsed);
    } else if (key == "video_detect_fps") {
        config.video_detect_fps = parseFloat(key, parsed);
    } else if (key == "video_high_detect_fps") {
        config.video_high_detect_fps = parseFloat(key, parsed);
    } else if (key == "high_res_roi_enabled") {
        config.high_res_roi_enabled = parseBool(key, parsed);
    } else if (key == "high_res_roi_x") {
        config.high_res_roi_x = parseFloat(key, parsed);
    } else if (key == "high_res_roi_y") {
        config.high_res_roi_y = parseFloat(key, parsed);
    } else if (key == "high_res_roi_width") {
        config.high_res_roi_width = parseFloat(key, parsed);
    } else if (key == "high_res_roi_height") {
        config.high_res_roi_height = parseFloat(key, parsed);
    } else if (key == "high_res_roi_full_frame_interval") {
        config.high_res_roi_full_frame_interval = parseInt(key, parsed);
    } else if (key == "video_stride_mode") {
        config.video_stride_mode = parsed;
    } else if (key == "video_model_async" || key == "video_onnx_async") {
        const bool async_enabled = parseBool(key, parsed);
        config.video_model_async = async_enabled;
        config.video_onnx_async = async_enabled;
    } else if (key == "client_max_body_mb") {
        config.client_max_body_mb = parseInt(key, parsed);
    } else if (key == "client_max_memory_body_mb") {
        config.client_max_memory_body_mb = parseInt(key, parsed);
    } else if (key == "api_bearer_token_env") {
        config.api_bearer_token_env = parsed;
    } else if (key == "api_rate_limit_requests_per_second") {
        config.api_rate_limit_requests_per_second = parseFloat(key, parsed);
    } else if (key == "api_rate_limit_burst") {
        config.api_rate_limit_burst = parseInt(key, parsed);
    } else if (key == "tls_certificate_path") {
        config.tls_certificate_path = parsed;
    } else if (key == "tls_private_key_path") {
        config.tls_private_key_path = parsed;
    }
}

std::filesystem::path resolveConfigPath(const std::string& config_path) {
    const std::filesystem::path path(config_path);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return std::filesystem::absolute(path).lexically_normal();
}

void validateConfig(AppConfig& config) {
    if (config.model_path.empty()) {
        throw std::runtime_error("model_path cannot be empty");
    }
    if (config.model_backend != "auto"
        && config.model_backend != "onnx"
        && config.model_backend != "openvino") {
        throw std::runtime_error("model_backend must be auto, onnx, or openvino");
    }
    if (config.openvino_device.empty()) {
        throw std::runtime_error("openvino_device cannot be empty");
    }
    if (config.input_width <= 0 || config.input_height <= 0) {
        throw std::runtime_error("input_width and input_height must be positive");
    }
    if (config.conf_threshold < 0.0F || config.conf_threshold > 1.0F) {
        throw std::runtime_error("conf_threshold must be in [0, 1]");
    }
    if (config.iou_threshold < 0.0F || config.iou_threshold > 1.0F) {
        throw std::runtime_error("iou_threshold must be in [0, 1]");
    }
    if (invalidOptionalThreshold(config.low_res_conf_threshold)) {
        throw std::runtime_error("low_res_conf_threshold must be unset or in [0, 1]");
    }
    if (invalidOptionalThreshold(config.low_res_iou_threshold)) {
        throw std::runtime_error("low_res_iou_threshold must be unset or in [0, 1]");
    }
    if (!config.low_res_model_path.empty()
        && (config.low_res_input_width <= 0 || config.low_res_input_height <= 0)) {
        throw std::runtime_error(
            "low_res_input_width and low_res_input_height must be positive"
        );
    }
    if (config.thread_num <= 0) {
        throw std::runtime_error("thread_num must be positive");
    }
    if (config.server_io_threads < 0
        || config.opencv_threads < 0
        || config.high_model_threads < 0
        || config.low_model_threads < 0) {
        throw std::runtime_error("scoped thread settings cannot be negative");
    }
    if (config.infer_request_count < 0 || config.low_res_infer_request_count < 0) {
        throw std::runtime_error("OpenVINO infer request counts cannot be negative");
    }
    if (config.openvino_performance_mode != "latency"
        && config.openvino_performance_mode != "throughput") {
        throw std::runtime_error(
            "openvino_performance_mode must be latency or throughput"
        );
    }
    if (config.max_streams <= 0 || config.per_stream_queue_depth <= 0) {
        throw std::runtime_error(
            "max_streams and per_stream_queue_depth must be positive"
        );
    }
    if (config.max_request_age_ms < 0 || config.max_stream_duration_seconds < 0) {
        throw std::runtime_error(
            "request age and stream duration settings cannot be negative"
        );
    }
    if (config.max_result_age_ms < 0) {
        throw std::runtime_error("max_result_age_ms cannot be negative");
    }
    if (config.stream_max_consecutive_errors <= 0) {
        throw std::runtime_error("stream_max_consecutive_errors must be positive");
    }
    if (config.video_job_threads <= 0 || config.video_job_queue_depth <= 0) {
        throw std::runtime_error(
            "video_job_threads and video_job_queue_depth must be positive"
        );
    }
    if (config.video_detect_fps < 0.0F || config.video_high_detect_fps < 0.0F) {
        throw std::runtime_error("video detection FPS settings must be non-negative");
    }
    const bool invalid_roi = config.high_res_roi_x < 0.0F
        || config.high_res_roi_y < 0.0F
        || config.high_res_roi_width <= 0.0F
        || config.high_res_roi_height <= 0.0F
        || config.high_res_roi_x + config.high_res_roi_width > 1.0F
        || config.high_res_roi_y + config.high_res_roi_height > 1.0F;
    if (invalid_roi) {
        throw std::runtime_error(
            "high-resolution ROI must be a positive normalized rectangle inside [0, 1]"
        );
    }
    if (config.high_res_roi_full_frame_interval <= 0) {
        throw std::runtime_error(
            "high_res_roi_full_frame_interval must be positive"
        );
    }
    if (config.client_max_body_mb <= 0) {
        throw std::runtime_error("client_max_body_mb must be positive");
    }
    if (config.video_stride_mode != "dynamic" && config.video_stride_mode != "fixed") {
        throw std::runtime_error("video_stride_mode must be dynamic or fixed");
    }
    if (config.client_max_memory_body_mb <= 0
        || config.client_max_memory_body_mb > config.client_max_body_mb) {
        throw std::runtime_error(
            "client_max_memory_body_mb must be positive and not exceed client_max_body_mb"
        );
    }
    if (!isValidEnvironmentVariableName(config.api_bearer_token_env)) {
        throw std::runtime_error("api_bearer_token_env is not a valid environment variable name");
    }
    if (config.api_rate_limit_requests_per_second < 0.0F
        || config.api_rate_limit_burst <= 0) {
        throw std::runtime_error(
            "API rate limit must be non-negative with a positive burst"
        );
    }
    if (config.tls_certificate_path.empty()
        != config.tls_private_key_path.empty()) {
        throw std::runtime_error(
            "tls_certificate_path and tls_private_key_path must be configured together"
        );
    }
    if (config.num_classes < 0) {
        throw std::runtime_error("num_classes cannot be negative");
    }
    if (config.num_classes == 0 && !config.class_names.empty()) {
        config.num_classes = static_cast<int>(config.class_names.size());
    }
    if (config.num_classes > 0
        && !config.class_names.empty()
        && static_cast<int>(config.class_names.size()) != config.num_classes) {
        throw std::runtime_error("num_classes must match class_names size");
    }
    for (const auto* thresholds : {
             &config.class_conf_thresholds, &config.low_res_class_conf_thresholds}) {
        if (thresholds->empty()) {
            continue;
        }
        if (config.num_classes <= 0
            || thresholds->size() != static_cast<size_t>(config.num_classes)) {
            throw std::runtime_error(
                "class threshold list size must match num_classes/class_names"
            );
        }
        for (float threshold : *thresholds) {
            if (!std::isfinite(threshold) || invalidOptionalThreshold(threshold)) {
                throw std::runtime_error("class thresholds must be -1 or in [0, 1]");
            }
        }
    }
}

}  // namespace

AppConfig loadAppConfig(const std::string& config_path) {
    const std::filesystem::path path = resolveConfigPath(config_path);
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + path.string());
    }

    AppConfig config;
    std::string list_key;
    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        ++line_number;
        const std::string parsed_line = trim(stripComment(line));
        if (parsed_line.empty()) {
            continue;
        }

        if (parsed_line.front() == '-') {
            const std::string item = unquote(parsed_line.substr(1));
            if (list_key == "class_names") {
                config.class_names.push_back(item);
            } else if (list_key == "class_conf_thresholds") {
                config.class_conf_thresholds.push_back(parseFloat(list_key, item));
            } else if (list_key == "low_res_class_conf_thresholds") {
                config.low_res_class_conf_thresholds.push_back(parseFloat(list_key, item));
            } else {
                throw std::runtime_error(
                    "Unexpected list item at " + path.string() + ":" + std::to_string(line_number)
                );
            }
            continue;
        }

        const size_t sep = parsed_line.find(':');
        if (sep == std::string::npos) {
            throw std::runtime_error(
                "Invalid config line at " + path.string() + ":" + std::to_string(line_number)
            );
        }

        const std::string key = trim(parsed_line.substr(0, sep));
        const std::string value = trim(parsed_line.substr(sep + 1));

        if (key == "class_names") {
            list_key = key;
            config.class_names.clear();
            if (!value.empty()) {
                config.class_names = parseInlineList(value);
            }
            continue;
        }

        if (key == "class_conf_thresholds" || key == "low_res_class_conf_thresholds") {
            list_key = key;
            auto& thresholds = key == "class_conf_thresholds"
                ? config.class_conf_thresholds
                : config.low_res_class_conf_thresholds;
            thresholds = value.empty() ? std::vector<float>{} : parseThresholdList(key, value);
            continue;
        }

        list_key.clear();
        setScalar(config, key, value);
    }

    validateConfig(config);

    std::filesystem::path model_path(config.model_path);
    if (model_path.is_relative()) {
        model_path = path.parent_path() / model_path;
    }
    config.model_path = model_path.lexically_normal().string();

    if (!config.low_res_model_path.empty()) {
        std::filesystem::path low_res_model_path(config.low_res_model_path);
        if (low_res_model_path.is_relative()) {
            low_res_model_path = path.parent_path() / low_res_model_path;
        }
        config.low_res_model_path = low_res_model_path.lexically_normal().string();
    }

    const auto resolve_optional_path = [&path](std::string& value) {
        if (value.empty()) {
            return;
        }
        std::filesystem::path resolved(value);
        if (resolved.is_relative()) {
            resolved = path.parent_path() / resolved;
        }
        value = resolved.lexically_normal().string();
    };
    resolve_optional_path(config.tls_certificate_path);
    resolve_optional_path(config.tls_private_key_path);

    return config;
}

bool hasLowResModelConfig(const AppConfig& config) {
    return !config.low_res_model_path.empty();
}

int serverIoThreadCount(const AppConfig& config) {
    return config.server_io_threads > 0
        ? config.server_io_threads
        : config.thread_num;
}

AppConfig makeHighResAppConfig(const AppConfig& config) {
    AppConfig high_res = config;
    if (config.high_model_threads > 0) {
        high_res.thread_num = config.high_model_threads;
    }
    return high_res;
}

AppConfig makeLowResAppConfig(const AppConfig& config) {
    AppConfig low_res = config;
    low_res.model_path = config.low_res_model_path;
    low_res.input_width = config.low_res_input_width;
    low_res.input_height = config.low_res_input_height;
    low_res.conf_threshold = config.low_res_conf_threshold >= 0.0F
        ? config.low_res_conf_threshold
        : config.conf_threshold;
    low_res.class_conf_thresholds = config.low_res_class_conf_thresholds.empty()
        ? config.class_conf_thresholds
        : config.low_res_class_conf_thresholds;
    low_res.iou_threshold = config.low_res_iou_threshold >= 0.0F
        ? config.low_res_iou_threshold
        : config.iou_threshold;
    low_res.thread_num = config.low_model_threads > 0
        ? config.low_model_threads
        : makeHighResAppConfig(config).thread_num;
    if (config.low_res_infer_request_count > 0) {
        low_res.infer_request_count = config.low_res_infer_request_count;
    }
    low_res.low_res_model_path.clear();
    low_res.low_res_input_width = 0;
    low_res.low_res_input_height = 0;
    low_res.low_res_conf_threshold = -1.0F;
    low_res.low_res_class_conf_thresholds.clear();
    low_res.low_res_iou_threshold = -1.0F;
    return low_res;
}

}  // namespace yolo
