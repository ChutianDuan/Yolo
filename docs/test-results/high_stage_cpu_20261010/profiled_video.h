#pragma once

#include "video/video_inference.h"

namespace yolo {
// Experiment-only measurements; no changes to the production result schema.
struct StageProfile {
    std::vector<double> video_decode_ms;
    std::vector<double> lk_flow_ms;
    std::vector<double> byte_track_ms;
    double cpu_seconds = 0.0;
    double cpu_wall_ms = 0.0;
};
struct ProfiledVideoResult {
    VideoInferResult result;
    StageProfile profile;
};
ProfiledVideoResult profiledInferVideoFile(
    const std::shared_ptr<YoloEngine>& engine,
    const AppConfig& config,
    const std::filesystem::path& video_path,
    std::shared_ptr<InferenceScheduler> scheduler = nullptr,
    std::string stream_id = ""
);
} // namespace yolo
