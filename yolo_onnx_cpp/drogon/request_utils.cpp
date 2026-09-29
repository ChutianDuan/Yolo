#include "request_utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace yolo::api {
namespace {

std::string lowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::filesystem::path makeTempVideoPath(const std::string& extension) {
    static std::atomic<uint64_t> next_id{0};

    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    const uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);

    std::error_code error;
    const auto directory = std::filesystem::temp_directory_path(error);
    if (error) {
        throw std::runtime_error("Temporary video directory is unavailable");
    }
    return directory
        / ("yolo_video_" + std::to_string(micros) + "_" + std::to_string(id) + extension);
}

}  // namespace

bool parseBoolParameter(
    std::string_view key,
    std::string_view value,
    bool& output,
    std::string& error_message
) {
    const std::string parsed = lowerAscii(value);
    if (parsed == "true" || parsed == "1" || parsed == "yes") {
        output = true;
        return true;
    }
    if (parsed == "false" || parsed == "0" || parsed == "no") {
        output = false;
        return true;
    }

    error_message = "Invalid " + std::string(key) + ": " + std::string(value);
    return false;
}

bool parseSizeParameter(
    std::string_view key,
    std::string_view value,
    bool allow_zero,
    size_t& output,
    std::string& error_message
) {
    if (value.empty() || value.front() == '-' || value.front() == '+') {
        error_message = "Invalid " + std::string(key) + ": " + std::string(value);
        return false;
    }

    try {
        size_t parsed = 0;
        const std::string text(value);
        const unsigned long long result = std::stoull(text, &parsed);
        if (parsed != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        if (result > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
            throw std::out_of_range("too large");
        }
        if (!allow_zero && result == 0ULL) {
            error_message = std::string(key) + " must be positive";
            return false;
        }

        output = static_cast<size_t>(result);
        return true;
    } catch (const std::exception&) {
        error_message = "Invalid " + std::string(key) + ": " + std::string(value);
        return false;
    }
}

bool parseLastEventId(
    std::string_view value,
    std::optional<uint64_t>& output,
    std::string& error_message
) {
    output.reset();
    if (value.empty()) {
        return true;
    }
    if (!std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch >= '0' && ch <= '9';
        })) {
        error_message = "Last-Event-ID must be an unsigned decimal integer";
        return false;
    }

    try {
        size_t parsed = 0;
        const std::string text(value);
        const unsigned long long result = std::stoull(text, &parsed);
        if (parsed != text.size()
            || result > static_cast<unsigned long long>(
                std::numeric_limits<uint64_t>::max())) {
            throw std::out_of_range("invalid event sequence");
        }
        output = static_cast<uint64_t>(result);
        return true;
    } catch (const std::exception&) {
        error_message = "Last-Event-ID must be an unsigned decimal integer";
        return false;
    }
}

bool parseVideoFrameJsonOptions(
    std::string_view include_frames,
    std::string_view frame_offset,
    std::string_view frame_limit,
    VideoFrameJsonOptions& options,
    std::string& error_message
) {
    if (!include_frames.empty()
        && !parseBoolParameter(
            "include_frames",
            include_frames,
            options.include_frames,
            error_message)) {
        return false;
    }
    if (!frame_offset.empty()
        && !parseSizeParameter(
            "frame_offset",
            frame_offset,
            true,
            options.frame_offset,
            error_message)) {
        return false;
    }
    if (!frame_limit.empty()
        && !parseSizeParameter(
            "frame_limit",
            frame_limit,
            false,
            options.frame_limit,
            error_message)) {
        return false;
    }
    return true;
}

double elapsedMs(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
    ) / 1000.0;
}

std::string videoExtension(std::string_view file_name) {
    const std::string extension = std::filesystem::path(file_name).extension().string();
    if (extension.empty() || extension.size() > 16) {
        return ".mp4";
    }
    return extension;
}

TempVideoFile::TempVideoFile(std::string_view content, const std::string& extension)
    : path_(makeTempVideoPath(extension)) {
    bool created = false;
    try {
        std::ofstream output(path_, std::ios::binary);
        if (!output.is_open()) {
            throw std::runtime_error("Failed to create temp video file");
        }
        created = true;
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        // close() must succeed before the inference job can use the staged file.
        output.close();
        if (!output.good()) {
            throw std::runtime_error("Failed to write temp video file");
        }
    } catch (...) {
        // A throwing constructor does not invoke TempVideoFile's destructor.
        if (created) {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }
        throw;
    }
}

TempVideoFile::~TempVideoFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
}

const std::filesystem::path& TempVideoFile::path() const {
    return path_;
}

}  // namespace yolo::api
