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

// 解析原始或已内置 NMS 的模型输出，并映射回原图；引擎随后执行统一 NMS。
std::vector<Detection> decode(
    const float* output_data,
    const std::vector<int64_t>& output_shape,
    const TensorInput& input,
    int class_count,
    float score_threshold
);

std::vector<Detection> decode(
    const float* output_data,
    const std::vector<int64_t>& output_shape,
    const TensorInput& input,
    int class_count,
    float score_threshold,
    const std::vector<float>& class_score_thresholds
);

// 输入为 OpenCV BGR 图像；输出为归一化 RGB NCHW 张量及坐标变换信息。
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
