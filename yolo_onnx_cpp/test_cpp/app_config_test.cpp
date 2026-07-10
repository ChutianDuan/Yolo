#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "config/app_config.h"

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

std::filesystem::path writeConfig(const std::string& name, const std::string& content) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path()
        / ("yolo_app_config_test_" + std::to_string(now) + "_" + name + ".yaml");

    std::ofstream file(path);
    if (!file.is_open()) {
        fail("Failed to write config: " + path.string());
    }
    file << content;
    return path;
}

void expectThrowsWith(
    const std::string& name,
    const std::string& content,
    const std::string& expected_message
) {
    const auto path = writeConfig(name, content);
    try {
        (void)yolo::loadAppConfig(path.string());
    } catch (const std::exception& e) {
        const std::string message = e.what();
        expect(
            message.find(expected_message) != std::string::npos,
            "Unexpected error message: " + message
        );
        return;
    }

    fail("Expected config load to fail");
}

void testOldConfigStaysCompatible() {
    const auto path = writeConfig(
        "old",
        "model_path: ./deploy/best.onnx\n"
        "input_width: 1280\n"
        "input_height: 736\n"
        "conf_threshold: 0.25\n"
        "iou_threshold: 0.45\n"
        "num_classes: 2\n"
        "class_names: [person, car]\n"
    );

    const yolo::AppConfig config = yolo::loadAppConfig(path.string());
    const std::filesystem::path expected_model =
        (path.parent_path() / "./deploy/best.onnx").lexically_normal();

    expect(config.model_path == expected_model.string(), "model_path was not resolved");
    expect(config.model_backend == "auto", "old config should default model_backend to auto");
    expect(config.openvino_device == "CPU", "old config should default OpenVINO device to CPU");
    expect(config.video_model_async, "old config should default video_model_async to true");
    expect(config.video_onnx_async, "old config should default legacy video_onnx_async to true");
    expect(!yolo::hasLowResModelConfig(config), "old config should not enable low-res model");
    expect(config.low_res_model_path.empty(), "old config low_res_model_path should be empty");
}

void testVideoAsyncKeysStayCompatible() {
    const auto model_key_path = writeConfig(
        "model_async",
        "model_path: ./deploy/high.onnx\n"
        "input_width: 1280\n"
        "input_height: 736\n"
        "video_model_async: false\n"
    );
    const yolo::AppConfig model_key = yolo::loadAppConfig(model_key_path.string());
    expect(!model_key.video_model_async, "video_model_async key was not parsed");
    expect(!model_key.video_onnx_async, "legacy video_onnx_async alias was not synchronized");

    const auto legacy_key_path = writeConfig(
        "legacy_async",
        "model_path: ./deploy/high.onnx\n"
        "input_width: 1280\n"
        "input_height: 736\n"
        "video_onnx_async: false\n"
    );
    const yolo::AppConfig legacy_key = yolo::loadAppConfig(legacy_key_path.string());
    expect(!legacy_key.video_model_async, "legacy video_onnx_async key was not parsed");
    expect(!legacy_key.video_onnx_async, "legacy video_onnx_async key mismatch");
}

void testLowResConfigDerivesEngineConfig() {
    const auto path = writeConfig(
        "low",
        "model_path: ./deploy/high.onnx\n"
        "model_backend: openvino\n"
        "openvino_device: CPU\n"
        "input_width: 1280\n"
        "input_height: 736\n"
        "conf_threshold: 0.25\n"
        "iou_threshold: 0.50\n"
        "low_res_model_path: ./deploy/low.onnx\n"
        "low_res_input_width: 640\n"
        "low_res_input_height: 384\n"
        "num_classes: 2\n"
        "class_names: [person, car]\n"
        "thread_num: 3\n"
        "use_letterbox: false\n"
    );

    const yolo::AppConfig config = yolo::loadAppConfig(path.string());
    const yolo::AppConfig low_res = yolo::makeLowResAppConfig(config);
    const std::filesystem::path expected_low_model =
        (path.parent_path() / "./deploy/low.onnx").lexically_normal();

    expect(yolo::hasLowResModelConfig(config), "low-res config should be enabled");
    expect(config.low_res_model_path == expected_low_model.string(), "low-res path not resolved");
    expect(low_res.model_path == expected_low_model.string(), "derived low-res model mismatch");
    expect(low_res.input_width == 640, "derived low-res width mismatch");
    expect(low_res.input_height == 384, "derived low-res height mismatch");
    expect(low_res.conf_threshold == 0.25F, "derived low-res default confidence mismatch");
    expect(low_res.iou_threshold == 0.50F, "derived low-res default IoU mismatch");
    expect(low_res.model_backend == "openvino", "derived low-res backend mismatch");
    expect(low_res.openvino_device == "CPU", "derived low-res device mismatch");
    expect(low_res.thread_num == 3, "derived low-res thread count mismatch");
    expect(!low_res.use_letterbox, "derived low-res letterbox mismatch");
    expect(low_res.class_names.size() == 2, "derived low-res class names mismatch");
}

void testExplicitLowResThresholds() {
    const auto path = writeConfig(
        "thresholds",
        "model_path: ./deploy/high.onnx\n"
        "input_width: 1280\n"
        "input_height: 736\n"
        "conf_threshold: 0.25\n"
        "iou_threshold: 0.50\n"
        "low_res_model_path: ./deploy/low.onnx\n"
        "low_res_input_width: 640\n"
        "low_res_input_height: 384\n"
        "low_res_conf_threshold: 0.20\n"
        "low_res_iou_threshold: 0.40\n"
    );

    const yolo::AppConfig low_res =
        yolo::makeLowResAppConfig(yolo::loadAppConfig(path.string()));

    expect(low_res.conf_threshold == 0.20F, "explicit low-res confidence mismatch");
    expect(low_res.iou_threshold == 0.40F, "explicit low-res IoU mismatch");
}

void testLowResModelRequiresDimensions() {
    expectThrowsWith(
        "missing_low_dims",
        "model_path: ./deploy/high.onnx\n"
        "input_width: 1280\n"
        "input_height: 736\n"
        "low_res_model_path: ./deploy/low.onnx\n",
        "low_res_input_width and low_res_input_height must be positive"
    );
}

void testInvalidLowResThresholdFails() {
    expectThrowsWith(
        "invalid_low_threshold",
        "model_path: ./deploy/high.onnx\n"
        "input_width: 1280\n"
        "input_height: 736\n"
        "low_res_conf_threshold: -0.5\n",
        "low_res_conf_threshold must be unset or in [0, 1]"
    );
}

void testInvalidBackendFails() {
    expectThrowsWith(
        "invalid_backend",
        "model_path: ./deploy/high.onnx\n"
        "model_backend: tensorrt\n"
        "input_width: 1280\n"
        "input_height: 736\n",
        "model_backend must be auto, onnx, or openvino"
    );
}

}  // namespace

int main() {
    testOldConfigStaysCompatible();
    testVideoAsyncKeysStayCompatible();
    testLowResConfigDerivesEngineConfig();
    testExplicitLowResThresholds();
    testLowResModelRequiresDimensions();
    testInvalidLowResThresholdFails();
    testInvalidBackendFails();
    std::cout << "app_config_test passed\n";
    return 0;
}
