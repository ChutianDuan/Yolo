#pragma once

#include <chrono>

namespace yolo {

inline bool isFrameExpired(
    bool live_source,
    int max_result_age_ms,
    std::chrono::steady_clock::time_point captured_at,
    std::chrono::steady_clock::time_point now
) {
    return live_source && max_result_age_ms > 0
        && now - captured_at > std::chrono::milliseconds(max_result_age_ms);
}

}  // namespace yolo
