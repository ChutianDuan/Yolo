#pragma once

#include <cstdint>
#include <string>

namespace yolo::api {

enum class ApiLogEvent {
    Started,
    Completed,
    Rejected,
    Failed,
};

struct ApiLogRecord {
    std::string request_id;
    std::string route;
    std::string media_type;
    std::string stream_id;
    std::string reason_code;
    int status_code = 0;
    uint64_t content_size = 0;
    bool has_content_size = false;
    uint64_t detection_count = 0;
    uint64_t frame_count = 0;
    uint64_t processed_frame_count = 0;
    uint64_t detected_frame_count = 0;
    uint64_t track_observation_count = 0;
    double elapsed_ms = -1.0;
    double average_fps = -1.0;
    double cpu_utilization_percent = -1.0;
    double rss_memory_mb = -1.0;
};

std::string nextApiRequestId();
std::string formatApiLog(ApiLogEvent event, const ApiLogRecord& record);
void logApiEvent(ApiLogEvent event, const ApiLogRecord& record) noexcept;

}  // namespace yolo::api
