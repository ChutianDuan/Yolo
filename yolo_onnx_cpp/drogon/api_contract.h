#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace yolo {

struct VideoFrameJsonOptions {
    bool include_frames = true;
    size_t frame_offset = 0;
    size_t frame_limit = 0;
};

namespace api {

struct RouteContract {
    std::string_view path;
    std::string_view upload_field;
};

inline constexpr RouteContract kImageInferenceRoute{"/infer", "image"};
inline constexpr RouteContract kVideoInferenceRoute{"/infer_video", "video"};
inline constexpr RouteContract kHighLowVideoInferenceRoute{
    "/infer_video_high_low",
    "video"
};

inline constexpr std::array<RouteContract, 3> kInferenceRoutes{
    kImageInferenceRoute,
    kVideoInferenceRoute,
    kHighLowVideoInferenceRoute
};

}  // namespace api
}  // namespace yolo
