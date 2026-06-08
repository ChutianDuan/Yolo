#include <iostream>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>

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

    std::shared_ptr<yolo::YoloEngine> engine;
    try {
        engine = std::make_shared<yolo::YoloEngine>(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load ONNX model: " << config.model_path << '\n'
                  << e.what() << '\n';
        return 1;
    }

    yolo::runApiServer(engine, config, port);
    return 0;
}
