cmake_minimum_required(VERSION 3.20)
project(high_quantization_experiment LANGUAGES CXX)
if(NOT YOLO_REPO_ROOT)
    message(FATAL_ERROR "Set YOLO_REPO_ROOT to the repository root")
endif()
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs video videoio)
find_package(onnxruntime CONFIG REQUIRED)
find_package(Drogon CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(OpenVINO REQUIRED COMPONENTS Runtime)
set(experiment_sources
    config/app_config.cpp image/image_processing.cpp
    model/inference_scheduler.cpp model/yolo_engine.cpp
    stream/stream_processor.cpp tracking/authority_tracker.cpp
    tracking/byte_tracker.cpp video/optical_flow_tracker.cpp
    video/video_inference_detail.cpp video/video_inference.cpp)
list(TRANSFORM experiment_sources PREPEND "${YOLO_REPO_ROOT}/yolo_onnx_cpp/")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(high_lk_runner runner.cpp ${experiment_sources})
target_compile_features(high_lk_runner PRIVATE cxx_std_17)
target_compile_definitions(high_lk_runner PRIVATE YOLO_ENABLE_OPENVINO=1)
target_include_directories(high_lk_runner PRIVATE "${YOLO_REPO_ROOT}/yolo_onnx_cpp")
target_compile_options(high_lk_runner PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(high_lk_runner PRIVATE
    opencv_core opencv_imgproc opencv_imgcodecs opencv_video opencv_videoio
    onnxruntime::onnxruntime openvino::runtime Drogon::Drogon)
