#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <opencv2/core.hpp>

#include "config/app_config.h"
#include "model/inference_types.h"
#include "tracking/authority_tracker.h"

namespace yolo {

struct HighResRoiSelection {
    cv::Rect crop;
    HighResRegion authority_region;
};

inline std::optional<HighResRoiSelection> selectHighResRoi(
    const AppConfig& config,
    const cv::Size& frame_size,
    bool urgent,
    int64_t completed_high_res_count
) {
    if (!config.high_res_roi_enabled || urgent
        || completed_high_res_count <= 0
        || config.high_res_roi_full_frame_interval <= 0
        || completed_high_res_count % config.high_res_roi_full_frame_interval == 0
        || frame_size.width <= 0 || frame_size.height <= 0) {
        return std::nullopt;
    }

    const int left = std::clamp(
        static_cast<int>(std::floor(config.high_res_roi_x * frame_size.width)),
        0,
        frame_size.width - 1
    );
    const int top = std::clamp(
        static_cast<int>(std::floor(config.high_res_roi_y * frame_size.height)),
        0,
        frame_size.height - 1
    );
    const int right = std::clamp(
        static_cast<int>(std::ceil(
            (config.high_res_roi_x + config.high_res_roi_width) * frame_size.width
        )),
        left + 1,
        frame_size.width
    );
    const int bottom = std::clamp(
        static_cast<int>(std::ceil(
            (config.high_res_roi_y + config.high_res_roi_height) * frame_size.height
        )),
        top + 1,
        frame_size.height
    );
    const cv::Rect crop(left, top, right - left, bottom - top);
    if (crop.x == 0 && crop.y == 0
        && crop.width == frame_size.width && crop.height == frame_size.height) {
        return std::nullopt;
    }

    return HighResRoiSelection{
        crop,
        HighResRegion{
            static_cast<float>(left),
            static_cast<float>(top),
            static_cast<float>(right),
            static_cast<float>(bottom)
        }
    };
}

inline std::vector<Detection> translateHighResRoiDetections(
    const std::vector<Detection>& detections,
    const cv::Rect& crop,
    const cv::Size& frame_size
) {
    std::vector<Detection> translated;
    translated.reserve(detections.size());
    for (Detection detection : detections) {
        detection.x1 = static_cast<float>(crop.x) + std::clamp(
            detection.x1, 0.0F, static_cast<float>(crop.width)
        );
        detection.x2 = static_cast<float>(crop.x) + std::clamp(
            detection.x2, 0.0F, static_cast<float>(crop.width)
        );
        detection.y1 = static_cast<float>(crop.y) + std::clamp(
            detection.y1, 0.0F, static_cast<float>(crop.height)
        );
        detection.y2 = static_cast<float>(crop.y) + std::clamp(
            detection.y2, 0.0F, static_cast<float>(crop.height)
        );
        detection.x1 = std::clamp(
            detection.x1, 0.0F, static_cast<float>(frame_size.width)
        );
        detection.x2 = std::clamp(
            detection.x2, 0.0F, static_cast<float>(frame_size.width)
        );
        detection.y1 = std::clamp(
            detection.y1, 0.0F, static_cast<float>(frame_size.height)
        );
        detection.y2 = std::clamp(
            detection.y2, 0.0F, static_cast<float>(frame_size.height)
        );
        if (detection.x2 > detection.x1 && detection.y2 > detection.y1) {
            translated.push_back(detection);
        }
    }
    return translated;
}

}  // namespace yolo
