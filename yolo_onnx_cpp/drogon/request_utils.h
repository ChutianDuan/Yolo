#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "drogon/api_contract.h"

namespace yolo::api {

bool parseBoolParameter(
    std::string_view key,
    std::string_view value,
    bool& output,
    std::string& error_message
);

bool parseSizeParameter(
    std::string_view key,
    std::string_view value,
    bool allow_zero,
    size_t& output,
    std::string& error_message
);

bool parseLastEventId(
    std::string_view value,
    std::optional<uint64_t>& output,
    std::string& error_message
);

bool parseVideoFrameJsonOptions(
    std::string_view include_frames,
    std::string_view frame_offset,
    std::string_view frame_limit,
    VideoFrameJsonOptions& options,
    std::string& error_message
);

double elapsedMs(std::chrono::steady_clock::time_point start);

std::string videoExtension(std::string_view file_name);

class TempVideoFile {
public:
    TempVideoFile(std::string_view content, const std::string& extension);
    ~TempVideoFile();

    TempVideoFile(const TempVideoFile&) = delete;
    TempVideoFile& operator=(const TempVideoFile&) = delete;

    const std::filesystem::path& path() const;

private:
    std::filesystem::path path_;
};

}  // namespace yolo::api
