#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "drogon/response_json.h"

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

yolo::VideoInferResult makeVideoResult() {
    yolo::VideoInferResult result;
    result.frame_count = 3;
    result.display_frame_count = 3;
    result.output_shapes = {{1, 300, 6}};

    for (int i = 0; i < 3; ++i) {
        yolo::VideoFrameTracks frame;
        frame.frame_index = i;
        frame.timestamp_ms = static_cast<double>(i) * 40.0;
        frame.tracks_source = i == 0 ? "detected" : "weak_tracked";

        yolo::Detection detection;
        detection.class_id = 2;
        detection.score = 0.9F;
        detection.x1 = static_cast<float>(i);
        detection.y1 = static_cast<float>(i + 1);
        detection.x2 = static_cast<float>(i + 2);
        detection.y2 = static_cast<float>(i + 3);
        frame.tracks.push_back(yolo::TrackedDetection{i + 10, detection});

        result.frames.push_back(frame);
    }

    return result;
}

void testDefaultReturnsAllFrames() {
    const yolo::VideoInferResult result = makeVideoResult();
    const Json::Value json = yolo::videoInferResultToJson(result, {"person", "rider", "car"});

    expect(json["frames"].size() == 3, "Default response did not return all frames");
    expect(json["frames_returned"].asUInt64() == 3, "Default frames_returned mismatch");
    expect(json["frame_offset"].asUInt64() == 0, "Default frame_offset mismatch");
    expect(json["frame_limit"].isNull(), "Default frame_limit should be null");
    expect(!json["has_more_frames"].asBool(), "Default response should not have more frames");
    expect(
        json["frames"][0]["tracks"][0]["class_name"].asString() == "car",
        "Default response did not preserve track class name"
    );
}

void testSummaryOmitsFrames() {
    const yolo::VideoInferResult result = makeVideoResult();
    yolo::VideoFrameJsonOptions options;
    options.include_frames = false;

    const Json::Value json = yolo::videoInferResultToJson(result, {}, options);

    expect(json["frames"].isArray(), "Summary frames should be an array");
    expect(json["frames"].empty(), "Summary response should omit frame entries");
    expect(json["frames_returned"].asUInt64() == 0, "Summary frames_returned mismatch");
    expect(json["has_more_frames"].asBool(), "Summary response should report omitted frames");
}

void testFrameWindow() {
    const yolo::VideoInferResult result = makeVideoResult();
    yolo::VideoFrameJsonOptions options;
    options.frame_offset = 1;
    options.frame_limit = 1;

    const Json::Value json = yolo::videoInferResultToJson(result, {}, options);

    expect(json["frames"].size() == 1, "Window response should return one frame");
    expect(json["frames_returned"].asUInt64() == 1, "Window frames_returned mismatch");
    expect(json["frame_offset"].asUInt64() == 1, "Window frame_offset mismatch");
    expect(json["frame_limit"].asUInt64() == 1, "Window frame_limit mismatch");
    expect(json["frames"][0]["frame_index"].asInt64() == 1, "Window frame index mismatch");
    expect(json["has_more_frames"].asBool(), "Window response should report more frames");
}

void testOutOfRangeOffset() {
    const yolo::VideoInferResult result = makeVideoResult();
    yolo::VideoFrameJsonOptions options;
    options.frame_offset = 10;
    options.frame_limit = 2;

    const Json::Value json = yolo::videoInferResultToJson(result, {}, options);

    expect(json["frames"].empty(), "Out-of-range offset should return no frames");
    expect(json["frames_returned"].asUInt64() == 0, "Out-of-range frames_returned mismatch");
    expect(json["frame_offset"].asUInt64() == 10, "Out-of-range frame_offset mismatch");
    expect(!json["has_more_frames"].asBool(), "Out-of-range response should not have more frames");
}

}  // namespace

int main() {
    testDefaultReturnsAllFrames();
    testSummaryOmitsFrames();
    testFrameWindow();
    testOutOfRangeOffset();
    std::cout << "response_json_test passed\n";
    return 0;
}
