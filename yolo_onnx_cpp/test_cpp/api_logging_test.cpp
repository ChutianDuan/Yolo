#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>

#include "metrics/api_logging.h"

namespace {

void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

Json::Value parseRecord(const std::string& record) {
    expect(!record.empty() && record.back() == '\n', "log is not newline terminated");
    expect(record.find('\n') == record.size() - 1, "log contains multiple lines");
    Json::CharReaderBuilder reader;
    Json::Value json;
    std::string errors;
    std::istringstream input(record);
    expect(Json::parseFromStream(reader, input, &json, &errors), errors);
    return json;
}

void testRequestIds() {
    std::set<std::string> ids;
    for (int index = 0; index < 128; ++index) {
        const std::string id = yolo::api::nextApiRequestId();
        expect(id.rfind("api-", 0) == 0, "request ID prefix changed");
        expect(id.find_first_not_of("api-0123456789") == std::string::npos,
               "request ID contains unsafe characters");
        expect(ids.insert(id).second, "request ID was reused");
    }
}

void testStartedAndRejectedLogs() {
    yolo::api::ApiLogRecord record;
    record.request_id = "api-123-7";
    record.route = "/infer_video";
    record.media_type = "video";
    record.stream_id = "upload-\"quoted\\line\nnext";

    Json::Value started = parseRecord(
        yolo::api::formatApiLog(yolo::api::ApiLogEvent::Started, record)
    );
    expect(started["schema_version"].asInt() == 1, "schema version mismatch");
    expect(started["component"].asString() == "api_inference", "component mismatch");
    expect(started["event"].asString() == "request_started", "start event mismatch");
    expect(started["level"].asString() == "info", "start level mismatch");
    expect(started["stream_id"].asString() == record.stream_id,
           "stream ID escaping changed content");

    record.reason_code = "video_queue_full";
    record.status_code = 503;
    record.content_size = std::numeric_limits<uint64_t>::max();
    record.has_content_size = true;
    record.elapsed_ms = 12.5;
    Json::Value rejected = parseRecord(
        yolo::api::formatApiLog(yolo::api::ApiLogEvent::Rejected, record)
    );
    expect(rejected["event"].asString() == "request_rejected", "reject event mismatch");
    expect(rejected["level"].asString() == "warning", "reject level mismatch");
    expect(rejected["reason_code"].asString() == "video_queue_full",
           "reason code mismatch");
    expect(rejected["status_code"].asInt() == 503, "status code mismatch");
    expect(rejected["content_size"].asUInt64()
               == std::numeric_limits<uint64_t>::max(),
           "64-bit content size changed");
    expect(rejected["elapsed_ms"].asDouble() == 12.5, "elapsed time mismatch");
}

void testCompletedLogsAndAllowlist() {
    yolo::api::ApiLogRecord image;
    image.request_id = "api-image";
    image.route = "/infer";
    image.media_type = "image";
    image.status_code = 200;
    image.detection_count = 9;
    image.average_fps = 14.5;
    image.cpu_utilization_percent = 31.25;
    image.rss_memory_mb = 512.0;
    Json::Value image_json = parseRecord(
        yolo::api::formatApiLog(yolo::api::ApiLogEvent::Completed, image)
    );
    expect(image_json["detection_count"].asUInt64() == 9,
           "image detection count mismatch");
    expect(!image_json.isMember("frame_count"), "image log contains video counts");

    yolo::api::ApiLogRecord video = image;
    video.request_id = "api-video";
    video.route = "/infer_video_high_low";
    video.media_type = "video";
    video.stream_id = "upload-42";
    video.frame_count = 300;
    video.processed_frame_count = 298;
    video.detected_frame_count = 40;
    video.track_observation_count = 1234;
    video.average_fps = std::numeric_limits<double>::quiet_NaN();
    video.cpu_utilization_percent = -1.0;
    Json::Value video_json = parseRecord(
        yolo::api::formatApiLog(yolo::api::ApiLogEvent::Completed, video)
    );
    expect(video_json["frame_count"].asUInt64() == 300,
           "video frame count mismatch");
    expect(video_json["processed_frame_count"].asUInt64() == 298,
           "video processed count mismatch");
    expect(video_json["detected_frame_count"].asUInt64() == 40,
           "video detected frame count mismatch");
    expect(video_json["track_observation_count"].asUInt64() == 1234,
           "video track count mismatch");
    expect(!video_json.isMember("detection_count"), "video log contains image counts");
    expect(!video_json.isMember("average_fps"), "NaN metric was serialized");
    expect(!video_json.isMember("cpu_utilization_percent"),
           "negative metric was serialized");

    for (const char* forbidden : {
             "file_name", "file", "source", "path", "message", "token", "error"
         }) {
        expect(!video_json.isMember(forbidden),
               std::string("sensitive field entered allowlist: ") + forbidden);
    }

    yolo::api::ApiLogRecord failed = video;
    failed.reason_code = "video_job_failed";
    failed.status_code = 500;
    Json::Value failed_json = parseRecord(
        yolo::api::formatApiLog(yolo::api::ApiLogEvent::Failed, failed)
    );
    expect(failed_json["level"].asString() == "error", "failure level mismatch");
    expect(failed_json["event"].asString() == "request_failed", "failure event mismatch");
}

}  // namespace

int main() {
    testRequestIds();
    testStartedAndRejectedLogs();
    testCompletedLogsAndAllowlist();
    std::cout << "api_logging_test passed\n";
    return 0;
}
