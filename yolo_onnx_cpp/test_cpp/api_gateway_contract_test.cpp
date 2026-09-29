#include <csignal>
#include <cstdlib>
#include <iterator>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <sys/resource.h>

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

void testLastEventId() {
    std::optional<uint64_t> value;
    std::string error;
    expect(yolo::api::parseLastEventId("", value, error) && !value.has_value(),
           "Missing Last-Event-ID should disable replay");
    expect(yolo::api::parseLastEventId("0", value, error) && *value == 0,
           "Zero Last-Event-ID did not parse");
    expect(yolo::api::parseLastEventId(
               "18446744073709551615", value, error)
               && *value == std::numeric_limits<uint64_t>::max(),
           "Maximum Last-Event-ID did not parse");
    for (const std::string invalid : {"-1", "+1", " 1", "1 ", "1x",
                                      "18446744073709551616"}) {
        expect(!yolo::api::parseLastEventId(invalid, value, error),
               "Invalid Last-Event-ID was accepted: " + invalid);
        expect(!value.has_value(), "Invalid Last-Event-ID retained an old value");
    }
}

void testVideoExtension() {
    expect(yolo::api::videoExtension("sample.mov") == ".mov", "Video extension mismatch");
    expect(yolo::api::videoExtension("sample") == ".mp4", "Missing extension fallback changed");
    expect(
        yolo::api::videoExtension("sample.this_extension_is_too_long") == ".mp4",
        "Long extension fallback changed"
    );
}

class TempStagingFixture {
public:
    TempStagingFixture() {
        if (const char* value = std::getenv("TMPDIR")) {
            previous_tmpdir_ = value;
        }
        const auto pattern = std::filesystem::temp_directory_path() / "yolo_staging_test_XXXXXX";
        std::string buffer = pattern.string();
        char* directory = ::mkdtemp(buffer.data());
        expect(directory != nullptr, "Could not create staging fixture directory");
        root_ = directory;
        expect(::setenv("TMPDIR", root_.c_str(), 1) == 0, "Could not set fixture TMPDIR");
    }

    ~TempStagingFixture() {
        if (previous_tmpdir_) {
            ::setenv("TMPDIR", previous_tmpdir_->c_str(), 1);
        } else {
            ::unsetenv("TMPDIR");
        }
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
    std::optional<std::string> previous_tmpdir_;
};

class ZeroFileSizeLimit {
public:
    ZeroFileSizeLimit() {
        expect(::getrlimit(RLIMIT_FSIZE, &previous_limit_) == 0, "Could not read file-size limit");
        struct sigaction ignored {};
        ignored.sa_handler = SIG_IGN;
        ::sigemptyset(&ignored.sa_mask);
        expect(::sigaction(SIGXFSZ, &ignored, &previous_signal_) == 0,
               "Could not suppress fixture file-size signal");
        auto limit = previous_limit_;
        limit.rlim_cur = 0;
        expect(::setrlimit(RLIMIT_FSIZE, &limit) == 0, "Could not set fixture file-size limit");
    }

    ~ZeroFileSizeLimit() {
        ::setrlimit(RLIMIT_FSIZE, &previous_limit_);
        ::sigaction(SIGXFSZ, &previous_signal_, nullptr);
    }

private:
    struct rlimit previous_limit_ {};
    struct sigaction previous_signal_ {};
};

void testTempVideoFileFailures() {
    TempStagingFixture fixture;
    for (const size_t bytes : {size_t{8192}, size_t{64}}) {
        bool rejected = false;
        {
            ZeroFileSizeLimit limit;
            try {
                yolo::api::TempVideoFile file(std::string(bytes, 'v'), ".mp4");
            } catch (const std::runtime_error&) {
                rejected = true;
            }
        }
        expect(rejected, "Temporary video write/close failure was silently accepted");
        expect(std::filesystem::is_empty(fixture.root()),
               "Failed temporary video constructor leaked a partial file");
    }

    const auto unavailable = fixture.root() / "private-not-a-directory";
    { std::ofstream marker(unavailable); }
    expect(::setenv("TMPDIR", unavailable.c_str(), 1) == 0, "Could not set invalid TMPDIR");
    bool rejected = false;
    try {
        yolo::api::TempVideoFile file("video-bytes", ".mp4");
    } catch (const std::exception& error) {
        rejected = true;
        expect(std::string(error.what()).find(unavailable.string()) == std::string::npos,
               "Temporary directory failure exposed its private path");
    }
    expect(rejected, "Invalid temporary directory was accepted");
    expect(::setenv("TMPDIR", fixture.root().c_str(), 1) == 0, "Could not restore fixture TMPDIR");
    std::filesystem::remove(unavailable);
    {
        yolo::api::TempVideoFile recovered("recovered", ".mp4");
        expect(std::filesystem::file_size(recovered.path()) == 9,
               "Staging did not recover after restoring the limit and directory");
    }
    expect(std::filesystem::is_empty(fixture.root()), "Invalid-directory failure left a video file");
}

void testTempVideoFileCleanup() {
    std::filesystem::path path;
    {
        yolo::api::TempVideoFile file("video-bytes", ".mp4");
        path = file.path();
        expect(std::filesystem::exists(path), "Temporary video file was not created");
        std::ifstream input(path, std::ios::binary);
        const std::string stored((std::istreambuf_iterator<char>(input)), {});
        expect(stored == "video-bytes", "Temporary video was not fully persisted before use");
    }
    expect(!std::filesystem::exists(path), "Temporary video file was not removed");
}

}  // namespace

int main() {
    testRouteContracts();
    testBooleanParameters();
    testFrameOptions();
    testLastEventId();
    testVideoExtension();
    testTempVideoFileCleanup();
    testTempVideoFileFailures();
    std::cout << "api_gateway_contract_test passed\n";
    return 0;
}
