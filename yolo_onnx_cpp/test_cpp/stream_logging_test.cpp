#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>
#include <trantor/utils/Logger.h>

#include "metrics/stream_logging.h"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

Json::Value parseRecord(const std::string& line) {
    expect(!line.empty() && line.back() == '\n', "log record has no newline terminator");
    expect(std::count(line.begin(), line.end(), '\n') == 1, "log injection split a record");
    Json::CharReaderBuilder reader;
    reader["failIfExtra"] = true;
    Json::Value json;
    std::string errors;
    std::istringstream stream(line);
    expect(Json::parseFromStream(reader, stream, &json, &errors), "invalid JSON log: " + errors);
    expect(json.isObject(), "log is not an object");
    return json;
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}  // namespace

int main() {
    using yolo::StreamLogEvent;
    yolo::RealtimeStreamSnapshot snapshot;
    snapshot.stream_id = "cam-\"quoted\\\nname";
    snapshot.source = "rtsp://operator:password-SECRET@camera/path?token=TOKEN-SECRET";
    snapshot.last_error = "Bearer ERROR-SECRET\nraw backend diagnostic";
    snapshot.status = "running";
    snapshot.width = 1280;
    snapshot.height = 720;
    snapshot.decoded_frame_count = (uint64_t{1} << 40) + 7;
    snapshot.processed_frame_count = 18;
    snapshot.detection_frame_count = 4;
    snapshot.high_res_detection_count = 1;
    snapshot.roi_high_res_detection_count = 1;
    snapshot.low_res_detection_count = 3;
    snapshot.latest_result_age_ms = 42.5;
    snapshot.inference_error_count = 2;
    snapshot.consecutive_inference_error_count = 1;
    const yolo::InferenceContext context{"not-the-authoritative-id", 123, 456.5};
    const int64_t before = nowMs();
    const auto line = yolo::formatStreamLog(StreamLogEvent::InferenceFailed, snapshot, context, "high");
    const auto json = parseRecord(line);
    expect(json["schema_version"].asInt() == 1, "log schema version mismatch");
    expect(json["component"].asString() == "realtime_stream", "component mismatch");
    expect(json["event"].asString() == "inference_failed", "event mismatch");
    expect(json["level"].asString() == "warning", "severity mismatch");
    expect(json["stream_id"].asString() == snapshot.stream_id, "stream ID escaping changed content");
    expect(json["source_kind"].asString() == "live", "source classification mismatch");
    expect(json["frame_index"].asInt64() == 123 && json["timestamp_ms"].asDouble() == 456.5,
           "inference context was lost");
    expect(json["model_tier"].asString() == "high", "model tier mismatch");
    expect(json["decoded_frame_count"].asUInt64() == snapshot.decoded_frame_count,
           "64-bit log counter was truncated");
    expect(json["high_res_detection_count"].asUInt64() == 1
               && json["roi_high_res_detection_count"].asUInt64() == 1
               && json["low_res_detection_count"].asUInt64() == 3,
           "high/low detection counts mismatch");
    expect(json["timestamp_unix_ms"].asInt64() >= before
               && json["timestamp_unix_ms"].asInt64() <= nowMs(), "wall-clock timestamp mismatch");
    for (const std::string secret : {
             "operator", "password-SECRET", "TOKEN-SECRET", "ERROR-SECRET",
             "raw backend diagnostic", "not-the-authoritative-id"}) {
        expect(line.find(secret) == std::string::npos, "sensitive/untrusted field leaked into log");
    }
    expect(!json.isMember("source") && !json.isMember("last_error"), "log field allowlist was bypassed");

    snapshot.processed_frame_count = 0;
    const auto initial = parseRecord(yolo::formatStreamLog(StreamLogEvent::Created, snapshot));
    expect(!initial.isMember("latest_result_age_ms") && !initial.isMember("frame_index")
               && !initial.isMember("timestamp_ms") && !initial.isMember("model_tier"),
           "initial log invented frame context or result freshness");
    snapshot.processed_frame_count = 1;
    snapshot.latest_result_age_ms = std::numeric_limits<double>::infinity();
    auto invalid_context = context;
    invalid_context.timestamp_ms = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = parseRecord(yolo::formatStreamLog(
        StreamLogEvent::InferenceFailed, snapshot, invalid_context, "TOKEN-SECRET"
    ));
    expect(!invalid.isMember("latest_result_age_ms") && !invalid.isMember("timestamp_ms")
               && !invalid.isMember("model_tier"), "invalid optional telemetry was serialized");

    struct ExpectedEvent { StreamLogEvent event; const char* name; const char* level; };
    const std::vector<ExpectedEvent> cases{
        {StreamLogEvent::Created, "stream_created", "info"},
        {StreamLogEvent::StartFailed, "stream_start_failed", "error"},
        {StreamLogEvent::SourceConnected, "source_connected", "info"},
        {StreamLogEvent::SourceUnavailable, "source_unavailable", "warning"},
        {StreamLogEvent::SourceDisconnected, "source_disconnected", "warning"},
        {StreamLogEvent::InferenceFailed, "inference_failed", "warning"},
        {StreamLogEvent::DecoderFailed, "decoder_failed", "error"},
        {StreamLogEvent::ProcessorFailed, "processor_failed", "error"},
        {StreamLogEvent::CircuitOpened, "circuit_opened", "error"},
        {StreamLogEvent::LifetimeExpired, "stream_expired", "warning"},
        {StreamLogEvent::StopRequested, "stream_stop_requested", "info"},
        {StreamLogEvent::Finished, "stream_finished", "info"},
        {StreamLogEvent::Removed, "stream_removed", "info"},
    };
    snapshot.source = "file:///local/video.avi";
    for (const auto& expected : cases) {
        const auto record = parseRecord(yolo::formatStreamLog(expected.event, snapshot));
        expect(record["event"].asString() == expected.name && record["level"].asString() == expected.level,
               "lifecycle event mapping mismatch");
        expect(record["source_kind"].asString() == "file", "file URI classified as live");
    }
    snapshot.status = "failed";
    expect(parseRecord(yolo::formatStreamLog(StreamLogEvent::Finished, snapshot))["level"].asString()
               == "error", "failed terminal event was not logged as an error");
    snapshot.status = "running";

    for (uint64_t count = 0; count <= 20; ++count) {
        const bool expected = count == 1 || count == 2 || count == 4 || count == 8 || count == 16;
        expect(yolo::shouldLogRepeatedFailure(count) == expected, "retry log thinning mismatch");
    }
    expect(yolo::shouldLogRepeatedFailure(uint64_t{1} << 63), "large retry count was mishandled");
    expect(!yolo::shouldLogRepeatedFailure(std::numeric_limits<uint64_t>::max()),
           "overflow-edge retry count was logged");

    std::mutex output_mutex;
    std::vector<std::string> records;
    const auto previous_level = trantor::Logger::logLevel();
    trantor::Logger::setOutputFunction(
        [&](const char* data, uint64_t length) {
            std::lock_guard<std::mutex> lock(output_mutex);
            records.emplace_back(data, static_cast<size_t>(length));
        },
        []() {}
    );
    trantor::Logger::setLogLevel(trantor::Logger::kError);
    yolo::logStreamEvent(StreamLogEvent::Created, snapshot);
    expect(records.empty(), "info log ignored the configured log level");
    yolo::logStreamEvent(StreamLogEvent::CircuitOpened, snapshot, context, "low");
    expect(records.size() == 1, "error log was filtered out");
    expect(parseRecord(records.front())["model_tier"].asString() == "low", "raw sink changed JSON");
    auto oversized = snapshot;
    oversized.stream_id.assign(5000, 'x');
    yolo::logStreamEvent(StreamLogEvent::CircuitOpened, oversized);
    expect(records.size() == 1, "oversized log produced a partial/truncated JSON record");

    records.clear();
    trantor::Logger::setLogLevel(trantor::Logger::kInfo);
    std::vector<std::thread> writers;
    for (int worker = 0; worker < 4; ++worker) {
        writers.emplace_back([&, worker]() {
            auto local = snapshot;
            local.stream_id = "camera-" + std::to_string(worker);
            for (int i = 0; i < 64; ++i) {
                yolo::logStreamEvent(StreamLogEvent::SourceConnected, local);
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    expect(records.size() == 256, "concurrent log records were lost or split");
    std::map<std::string, size_t> counts;
    for (const auto& record : records) {
        ++counts[parseRecord(record)["stream_id"].asString()];
    }
    expect(counts.size() == 4, "concurrent log stream identities were mixed");
    for (const auto& [id, count] : counts) {
        (void)id;
        expect(count == 64, "per-stream log count mismatch");
    }
    trantor::Logger::setLogLevel(previous_level);
    trantor::Logger::setOutputFunction(
        [](const char* data, uint64_t length) { std::fwrite(data, 1, static_cast<size_t>(length), stdout); },
        []() { std::fflush(stdout); }
    );
    std::cout << "stream_logging_test passed\n";
}
