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

option(YOLO_ENABLE_OPENVINO "Build optional OpenVINO inference backend" OFF)

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

set(OPENVINO_RUNTIME_TARGET "")
if(YOLO_ENABLE_OPENVINO)
    find_package(OpenVINO REQUIRED COMPONENTS Runtime)
    if(TARGET openvino::runtime)
        set(OPENVINO_RUNTIME_TARGET openvino::runtime)
    elseif(TARGET OpenVINO::runtime)
        set(OPENVINO_RUNTIME_TARGET OpenVINO::runtime)
    else()
        message(FATAL_ERROR "OpenVINO runtime target not found. Expected openvino::runtime or OpenVINO::runtime.")
    endif()
    message(STATUS "OpenVINO backend: enabled")
else()
    message(STATUS "OpenVINO backend: disabled")
endif()

set(YOLO_DEFAULT_CONFIG_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml"
    CACHE FILEPATH "Default config file used by yolo_api"
)
set(YOLO_REFERENCE_CONFIG_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/config.reference.yaml"
    CACHE FILEPATH "Reference full-frame model config file used by yolo_api"
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
    video/video_inference_high_low.cpp
    video/video_inference_detail.cpp
    video/video_inference.cpp
)

target_include_directories(yolo_api PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(yolo_api PRIVATE
    YOLO_DEFAULT_CONFIG_PATH="${YOLO_RESOLVED_CONFIG_PATH}"
)
if(YOLO_ENABLE_OPENVINO)
    target_compile_definitions(yolo_api PRIVATE YOLO_ENABLE_OPENVINO=1)
endif()

target_link_libraries(yolo_api PRIVATE
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_video
    opencv_videoio
    ${ORT_TARGET}
    ${OPENVINO_RUNTIME_TARGET}
    Drogon::Drogon
)

target_compile_options(yolo_api PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

include(CTest)

if(BUILD_TESTING)
    add_executable(app_config_test
        config/app_config.cpp
        test_cpp/app_config_test.cpp
    )

    target_include_directories(app_config_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(app_config_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME app_config_test COMMAND app_config_test)

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

    add_executable(authority_tracker_policy_test
        config/app_config.cpp
        image/image_processing.cpp
        model/yolo_engine.cpp
        tracking/byte_tracker.cpp
        video/optical_flow_tracker.cpp
        video/video_inference_detail.cpp
        video/video_inference.cpp
        test_cpp/authority_tracker_policy_test.cpp
    )

    target_include_directories(authority_tracker_policy_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(authority_tracker_policy_test PRIVATE
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_video
        opencv_videoio
        ${ORT_TARGET}
        ${OPENVINO_RUNTIME_TARGET}
        Drogon::Drogon
    )
    if(YOLO_ENABLE_OPENVINO)
        target_compile_definitions(authority_tracker_policy_test PRIVATE YOLO_ENABLE_OPENVINO=1)
    endif()

    target_compile_options(authority_tracker_policy_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME authority_tracker_policy_test COMMAND authority_tracker_policy_test)

    add_executable(video_inference_compare_test
        config/app_config.cpp
        image/image_processing.cpp
        model/yolo_engine.cpp
        tracking/byte_tracker.cpp
        video/optical_flow_tracker.cpp
        video/video_inference_high_low.cpp
        video/video_inference_detail.cpp
        video/video_inference.cpp
        test_cpp/video_inference_compare_test.cpp
    )

    target_include_directories(video_inference_compare_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_definitions(video_inference_compare_test PRIVATE
        YOLO_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_link_libraries(video_inference_compare_test PRIVATE
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_video
        opencv_videoio
        ${ORT_TARGET}
        ${OPENVINO_RUNTIME_TARGET}
        Drogon::Drogon
    )
    if(YOLO_ENABLE_OPENVINO)
        target_compile_definitions(video_inference_compare_test PRIVATE YOLO_ENABLE_OPENVINO=1)
    endif()

    target_compile_options(video_inference_compare_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME video_inference_compare_test COMMAND video_inference_compare_test)
    set_tests_properties(video_inference_compare_test PROPERTIES TIMEOUT 180)

    find_program(CONDA_EXECUTABLE conda)
    if(CONDA_EXECUTABLE)
        add_test(
            NAME openvino_python_smoke_test
            COMMAND
                ${CONDA_EXECUTABLE} run -n yolo python
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/openvino_smoke_test.py
                --onnx ${CMAKE_CURRENT_SOURCE_DIR}/deploy/best_640x384.onnx
                --video ${CMAKE_CURRENT_SOURCE_DIR}/../Readme/dynamic_onnx_flow_detections.mp4
                --output-dir ${CMAKE_CURRENT_BINARY_DIR}/openvino-smoke
                --input-width 640
                --input-height 384
                --max-abs-tol 0.001
                --mean-abs-tol 0.0001
        )
        set_tests_properties(openvino_python_smoke_test PROPERTIES
            TIMEOUT 180
            SKIP_REGULAR_EXPRESSION "SKIP_OPENVINO"
        )
    else()
        message(STATUS "OpenVINO Python smoke test: disabled because conda was not found")
    endif()

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
