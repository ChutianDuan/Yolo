#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

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
    expect(yolo::serverIoThreadCount(config) == 4, "legacy server threads should fall back");
    expect(yolo::makeHighResAppConfig(config).thread_num == 4, "legacy model threads should fall back");
    expect(config.infer_request_count == 1, "legacy request pool default changed");
    expect(config.openvino_performance_mode == "latency", "legacy performance mode changed");
    expect(!config.openvino_cpu_pinning.has_value(), "legacy config changed CPU pinning default");
    expect(config.max_stream_duration_seconds == 0, "legacy stream duration changed");
    expect(config.max_result_age_ms == 300, "default live result age limit changed");
    expect(!config.high_res_roi_enabled, "legacy config unexpectedly enabled ROI");
    expect(config.high_res_roi_x == 0.0F && config.high_res_roi_y == 0.0F
               && config.high_res_roi_width == 1.0F
               && config.high_res_roi_height == 1.0F,
           "legacy ROI rectangle defaults changed");
    expect(config.high_res_roi_full_frame_interval == 4,
           "legacy ROI full-frame interval changed");
    expect(config.stream_max_consecutive_errors == 5, "legacy circuit limit changed");
    expect(config.api_bearer_token_env.empty(), "legacy auth default changed");
    expect(config.api_rate_limit_requests_per_second == 0.0F, "legacy rate limit changed");
    expect(config.tls_certificate_path.empty(), "legacy TLS default changed");
    expect(!yolo::hasLowResModelConfig(config), "old config should not enable low-res model");
    expect(config.low_res_model_path.empty(), "old config low_res_model_path should be empty");
    expect(config.class_conf_thresholds.empty(), "Old high class thresholds should remain empty");
    expect(config.low_res_class_conf_thresholds.empty(), "Old low class thresholds should remain empty");
}

