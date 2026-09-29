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
find_package(jsoncpp CONFIG REQUIRED)

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
    drogon/api_gateway.cpp
    drogon/api_server.cpp
    drogon/inference_handlers.cpp
    drogon/realtime_stream_handlers.cpp
    drogon/request_gate.cpp
    drogon/video_job_executor.cpp
    drogon/request_utils.cpp
    drogon/response_json.cpp
    image/image_processing.cpp
    main.cpp
    metrics/api_logging.cpp
    metrics/stream_logging.cpp
    model/inference_scheduler.cpp
    model/yolo_engine.cpp
    stream/realtime_stream_manager.cpp
    stream/stream_processor.cpp
    tracking/authority_tracker.cpp
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

add_executable(onnx_thread_benchmark
    image/image_processing.cpp
    model/yolo_engine.cpp
    tools/onnx_thread_benchmark.cpp
)

target_include_directories(onnx_thread_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(onnx_thread_benchmark PRIVATE
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
    target_compile_definitions(onnx_thread_benchmark PRIVATE YOLO_ENABLE_OPENVINO=1)
endif()

target_compile_options(onnx_thread_benchmark PRIVATE
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

    add_executable(detection_cadence_test test_cpp/detection_cadence_test.cpp)
    target_include_directories(detection_cadence_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(detection_cadence_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME detection_cadence_test COMMAND detection_cadence_test)

    add_executable(stream_processor_test
        stream/stream_processor.cpp
        tracking/authority_tracker.cpp
        tracking/byte_tracker.cpp
        video/optical_flow_tracker.cpp
        test_cpp/stream_processor_test.cpp
    )
    target_include_directories(stream_processor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(stream_processor_test PRIVATE opencv_core opencv_imgproc opencv_video)
    target_compile_options(stream_processor_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME stream_processor_test COMMAND stream_processor_test)
    set_tests_properties(stream_processor_test PROPERTIES TIMEOUT 30)

    add_executable(frame_deadline_test test_cpp/frame_deadline_test.cpp)
    target_include_directories(frame_deadline_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(frame_deadline_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME frame_deadline_test COMMAND frame_deadline_test)

    add_executable(stream_metrics_test test_cpp/stream_metrics_test.cpp)
    target_include_directories(stream_metrics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(stream_metrics_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME stream_metrics_test COMMAND stream_metrics_test)

    add_executable(stream_logging_test
        metrics/stream_logging.cpp
        test_cpp/stream_logging_test.cpp
    )
    target_include_directories(stream_logging_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(stream_logging_test PRIVATE
        JsonCpp::JsonCpp Trantor::Trantor Threads::Threads
    )
    target_compile_options(stream_logging_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME stream_logging_test COMMAND stream_logging_test)

    add_executable(api_logging_test
        metrics/api_logging.cpp
        test_cpp/api_logging_test.cpp
    )
    target_include_directories(api_logging_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(api_logging_test PRIVATE
        JsonCpp::JsonCpp Trantor::Trantor Threads::Threads
    )
    target_compile_options(api_logging_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME api_logging_test COMMAND api_logging_test)

    add_executable(request_gate_test
        drogon/request_gate.cpp
        test_cpp/request_gate_test.cpp
    )

    target_include_directories(request_gate_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(request_gate_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_link_libraries(request_gate_test PRIVATE
        Threads::Threads
    )

    add_test(NAME request_gate_test COMMAND request_gate_test)

    add_executable(video_job_executor_test
        drogon/video_job_executor.cpp
        test_cpp/video_job_executor_test.cpp
    )

    target_include_directories(video_job_executor_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(video_job_executor_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_link_libraries(video_job_executor_test PRIVATE
        Threads::Threads
    )

    add_test(NAME video_job_executor_test COMMAND video_job_executor_test)

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
        model/inference_scheduler.cpp
        model/yolo_engine.cpp
        stream/stream_processor.cpp
        tracking/authority_tracker.cpp
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
        model/inference_scheduler.cpp
        model/yolo_engine.cpp
        stream/stream_processor.cpp
        tracking/authority_tracker.cpp
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
        if(YOLO_ENABLE_OPENVINO)
            set(VIDEO_STAGING_TEST_BACKEND openvino)
        else()
            set(VIDEO_STAGING_TEST_BACKEND onnx)
        endif()
        add_test(
            NAME video_staging_http_test
            COMMAND
                ${CONDA_EXECUTABLE} run -n yolo python
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_video_staging_http.py
                --server $<TARGET_FILE:yolo_api>
                --model ${CMAKE_CURRENT_SOURCE_DIR}/deploy/best_640x384.onnx
                --backend ${VIDEO_STAGING_TEST_BACKEND}
        )
        set_tests_properties(video_staging_http_test PROPERTIES
            TIMEOUT 60
            RESOURCE_LOCK yolo_api_port_8080
        )

        add_test(
            NAME multistream_acceptance_report_test
            COMMAND
                ${CONDA_EXECUTABLE} run -n yolo python
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_multistream_acceptance.py
        )
        set_tests_properties(multistream_acceptance_report_test PROPERTIES TIMEOUT 30)

        add_test(
            NAME bdd100k_tracking_evaluation_test
            COMMAND
                ${CONDA_EXECUTABLE} run -n yolo python
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_bdd100k_tracking_evaluation.py
        )
        set_tests_properties(bdd100k_tracking_evaluation_test PROPERTIES TIMEOUT 30)

        add_test(
            NAME bdd100k_openvino_quantization_test
            COMMAND
                ${CONDA_EXECUTABLE} run -n yolo python
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_bdd100k_openvino_quantization.py
        )
        set_tests_properties(bdd100k_openvino_quantization_test PROPERTIES TIMEOUT 30)

        add_test(
            NAME training_quality_pipeline_test
            COMMAND
                ${CONDA_EXECUTABLE} run -n yolo python
                ${CMAKE_CURRENT_SOURCE_DIR}/../model/test_training_quality_pipeline.py
        )
        set_tests_properties(training_quality_pipeline_test PROPERTIES TIMEOUT 30)

        add_test(
            NAME distillation_pipeline_test
            COMMAND
                ${CONDA_EXECUTABLE} run -n yolo python
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_distillation_pipeline.py
        )
        set_tests_properties(distillation_pipeline_test PROPERTIES TIMEOUT 30)

        add_test(
            NAME bdd100k_yolo_validation_test
            COMMAND
                ${CONDA_EXECUTABLE} run -n yolo python
                ${CMAKE_CURRENT_SOURCE_DIR}/test/test_bdd100k_yolo_validation.py
        )
        set_tests_properties(bdd100k_yolo_validation_test PROPERTIES TIMEOUT 30)

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

    add_executable(realtime_stream_manager_test
        config/app_config.cpp
        image/image_processing.cpp
        drogon/realtime_stream_handlers.cpp
        drogon/request_gate.cpp
        drogon/request_utils.cpp
        drogon/response_json.cpp
        drogon/video_job_executor.cpp
        model/inference_scheduler.cpp
        model/yolo_engine.cpp
        stream/realtime_stream_manager.cpp
        metrics/stream_logging.cpp
        stream/stream_processor.cpp
        tracking/authority_tracker.cpp
        tracking/byte_tracker.cpp
        video/optical_flow_tracker.cpp
        test_cpp/realtime_stream_manager_test.cpp
    )

    target_include_directories(realtime_stream_manager_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_definitions(realtime_stream_manager_test PRIVATE
        YOLO_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(YOLO_ENABLE_OPENVINO)
        target_compile_definitions(realtime_stream_manager_test PRIVATE YOLO_ENABLE_OPENVINO=1)
    endif()

    target_link_libraries(realtime_stream_manager_test PRIVATE
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_video
        opencv_videoio
        ${ORT_TARGET}
        ${OPENVINO_RUNTIME_TARGET}
        Drogon::Drogon
    )

    target_compile_options(realtime_stream_manager_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME realtime_stream_manager_test COMMAND realtime_stream_manager_test)
    set_tests_properties(realtime_stream_manager_test PROPERTIES TIMEOUT 60)

    add_executable(inference_scheduler_test
        image/image_processing.cpp
        model/inference_scheduler.cpp
        model/yolo_engine.cpp
        test_cpp/inference_scheduler_test.cpp
    )

    target_include_directories(inference_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_definitions(inference_scheduler_test PRIVATE
        YOLO_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(YOLO_ENABLE_OPENVINO)
        target_compile_definitions(inference_scheduler_test PRIVATE YOLO_ENABLE_OPENVINO=1)
    endif()

    target_link_libraries(inference_scheduler_test PRIVATE
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_video
        opencv_videoio
        ${ORT_TARGET}
        ${OPENVINO_RUNTIME_TARGET}
        Drogon::Drogon
    )

    target_compile_options(inference_scheduler_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME inference_scheduler_test COMMAND inference_scheduler_test)
    set_tests_properties(inference_scheduler_test PROPERTIES TIMEOUT 120)

    add_executable(yolo_engine_concurrency_test
        image/image_processing.cpp
        model/yolo_engine.cpp
        test_cpp/yolo_engine_concurrency_test.cpp
    )

    target_include_directories(yolo_engine_concurrency_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_definitions(yolo_engine_concurrency_test PRIVATE
        YOLO_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(YOLO_ENABLE_OPENVINO)
        target_compile_definitions(yolo_engine_concurrency_test PRIVATE YOLO_ENABLE_OPENVINO=1)
    endif()

    target_link_libraries(yolo_engine_concurrency_test PRIVATE
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_video
        opencv_videoio
        ${ORT_TARGET}
        ${OPENVINO_RUNTIME_TARGET}
        Drogon::Drogon
    )

    target_compile_options(yolo_engine_concurrency_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME yolo_engine_concurrency_test COMMAND yolo_engine_concurrency_test)
    set_tests_properties(yolo_engine_concurrency_test PROPERTIES TIMEOUT 120)

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

    add_executable(api_gateway_contract_test
        drogon/request_utils.cpp
        test_cpp/api_gateway_contract_test.cpp
    )

    target_include_directories(api_gateway_contract_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(api_gateway_contract_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME api_gateway_contract_test COMMAND api_gateway_contract_test)
endif()
