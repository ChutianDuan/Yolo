#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include "model/inference_types.h"
#include "video/video_inference.h"

namespace yolo {

struct VideoFrameJsonOptions {
    bool include_frames = true;
    size_t frame_offset = 0;
    size_t frame_limit = 0;
};

drogon::HttpResponsePtr makeJsonResponse(
    int code,
    const std::string& message,
    drogon::HttpStatusCode status
);

Json::Value shapesToJson(const std::vector<std::vector<int64_t>>& shapes);

Json::Value detectionsToJson(
    const std::vector<Detection>& detections,
    const std::vector<std::string>& class_names
);

Json::Value tracksToJson(
    const std::vector<TrackedDetection>& tracks,
    const std::vector<std::string>& class_names
);

Json::Value inferResultToJson(
    const InferResult& result,
    const std::vector<std::string>& class_names
);

Json::Value videoInferResultToJson(
    const VideoInferResult& result,
    const std::vector<std::string>& class_names
);

Json::Value videoInferResultToJson(
    const VideoInferResult& result,
    const std::vector<std::string>& class_names,
    const VideoFrameJsonOptions& options
);

}  // namespace yolo