void testOpenVinoCpuPinning() {
    const std::string base =
        "model_path: ./deploy/high.onnx\n"
        "low_res_model_path: ./deploy/low.onnx\n"
        "low_res_input_width: 640\nlow_res_input_height: 384\n";
    for (const auto& item : std::vector<std::pair<std::string, bool>>{
             {"true", true}, {"false", false}, {"YES", true}, {"0", false}}) {
        const auto config = yolo::loadAppConfig(writeConfig(
            "cpu_pinning_" + item.first, base + "openvino_cpu_pinning: " + item.first + "\n"
        ).string());
        expect(config.openvino_cpu_pinning.has_value()
                   && *config.openvino_cpu_pinning == item.second,
               "explicit CPU pinning value was not parsed");
        expect(yolo::makeHighResAppConfig(config).openvino_cpu_pinning == config.openvino_cpu_pinning
                   && yolo::makeLowResAppConfig(config).openvino_cpu_pinning == config.openvino_cpu_pinning,
               "derived engine lost CPU pinning setting");
    }
    for (const std::string value : {"auto", "null", "invalid"}) {
        expectThrowsWith("cpu_pinning_bad", base + "openvino_cpu_pinning: " + value + "\n",
                         "Invalid bool for openvino_cpu_pinning");
    }
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

void testScopedRuntimeConfig() {
    const auto path = writeConfig(
        "scoped_runtime",
        "model_path: ./deploy/high.onnx\n"
        "input_width: 1280\n"
        "input_height: 736\n"
        "low_res_model_path: ./deploy/low.onnx\n"
        "low_res_input_width: 640\n"
        "low_res_input_height: 384\n"
        "thread_num: 3\n"
        "server_io_threads: 8\n"
        "opencv_threads: 2\n"
        "high_model_threads: 11\n"
        "low_model_threads: 5\n"
        "infer_request_count: 3\n"
        "low_res_infer_request_count: 2\n"
        "openvino_performance_mode: THROUGHPUT\n"
        "max_streams: 6\n"
        "per_stream_queue_depth: 3\n"
        "max_request_age_ms: 250\n"
        "max_result_age_ms: 180\n"
        "max_stream_duration_seconds: 3600\n"
        "stream_max_consecutive_errors: 3\n"
        "video_job_threads: 3\n"
        "video_job_queue_depth: 7\n"
        "video_high_detect_fps: 0.5\n"
        "high_res_roi_enabled: true\n"
        "high_res_roi_x: 0.1\n"
        "high_res_roi_y: 0.2\n"
        "high_res_roi_width: 0.6\n"
        "high_res_roi_height: 0.5\n"
        "high_res_roi_full_frame_interval: 3\n"
        "client_max_body_mb: 128\n"
        "client_max_memory_body_mb: 8\n"
        "api_bearer_token_env: YOLO_API_TOKEN\n"
        "api_rate_limit_requests_per_second: 12.5\n"
        "api_rate_limit_burst: 30\n"
        "tls_certificate_path: ./certs/server.crt\n"
        "tls_private_key_path: ./certs/server.key\n"
    );

    const yolo::AppConfig config = yolo::loadAppConfig(path.string());
    const yolo::AppConfig high_res = yolo::makeHighResAppConfig(config);
    const yolo::AppConfig low_res = yolo::makeLowResAppConfig(config);
    const std::filesystem::path expected_cert =
        (path.parent_path() / "./certs/server.crt").lexically_normal();
    const std::filesystem::path expected_key =
        (path.parent_path() / "./certs/server.key").lexically_normal();

    expect(yolo::serverIoThreadCount(config) == 8, "scoped server threads mismatch");
    expect(config.opencv_threads == 2, "OpenCV threads mismatch");
    expect(high_res.thread_num == 11, "high-res model threads mismatch");
    expect(high_res.infer_request_count == 3, "high-res request pool mismatch");
    expect(low_res.thread_num == 5, "low-res model threads mismatch");
    expect(low_res.infer_request_count == 2, "low-res request pool mismatch");
    expect(
        config.openvino_performance_mode == "throughput",
        "OpenVINO performance mode normalization mismatch"
    );
    expect(config.max_streams == 6, "max streams mismatch");
    expect(config.per_stream_queue_depth == 3, "per-stream queue depth mismatch");
    expect(config.max_request_age_ms == 250, "maximum request age mismatch");
    expect(config.max_result_age_ms == 180, "result age should be independent of request age");
    expect(config.max_stream_duration_seconds == 3600, "stream duration mismatch");
    expect(config.stream_max_consecutive_errors == 3, "circuit error limit mismatch");
    expect(config.video_job_threads == 3, "video job thread count mismatch");
    expect(config.video_job_queue_depth == 7, "video job queue depth mismatch");
    expect(config.video_high_detect_fps == 0.5F, "high detection cadence mismatch");
    expect(config.high_res_roi_enabled, "ROI enable flag mismatch");
    expect(config.high_res_roi_x == 0.1F && config.high_res_roi_y == 0.2F
               && config.high_res_roi_width == 0.6F
               && config.high_res_roi_height == 0.5F,
           "normalized ROI rectangle mismatch");
    expect(config.high_res_roi_full_frame_interval == 3,
           "ROI full-frame interval mismatch");
    expect(config.client_max_body_mb == 128, "maximum request body mismatch");
    expect(config.client_max_memory_body_mb == 8, "memory body threshold mismatch");
    expect(config.api_bearer_token_env == "YOLO_API_TOKEN", "auth env mismatch");
    expect(config.api_rate_limit_requests_per_second == 12.5F, "rate limit mismatch");
    expect(config.api_rate_limit_burst == 30, "rate limit burst mismatch");
    expect(config.tls_certificate_path == expected_cert.string(), "TLS cert path mismatch");
    expect(config.tls_private_key_path == expected_key.string(), "TLS key path mismatch");
}

void testInvalidRuntimeConfigFails() {
    expectThrowsWith(
        "invalid_result_age",
        "max_result_age_ms: -1\n",
        "max_result_age_ms cannot be negative"
    );
    const auto no_result_deadline = yolo::loadAppConfig(
        writeConfig("no_result_deadline", "max_result_age_ms: 0\n").string()
    );
    expect(no_result_deadline.max_result_age_ms == 0, "zero result age should disable deadline");
    expectThrowsWith(
        "negative_scoped_threads",
        "model_path: ./deploy/high.onnx\nserver_io_threads: -1\n",
        "scoped thread settings cannot be negative"
    );
    expectThrowsWith(
        "negative_request_count",
        "model_path: ./deploy/high.onnx\ninfer_request_count: -1\n",
        "OpenVINO infer request counts cannot be negative"
    );
    expectThrowsWith(
        "invalid_performance_mode",
        "model_path: ./deploy/high.onnx\nopenvino_performance_mode: balanced\n",
        "openvino_performance_mode must be latency or throughput"
    );
    expectThrowsWith(
        "invalid_stream_capacity",
        "model_path: ./deploy/high.onnx\nmax_streams: 0\n",
        "max_streams and per_stream_queue_depth must be positive"
    );
    expectThrowsWith(
        "invalid_request_age",
        "model_path: ./deploy/high.onnx\nmax_request_age_ms: -1\n",
        "request age and stream duration settings cannot be negative"
    );
    expectThrowsWith(
        "invalid_stream_duration",
        "model_path: ./deploy/high.onnx\nmax_stream_duration_seconds: -1\n",
        "request age and stream duration settings cannot be negative"
    );
    expectThrowsWith(
        "invalid_stream_error_limit",
        "model_path: ./deploy/high.onnx\nstream_max_consecutive_errors: 0\n",
        "stream_max_consecutive_errors must be positive"
    );
    expectThrowsWith(
        "invalid_video_job_capacity",
        "model_path: ./deploy/high.onnx\nvideo_job_threads: 0\n",
        "video_job_threads and video_job_queue_depth must be positive"
    );
    expectThrowsWith(
        "invalid_high_detect_fps",
        "model_path: ./deploy/high.onnx\nvideo_high_detect_fps: -1\n",
        "video detection FPS settings must be non-negative"
    );
    for (const std::string invalid_roi : {
             "high_res_roi_x: -0.1\n",
             "high_res_roi_y: 1.0\n",
             "high_res_roi_width: 0\n",
             "high_res_roi_height: 1.1\n",
             "high_res_roi_x: 0.6\nhigh_res_roi_width: 0.5\n",
             "high_res_roi_y: 0.6\nhigh_res_roi_height: 0.5\n"}) {
        expectThrowsWith(
            "invalid_roi", invalid_roi,
            "high-resolution ROI must be a positive normalized rectangle"
        );
    }
    expectThrowsWith(
        "invalid_roi_interval",
        "high_res_roi_full_frame_interval: 0\n",
        "high_res_roi_full_frame_interval must be positive"
    );
    expectThrowsWith(
        "invalid_memory_body_limit",
        "model_path: ./deploy/high.onnx\nclient_max_body_mb: 8\nclient_max_memory_body_mb: 9\n",
        "client_max_memory_body_mb must be positive and not exceed client_max_body_mb"
    );
    expectThrowsWith(
        "invalid_auth_env",
        "model_path: ./deploy/high.onnx\napi_bearer_token_env: 9INVALID\n",
        "api_bearer_token_env is not a valid environment variable name"
    );
    expectThrowsWith(
        "invalid_rate_limit",
        "model_path: ./deploy/high.onnx\napi_rate_limit_requests_per_second: -1\n",
        "API rate limit must be non-negative with a positive burst"
    );
    expectThrowsWith(
        "invalid_rate_burst",
        "model_path: ./deploy/high.onnx\napi_rate_limit_burst: 0\n",
        "API rate limit must be non-negative with a positive burst"
    );
    expectThrowsWith(
        "incomplete_tls",
        "model_path: ./deploy/high.onnx\ntls_certificate_path: ./server.crt\n",
        "tls_certificate_path and tls_private_key_path must be configured together"
    );
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

void testClassThresholdConfig() {
    const std::string base =
        "class_names: [person, car, sign]\n"
        "conf_threshold: 0.25\n"
        "low_res_conf_threshold: 0.35\n"
        "class_conf_thresholds: [0.1, -1, 0.8]\n";
    const auto inherited = yolo::loadAppConfig(
        writeConfig("class_inheritance", base).string()
    );
    const std::vector<float> high_values{0.1F, -1.0F, 0.8F};
    expect(inherited.num_classes == 3, "Class count should be inferred from names");
    expect(inherited.class_conf_thresholds == high_values, "Inline class thresholds mismatch");
    expect(yolo::makeHighResAppConfig(inherited).class_conf_thresholds == high_values,
           "High engine lost class thresholds");
    const auto low_inherited = yolo::makeLowResAppConfig(inherited);
    expect(low_inherited.class_conf_thresholds == high_values, "Low engine should inherit class list");
    expect(low_inherited.conf_threshold == 0.35F, "Low scalar fallback should remain independent");

    const auto explicit_low = yolo::loadAppConfig(writeConfig(
        "class_low_override", base +
        "low_res_class_conf_thresholds:\n"
        "  - 0.2\n"
        "  - '0.3' # quoted values and comments\n"
        "  - -1\n"
    ).string());
    const auto low = yolo::makeLowResAppConfig(explicit_low);
    expect(low.class_conf_thresholds == std::vector<float>({0.2F, 0.3F, -1.0F}),
           "Multiline low class thresholds mismatch");
    expect(explicit_low.class_conf_thresholds == high_values, "Low override changed high thresholds");
    expect(low.low_res_class_conf_thresholds.empty(), "Derived engine retained auxiliary low list");

    const auto scalar_low = yolo::loadAppConfig(writeConfig(
        "class_low_scalar", base + "low_res_class_conf_thresholds: [-1, -1, -1]\n"
    ).string());
    expect(yolo::makeLowResAppConfig(scalar_low).class_conf_thresholds
               == std::vector<float>({-1.0F, -1.0F, -1.0F}),
           "Explicit low scalar fallbacks must not inherit high list");

    const auto empty_low = yolo::loadAppConfig(writeConfig(
        "class_empty_low", base +
        "low_res_class_conf_thresholds: [0.4, 0.4, 0.4]\n"
        "low_res_class_conf_thresholds: []\n"
    ).string());
    expect(yolo::makeLowResAppConfig(empty_low).class_conf_thresholds == high_values,
           "Empty repeated list should reset and inherit high thresholds");

    const auto block_high = yolo::loadAppConfig(writeConfig(
        "class_block_boundaries",
        "num_classes: 3\nclass_conf_thresholds:\n  - 0\n  - 1\n  - -1\n"
    ).string());
    expect(block_high.class_conf_thresholds == std::vector<float>({0.0F, 1.0F, -1.0F}),
           "Block high thresholds or boundary values mismatch");
}

void testInvalidClassThresholdConfig() {
    for (const std::string key : {"class_conf_thresholds", "low_res_class_conf_thresholds"}) {
        for (const std::string value : {"[0.2]", "[0.2, 0.3, 0.4]"}) {
            expectThrowsWith("class_length", "num_classes: 2\n" + key + ": " + value + "\n",
                             "class threshold list size must match");
        }
        expectThrowsWith("class_unknown_count", key + ": [0.2]\n",
                         "class threshold list size must match");
        for (const std::string value : {"[-0.5, 0.2]", "[0.2, 1.1]"}) {
            expectThrowsWith("class_range", "num_classes: 2\n" + key + ": " + value + "\n",
                             "class thresholds must be -1 or in [0, 1]");
        }
        for (const std::string value : {"[nan, 0.2]", "[0.2, inf]", "[, 0.2]",
                                       "[0.2,,0.3]", "[0.2, '']", "[0.2, nope]"}) {
            expectThrowsWith("class_invalid_float", "num_classes: 2\n" + key + ": " + value + "\n",
                             "Invalid float");
        }
        expectThrowsWith("class_scalar", "num_classes: 2\n" + key + ": 0.2\n", "must be a list");
        expectThrowsWith("class_trailing_comma", "num_classes: 2\n" + key + ": [0.2,]\n",
                         "cannot contain an empty item");
        expectThrowsWith("class_empty_block_item", "num_classes: 2\n" + key + ":\n  - 0.2\n  -\n",
                         "Invalid float");
    }
    for (const std::string key : {"conf_threshold", "low_res_conf_threshold",
                                  "video_detect_fps", "api_rate_limit_requests_per_second"}) {
        for (const std::string value : {"nan", "inf", "-inf"}) {
            expectThrowsWith("nonfinite_scalar", key + ": " + value + "\n", "Invalid float");
        }
    }
}

}  // namespace

int main() {
    testOldConfigStaysCompatible();
    testVideoAsyncKeysStayCompatible();
    testOpenVinoCpuPinning();
    testLowResConfigDerivesEngineConfig();
    testExplicitLowResThresholds();
    testScopedRuntimeConfig();
    testInvalidRuntimeConfigFails();
    testLowResModelRequiresDimensions();
    testInvalidLowResThresholdFails();
    testInvalidBackendFails();
    testClassThresholdConfig();
    testInvalidClassThresholdConfig();
    std::cout << "app_config_test passed\n";
    return 0;
}
