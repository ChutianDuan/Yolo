#include <iostream>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>

#include <opencv2/core.hpp>

#include "config/app_config.h"
#include "drogon/api_server.h"
#include "model/yolo_engine.h"

#ifndef YOLO_DEFAULT_CONFIG_PATH
#define YOLO_DEFAULT_CONFIG_PATH "config.yaml"
#endif

int main(int argc, char* argv[]) {
    const std::string config_path = argc > 1 ? argv[1] : YOLO_DEFAULT_CONFIG_PATH;
    constexpr uint16_t port = 8080;

    yolo::AppConfig config;
    try {
        config = yolo::loadAppConfig(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << config_path << '\n'
                  << e.what() << '\n';
        return 1;
    }

    if (config.opencv_threads > 0) {
        cv::setNumThreads(config.opencv_threads);
    }

    const yolo::AppConfig high_res_config = yolo::makeHighResAppConfig(config);
    std::shared_ptr<yolo::YoloEngine> engine;
    try {
        engine = std::make_shared<yolo::YoloEngine>(high_res_config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load model: " << high_res_config.model_path << '\n'
                  << e.what() << '\n';
        return 1;
    }

    std::shared_ptr<yolo::YoloEngine> low_res_engine;
    if (yolo::hasLowResModelConfig(config)) {
        const yolo::AppConfig low_res_config = yolo::makeLowResAppConfig(config);
        try {
            low_res_engine = std::make_shared<yolo::YoloEngine>(low_res_config);
        } catch (const std::exception& e) {
            std::cerr << "Failed to load low-res model: "
                      << low_res_config.model_path << '\n'
                      << e.what() << '\n';
            return 1;
        }
    }

    try {
        yolo::runApiServer(engine, low_res_engine, config, port);
    } catch (const std::exception& e) {
        std::cerr << "Failed to run API server:\n" << e.what() << '\n';
        return 1;
    }

    return 0;
}
