#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

#include "drogon/api_contract.h"
#include "drogon/request_utils.h"

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

void testRouteContracts() {
    std::set<std::string_view> paths;
    for (const yolo::api::RouteContract& route : yolo::api::kInferenceRoutes) {
        expect(!route.path.empty(), "Gateway route path is empty");
        expect(route.path.front() == '/', "Gateway route must be absolute");
        expect(!route.upload_field.empty(), "Gateway upload field is empty");
        expect(paths.insert(route.path).second, "Gateway route path is duplicated");
    }

    expect(yolo::api::kImageInferenceRoute.path == "/infer", "Image route changed");
    expect(
        yolo::api::kImageInferenceRoute.upload_field == "image",
        "Image upload field changed"
    );
    expect(yolo::api::kVideoInferenceRoute.path == "/infer_video", "Video route changed");
    expect(
        yolo::api::kHighLowVideoInferenceRoute.path == "/infer_video_high_low",
        "High-low video route changed"
    );
}

void testBooleanParameters() {
    bool value = false;
    std::string error;
    expect(
        yolo::api::parseBoolParameter("include_frames", "YES", value, error) && value,
        "YES should parse as true"
    );
    expect(
        yolo::api::parseBoolParameter("include_frames", "0", value, error) && !value,
        "0 should parse as false"
    );
    expect(
        !yolo::api::parseBoolParameter("include_frames", "sometimes", value, error),
        "Invalid boolean should fail"
    );
    expect(
        error == "Invalid include_frames: sometimes",
        "Invalid boolean error changed"
    );
}

void testFrameOptions() {
    yolo::VideoFrameJsonOptions options;
    std::string error;
    expect(
        yolo::api::parseVideoFrameJsonOptions("false", "2", "5", options, error),
        "Valid frame options did not parse"
    );
    expect(!options.include_frames, "include_frames mismatch");
    expect(options.frame_offset == 2, "frame_offset mismatch");
    expect(options.frame_limit == 5, "frame_limit mismatch");

    expect(
        !yolo::api::parseVideoFrameJsonOptions("", "", "0", options, error),
        "Zero frame_limit should fail"
    );
    expect(error == "frame_limit must be positive", "Zero frame_limit error changed");
    expect(
        !yolo::api::parseVideoFrameJsonOptions("", "-1", "", options, error),
        "Negative frame_offset should fail"
    );
    expect(error == "Invalid frame_offset: -1", "Negative frame_offset error changed");
}

void testVideoExtension() {
    expect(yolo::api::videoExtension("sample.mov") == ".mov", "Video extension mismatch");
    expect(yolo::api::videoExtension("sample") == ".mp4", "Missing extension fallback changed");
    expect(
        yolo::api::videoExtension("sample.this_extension_is_too_long") == ".mp4",
        "Long extension fallback changed"
    );
}

void testTempVideoFileCleanup() {
    std::filesystem::path path;
    {
        yolo::api::TempVideoFile file("video-bytes", ".mp4");
        path = file.path();
        expect(std::filesystem::exists(path), "Temporary video file was not created");
    }
    expect(!std::filesystem::exists(path), "Temporary video file was not removed");
}

}  // namespace

int main() {
    testRouteContracts();
    testBooleanParameters();
    testFrameOptions();
    testVideoExtension();
    testTempVideoFileCleanup();
    std::cout << "api_gateway_contract_test passed\n";
    return 0;
}
