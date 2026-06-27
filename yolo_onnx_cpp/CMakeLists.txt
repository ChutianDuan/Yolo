cmake_minimum_required(VERSION 3.20)

project(yolo_api
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

message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "VCPKG target triplet: ${VCPKG_TARGET_TRIPLET}")

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs video videoio)
find_package(onnxruntime CONFIG REQUIRED)
find_package(Drogon CONFIG REQUIRED)

message(STATUS "OpenCV version: ${OpenCV_VERSION}")

if(TARGET onnxruntime::onnxruntime)
    set(ORT_TARGET onnxruntime::onnxruntime)
elseif(TARGET onnxruntime)
    set(ORT_TARGET onnxruntime)
elseif(TARGET Microsoft.ML.OnnxRuntime)
    set(ORT_TARGET Microsoft.ML.OnnxRuntime)
else()
    message(FATAL_ERROR "ONNX Runtime target not found. Please check vcpkg onnxruntime package export target.")
endif()

set(YOLO_DEFAULT_CONFIG_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml"
    CACHE FILEPATH "Default config file used by yolo_api"
)
set(YOLO_REFERENCE_CONFIG_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/config.reference.yaml"
    CACHE FILEPATH "Reference full-frame ONNX config file used by yolo_api"
)
set(YOLO_CONFIG_PROFILE
    "default"
    CACHE STRING "Default config profile used by yolo_api: default or reference"
)
set_property(CACHE YOLO_CONFIG_PROFILE PROPERTY STRINGS default reference)

if(YOLO_CONFIG_PROFILE STREQUAL "reference")
    set(YOLO_RESOLVED_CONFIG_PATH "${YOLO_REFERENCE_CONFIG_PATH}")
elseif(YOLO_CONFIG_PROFILE STREQUAL "default")
    set(YOLO_RESOLVED_CONFIG_PATH "${YOLO_DEFAULT_CONFIG_PATH}")
else()
    message(FATAL_ERROR "YOLO_CONFIG_PROFILE must be default or reference, got: ${YOLO_CONFIG_PROFILE}")
endif()

message(STATUS "YOLO config profile: ${YOLO_CONFIG_PROFILE}")
message(STATUS "YOLO resolved config path: ${YOLO_RESOLVED_CONFIG_PATH}")

add_executable(yolo_api
    config/app_config.cpp
    drogon/api_server.cpp
    drogon/response_json.cpp
    image/image_processing.cpp
    main.cpp
    model/yolo_engine.cpp
    tracking/byte_tracker.cpp
    video/optical_flow_tracker.cpp
    video/video_inference_detail.cpp
    video/video_inference.cpp
)

target_include_directories(yolo_api PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(yolo_api PRIVATE
    YOLO_DEFAULT_CONFIG_PATH="${YOLO_RESOLVED_CONFIG_PATH}"
)

target_link_libraries(yolo_api PRIVATE
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_video
    opencv_videoio
    ${ORT_TARGET}
    Drogon::Drogon
)

target_compile_options(yolo_api PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

include(CTest)

if(BUILD_TESTING)
    add_executable(image_processing_test
        image/image_processing.cpp
        test_cpp/image_processing_test.cpp
        video/optical_flow_tracker.cpp
    )

    target_include_directories(image_processing_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(image_processing_test PRIVATE
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_video
        opencv_videoio
    )

    target_compile_options(image_processing_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME image_processing_test COMMAND image_processing_test)

    add_executable(response_json_test
        drogon/response_json.cpp
        test_cpp/response_json_test.cpp
    )

    target_include_directories(response_json_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(response_json_test PRIVATE
        Drogon::Drogon
    )

    target_compile_options(response_json_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME response_json_test COMMAND response_json_test)
endif()
