cmake_minimum_required(VERSION 3.20)

project(yolo_qt
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_AUTOMOC ON)

set(YOLO_CPP_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../yolo_onnx_cpp"
    CACHE PATH "Path to the existing yolo_onnx_cpp source directory"
)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs videoio)
find_package(onnxruntime CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

if(TARGET onnxruntime::onnxruntime)
    set(ORT_TARGET onnxruntime::onnxruntime)
elseif(TARGET onnxruntime)
    set(ORT_TARGET onnxruntime)
elseif(TARGET Microsoft.ML.OnnxRuntime)
    set(ORT_TARGET Microsoft.ML.OnnxRuntime)
else()
    message(FATAL_ERROR "ONNX Runtime target not found. Please check vcpkg onnxruntime package export target.")
endif()

add_executable(yolo_qt
    MainWindow.cpp
    MainWindow.h
    main.cpp
    "${YOLO_CPP_DIR}/config/app_config.cpp"
    "${YOLO_CPP_DIR}/image/image_processing.cpp"
    "${YOLO_CPP_DIR}/model/yolo_engine.cpp"
)

target_include_directories(yolo_qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${YOLO_CPP_DIR}"
)

target_link_libraries(yolo_qt PRIVATE
    Qt6::Widgets
    ${OpenCV_LIBS}
    ${ORT_TARGET}
    JsonCpp::JsonCpp
)

if(MSVC)
    target_compile_options(yolo_qt PRIVATE /W4)
else()
    target_compile_options(yolo_qt PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
