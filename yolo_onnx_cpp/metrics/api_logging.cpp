#include "metrics/api_logging.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <string_view>

#include <json/json.h>
#include <trantor/utils/Logger.h>

namespace yolo::api {
namespace {

struct EventInfo {
    const char* name;
    trantor::Logger::LogLevel level;
};

EventInfo eventInfo(ApiLogEvent event) {
    using Logger = trantor::Logger;
    switch (event) {
    case ApiLogEvent::Started: return {"request_started", Logger::kInfo};
    case ApiLogEvent::Completed: return {"request_completed", Logger::kInfo};
    case ApiLogEvent::Rejected: return {"request_rejected", Logger::kWarn};
    case ApiLogEvent::Failed: return {"request_failed", Logger::kError};
    }
    return {"unknown_api_event", Logger::kError};
}

const char* levelName(trantor::Logger::LogLevel level) {
    return level >= trantor::Logger::kError ? "error"
        : level >= trantor::Logger::kWarn ? "warning" : "info";
}

void addFiniteNonNegative(Json::Value& json, const char* key, double value) {
    if (std::isfinite(value) && value >= 0.0) {
        json[key] = value;
    }
}

}  // namespace

std::string nextApiRequestId() {
    static const auto process_epoch_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    static std::atomic<uint64_t> next_id{1};
    return "api-" + std::to_string(process_epoch_ms) + "-"
        + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed));
}

std::string formatApiLog(ApiLogEvent event, const ApiLogRecord& record) {
    const EventInfo info = eventInfo(event);
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    Json::Value json(Json::objectValue);
    json["schema_version"] = 1;
    json["component"] = "api_inference";
    json["timestamp_unix_ms"] = Json::Int64(timestamp);
    json["level"] = levelName(info.level);
    json["event"] = info.name;
    json["request_id"] = record.request_id;
    json["route"] = record.route;
    json["media_type"] = record.media_type;
    if (!record.stream_id.empty()) {
        json["stream_id"] = record.stream_id;
    }
    if (!record.reason_code.empty()) {
        json["reason_code"] = record.reason_code;
    }
    if (record.status_code > 0) {
        json["status_code"] = record.status_code;
    }
    if (record.has_content_size) {
        json["content_size"] = Json::UInt64(record.content_size);
    }
    if (event == ApiLogEvent::Completed) {
        if (record.media_type == "image") {
            json["detection_count"] = Json::UInt64(record.detection_count);
        } else if (record.media_type == "video") {
            json["frame_count"] = Json::UInt64(record.frame_count);
            json["processed_frame_count"] =
                Json::UInt64(record.processed_frame_count);
            json["detected_frame_count"] =
                Json::UInt64(record.detected_frame_count);
            json["track_observation_count"] =
                Json::UInt64(record.track_observation_count);
        }
    }
    addFiniteNonNegative(json, "elapsed_ms", record.elapsed_ms);
    addFiniteNonNegative(json, "average_fps", record.average_fps);
    addFiniteNonNegative(
        json, "cpu_utilization_percent", record.cpu_utilization_percent
    );
    addFiniteNonNegative(json, "rss_memory_mb", record.rss_memory_mb);

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, json) + '\n';
}

void logApiEvent(ApiLogEvent event, const ApiLogRecord& record) noexcept {
    try {
        if (eventInfo(event).level < trantor::Logger::logLevel()) {
            return;
        }
        const std::string line = formatApiLog(event, record);
        if (line.size() < 3800) {
            LOG_RAW << line;
        }
    } catch (...) {
        // Logging must not change request processing or response delivery.
    }
}

}  // namespace yolo::api
