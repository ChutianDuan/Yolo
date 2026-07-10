#include "app_config.h"

#include <algorithm>
#include <cctype>
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
        if (parsed != value.size()) {
            throw std::invalid_argument("trailing characters");
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
    } else if (key == "use_letterbox") {
        config.use_letterbox = parseBool(key, parsed);
    } else if (key == "video_detect_fps") {
        config.video_detect_fps = parseFloat(key, parsed);
    } else if (key == "video_stride_mode") {
        config.video_stride_mode = parsed;
    } else if (key == "video_model_async" || key == "video_onnx_async") {
        const bool async_enabled = parseBool(key, parsed);
        config.video_model_async = async_enabled;
        config.video_onnx_async = async_enabled;
    } else if (key == "client_max_body_mb") {
        config.client_max_body_mb = parseInt(key, parsed);
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
    if (config.video_detect_fps < 0.0F) {
        throw std::runtime_error("video_detect_fps must be non-negative");
    }
    if (config.client_max_body_mb <= 0) {
        throw std::runtime_error("client_max_body_mb must be positive");
    }
    if (config.video_stride_mode != "dynamic" && config.video_stride_mode != "fixed") {
        throw std::runtime_error("video_stride_mode must be dynamic or fixed");
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
            if (list_key != "class_names") {
                throw std::runtime_error(
                    "Unexpected list item at " + path.string() + ":" + std::to_string(line_number)
                );
            }
            config.class_names.push_back(unquote(parsed_line.substr(1)));
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

    return config;
}

bool hasLowResModelConfig(const AppConfig& config) {
    return !config.low_res_model_path.empty();
}

AppConfig makeLowResAppConfig(const AppConfig& config) {
    AppConfig low_res = config;
    low_res.model_path = config.low_res_model_path;
    low_res.input_width = config.low_res_input_width;
    low_res.input_height = config.low_res_input_height;
    low_res.conf_threshold = config.low_res_conf_threshold >= 0.0F
        ? config.low_res_conf_threshold
        : config.conf_threshold;
    low_res.iou_threshold = config.low_res_iou_threshold >= 0.0F
        ? config.low_res_iou_threshold
        : config.iou_threshold;
    low_res.low_res_model_path.clear();
    low_res.low_res_input_width = 0;
    low_res.low_res_input_height = 0;
    low_res.low_res_conf_threshold = -1.0F;
    low_res.low_res_iou_threshold = -1.0F;
    return low_res;
}

}  // namespace yolo
