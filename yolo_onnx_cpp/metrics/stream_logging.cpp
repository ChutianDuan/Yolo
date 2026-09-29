#include "metrics/stream_logging.h"

#include <chrono>
#include <cmath>
#include <string_view>

#include <json/json.h>
#include <trantor/utils/Logger.h>

namespace yolo {
namespace {

struct EventInfo {
    const char* name;
    trantor::Logger::LogLevel level;
};

EventInfo eventInfo(StreamLogEvent event, const std::string& status) {
    using Logger = trantor::Logger;
    switch (event) {
    case StreamLogEvent::Created: return {"stream_created", Logger::kInfo};
    case StreamLogEvent::StartFailed: return {"stream_start_failed", Logger::kError};
    case StreamLogEvent::SourceConnected: return {"source_connected", Logger::kInfo};
    case StreamLogEvent::SourceUnavailable: return {"source_unavailable", Logger::kWarn};
    case StreamLogEvent::SourceDisconnected: return {"source_disconnected", Logger::kWarn};
    case StreamLogEvent::InferenceFailed: return {"inference_failed", Logger::kWarn};
    case StreamLogEvent::DecoderFailed: return {"decoder_failed", Logger::kError};
    case StreamLogEvent::ProcessorFailed: return {"processor_failed", Logger::kError};
    case StreamLogEvent::CircuitOpened: return {"circuit_opened", Logger::kError};
    case StreamLogEvent::LifetimeExpired: return {"stream_expired", Logger::kWarn};
    case StreamLogEvent::StopRequested: return {"stream_stop_requested", Logger::kInfo};
    case StreamLogEvent::Removed: return {"stream_removed", Logger::kInfo};
    case StreamLogEvent::Finished:
        return {"stream_finished", status == "failed" ? Logger::kError : Logger::kInfo};
    }
    return {"unknown_stream_event", Logger::kError};
}

const char* levelName(trantor::Logger::LogLevel level) {
    return level >= trantor::Logger::kError ? "error"
        : level >= trantor::Logger::kWarn ? "warning" : "info";
}

}  // namespace

bool shouldLogRepeatedFailure(uint64_t count) {
    return count != 0 && (count & (count - 1)) == 0;
}

std::string formatStreamLog(
    StreamLogEvent event,
    const RealtimeStreamSnapshot& snapshot,
    const InferenceContext& inference,
    const char* model_tier
) {
    const auto info = eventInfo(event, snapshot.status);
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    Json::Value json(Json::objectValue);
    json["schema_version"] = 1;
    json["component"] = "realtime_stream";
    json["timestamp_unix_ms"] = Json::Int64(timestamp);
    json["level"] = levelName(info.level);
    json["event"] = info.name;
    json["stream_id"] = snapshot.stream_id;
    json["status"] = snapshot.status;
    json["source_kind"] = snapshot.source.find("://") != std::string::npos
            && snapshot.source.rfind("file://", 0) != 0 ? "live" : "file";
    // Allowlist fields: never serialize source URLs, last_error, tokens or image data.
    json["width"] = snapshot.width;
    json["height"] = snapshot.height;
    json["decoded_frame_count"] = Json::UInt64(snapshot.decoded_frame_count);
    json["processed_frame_count"] = Json::UInt64(snapshot.processed_frame_count);
    json["detection_frame_count"] = Json::UInt64(snapshot.detection_frame_count);
    json["high_res_detection_count"] = Json::UInt64(snapshot.high_res_detection_count);
    json["roi_high_res_detection_count"] =
        Json::UInt64(snapshot.roi_high_res_detection_count);
    json["low_res_detection_count"] = Json::UInt64(snapshot.low_res_detection_count);
    json["dropped_frame_count"] = Json::UInt64(snapshot.dropped_frame_count);
    json["stale_frame_drop_count"] = Json::UInt64(snapshot.stale_frame_drop_count);
    json["skipped_inference_count"] = Json::UInt64(snapshot.skipped_inference_count);
    json["inference_error_count"] = Json::UInt64(snapshot.inference_error_count);
    json["consecutive_inference_error_count"] =
        Json::UInt64(snapshot.consecutive_inference_error_count);
    json["reconnect_count"] = Json::UInt64(snapshot.reconnect_count);
    if (snapshot.processed_frame_count > 0 && std::isfinite(snapshot.latest_result_age_ms)) {
        json["latest_result_age_ms"] = snapshot.latest_result_age_ms;
    }
    if (inference.frame_index >= 0) {
        json["frame_index"] = Json::Int64(inference.frame_index);
    }
    if (inference.timestamp_ms >= 0 && std::isfinite(inference.timestamp_ms)) {
        json["timestamp_ms"] = inference.timestamp_ms;
    }
    if (model_tier != nullptr
        && (std::string_view(model_tier) == "high" || std::string_view(model_tier) == "low")) {
        json["model_tier"] = model_tier;
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, json) + '\n';
}

void logStreamEvent(
    StreamLogEvent event,
    const RealtimeStreamSnapshot& snapshot,
    const InferenceContext& inference,
    const char* model_tier
) noexcept {
    try {
        if (eventInfo(event, snapshot.status).level < trantor::Logger::logLevel()) {
            return;
        }
        const auto line = formatStreamLog(event, snapshot, inference, model_tier);
        // Trantor's normal buffer is 4000 bytes; never emit a truncated JSON record.
        if (line.size() < 3800) {
            LOG_RAW << line;
        }
    } catch (...) {
        // Serialization failures must not count as model failures or open the circuit.
    }
}

}  // namespace yolo
