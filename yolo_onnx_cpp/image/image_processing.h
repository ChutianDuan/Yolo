#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <opencv2/core.hpp>

namespace cv {
class VideoCapture;
}

#include "config/app_config.h"
#include "model/inference_types.h"

namespace yolo {

cv::Mat letterbox(
    const cv::Mat& image,
    LetterBoxInfo& info,
    const cv::Size& target_size,
    const cv::Scalar& color = cv::Scalar(114, 114, 114)
);

std::vector<Detection> decode(
    const float* output_data,
    const std::vector<int64_t>& output_shape,
    const TensorInput& input,
    int class_count,
    float score_threshold
);

std::optional<TensorInput> preprocessImageMat(
    const cv::Mat& image,
    const AppConfig& config
);

std::optional<TensorInput> preprocessImageContent(
    std::string_view content,
    const AppConfig& config
);

bool openVideoCapture(
    cv::VideoCapture& capture,
    const std::filesystem::path& video_path
);

std::string videoOpenFailureMessage(const std::filesystem::path& video_path);

}  // namespace yolo
