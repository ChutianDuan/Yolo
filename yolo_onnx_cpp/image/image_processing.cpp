#include "image_processing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace yolo {
namespace {

constexpr int kInputC = 3;
constexpr int kBatch = 1;
constexpr int64_t kNmsOutputCandidateCount = 300;
constexpr int64_t kNmsOutputFeatureCount = 6;
constexpr int64_t kNmsX1Index = 0;
constexpr int64_t kNmsY1Index = 1;
constexpr int64_t kNmsX2Index = 2;
constexpr int64_t kNmsY2Index = 3;
constexpr int64_t kNmsScoreIndex = 4;
constexpr int64_t kNmsClassIndex = 5;

cv::Mat decodeImage(std::string_view content) {
    const std::vector<unsigned char> buffer(content.begin(), content.end());
    return cv::imdecode(buffer, cv::IMREAD_COLOR);
}

std::string lowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

bool isQuickTimeContainer(const std::filesystem::path& path) {
    const std::string extension = lowerExtension(path);
    return extension == ".mov" || extension == ".qt";
}

bool tryOpenVideoCapture(
    cv::VideoCapture& capture,
    const std::filesystem::path& video_path,
    int backend
) {
    capture.release();
    try {
        return capture.open(video_path.string(), backend) && capture.isOpened();
    } catch (const cv::Exception&) {
        capture.release();
        return false;
    }
}

float clipped(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

void clipBox(Detection& detection, int image_width, int image_height) {
    detection.x1 = std::max(0.0F, detection.x1);
    detection.y1 = std::max(0.0F, detection.y1);
    detection.x2 = std::max(0.0F, detection.x2);
    detection.y2 = std::max(0.0F, detection.y2);

    if (image_width > 0) {
        const float max_x = static_cast<float>(image_width);
        detection.x1 = clipped(detection.x1, 0.0F, max_x);
        detection.x2 = clipped(detection.x2, 0.0F, max_x);
    }

    if (image_height > 0) {
        const float max_y = static_cast<float>(image_height);
        detection.y1 = clipped(detection.y1, 0.0F, max_y);
        detection.y2 = clipped(detection.y2, 0.0F, max_y);
    }
}

TensorInput preprocessImage(
    const cv::Mat& image,
    const AppConfig& config,
    int original_width,
    int original_height,
    const LetterBoxInfo& letterbox_info
) {
    cv::Mat rgb;
    cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);

    cv::Mat float_img;
    rgb.convertTo(float_img, CV_32FC3, 1.0 / 255.0);

    TensorInput input;
    input.shape = {kBatch, kInputC, config.input_height, config.input_width};
    input.image_width = original_width;
    input.image_height = original_height;
    input.letterbox = letterbox_info;
    input.values.resize(kBatch * kInputC * config.input_height * config.input_width);

    int index = 0;
    for (int c = 0; c < kInputC; ++c) {
        for (int h = 0; h < config.input_height; ++h) {
            for (int w = 0; w < config.input_width; ++w) {
                input.values[index++] = float_img.at<cv::Vec3f>(h, w)[c];
            }
        }
    }

    return input;
}

Detection makeDetection(
    float x1,
    float y1,
    float x2,
    float y2,
    float score,
    int class_id,
    const TensorInput& input
) {
    const float ratio = input.letterbox.ratio > 0.0F ? input.letterbox.ratio : 1.0F;

    Detection detection;
    detection.class_id = class_id;
    detection.score = score;
    detection.x1 = (x1 - input.letterbox.pad_w) / ratio;
    detection.y1 = (y1 - input.letterbox.pad_h) / ratio;
    detection.x2 = (x2 - input.letterbox.pad_w) / ratio;
    detection.y2 = (y2 - input.letterbox.pad_h) / ratio;

    clipBox(detection, input.image_width, input.image_height);
    return detection;
}

}  // namespace

cv::Mat letterbox(
    const cv::Mat& image,
    LetterBoxInfo& info,
    const cv::Size& target_size,
    const cv::Scalar& color
) {
    info = LetterBoxInfo{};

    if (image.empty() || target_size.width <= 0 || target_size.height <= 0) {
        return {};
    }

    const float ratio = std::min(
        static_cast<float>(target_size.width) / static_cast<float>(image.cols),
        static_cast<float>(target_size.height) / static_cast<float>(image.rows)
    );

    const int resized_w = std::min(
        target_size.width,
        std::max(1, static_cast<int>(std::round(static_cast<float>(image.cols) * ratio)))
    );
    const int resized_h = std::min(
        target_size.height,
        std::max(1, static_cast<int>(std::round(static_cast<float>(image.rows) * ratio)))
    );

    const int pad_w_total = target_size.width - resized_w;
    const int pad_h_total = target_size.height - resized_h;
    const int left = pad_w_total / 2;
    const int right = pad_w_total - left;
    const int top = pad_h_total / 2;
    const int bottom = pad_h_total - top;

    cv::Mat resized;
    if (image.cols == resized_w && image.rows == resized_h) {
        resized = image;
    } else {
        cv::resize(image, resized, cv::Size(resized_w, resized_h));
    }

    cv::Mat padded;
    cv::copyMakeBorder(
        resized,
        padded,
        top,
        bottom,
        left,
        right,
        cv::BORDER_CONSTANT,
        color
    );

    info.ratio = ratio;
    info.pad_w = static_cast<float>(left);
    info.pad_h = static_cast<float>(top);

    return padded;
}

std::vector<Detection> decode(
    const float* output_data,
    const std::vector<int64_t>& output_shape,
    const TensorInput& input,
    int class_count,
    float score_threshold
) {
    if (output_data == nullptr || output_shape.size() != 3 || output_shape[0] != 1) {
        return {};
    }
    if (output_shape[1] <= 0 || output_shape[2] <= 0) {
        return {};
    }

    if (output_shape[1] == kNmsOutputCandidateCount
        && output_shape[2] == kNmsOutputFeatureCount) {
        const int64_t candidate_count = output_shape[1];
        std::vector<Detection> detections;
        detections.reserve(static_cast<size_t>(candidate_count));

        for (int64_t i = 0; i < candidate_count; ++i) {
            const float* row = output_data + i * kNmsOutputFeatureCount;
            const float score = row[kNmsScoreIndex];
            if (score < score_threshold) {
                continue;
            }

            const int class_id = static_cast<int>(std::round(row[kNmsClassIndex]));
            if (class_id < 0 || (class_count > 0 && class_id >= class_count)) {
                continue;
            }

            const Detection detection = makeDetection(
                row[kNmsX1Index],
                row[kNmsY1Index],
                row[kNmsX2Index],
                row[kNmsY2Index],
                score,
                class_id,
                input
            );
            if (detection.x2 <= detection.x1 || detection.y2 <= detection.y1) {
                continue;
            }

            detections.push_back(detection);
        }

        return detections;
    }

    if (class_count <= 0) {
        return {};
    }

    const bool channel_first = output_shape[1] < output_shape[2];
    const int64_t feature_count = channel_first ? output_shape[1] : output_shape[2];
    const int64_t candidate_count = channel_first ? output_shape[2] : output_shape[1];

    const bool has_objectness = feature_count == static_cast<int64_t>(class_count) + 5;
    const int64_t class_start = has_objectness ? 5 : 4;
    if (feature_count < class_start + static_cast<int64_t>(class_count)) {
        return {};
    }

    auto value_at = [output_data, channel_first, feature_count, candidate_count](
        int64_t candidate,
        int64_t feature
    ) -> float {
        if (channel_first) {
            return output_data[feature * candidate_count + candidate];
        }
        return output_data[candidate * feature_count + feature];
    };

    std::vector<Detection> detections;

    for (int64_t i = 0; i < candidate_count; ++i) {
        const float objectness = has_objectness ? value_at(i, 4) : 1.0F;
        int best_class_id = 0;
        float best_class_score = value_at(i, class_start);

        for (int cls = 1; cls < class_count; ++cls) {
            const float class_score = value_at(i, class_start + cls);
            if (class_score > best_class_score) {
                best_class_score = class_score;
                best_class_id = cls;
            }
        }

        const float score = objectness * best_class_score;
        if (score < score_threshold) {
            continue;
        }

        const float cx = value_at(i, 0);
        const float cy = value_at(i, 1);
        const float w = value_at(i, 2);
        const float h = value_at(i, 3);

        const Detection detection = makeDetection(
            cx - w * 0.5F,
            cy - h * 0.5F,
            cx + w * 0.5F,
            cy + h * 0.5F,
            score,
            best_class_id,
            input
        );
        if (detection.x2 <= detection.x1 || detection.y2 <= detection.y1) {
            continue;
        }

        detections.push_back(detection);
    }

    return detections;
}

bool openVideoCapture(
    cv::VideoCapture& capture,
    const std::filesystem::path& video_path
) {
    if (isQuickTimeContainer(video_path)) {
        if (tryOpenVideoCapture(capture, video_path, cv::CAP_FFMPEG)) {
            return true;
        }
        if (tryOpenVideoCapture(capture, video_path, cv::CAP_GSTREAMER)) {
            return true;
        }
    }

    return tryOpenVideoCapture(capture, video_path, cv::CAP_ANY);
}

std::string videoOpenFailureMessage(const std::filesystem::path& video_path) {
    if (isQuickTimeContainer(video_path)) {
        return "Failed to open .mov video. Rebuild OpenCV with FFmpeg or GStreamer "
            "videoio support, or convert the file to mp4/avi.";
    }
    return "Failed to open video";
}

std::optional<TensorInput> preprocessImageContent(
    std::string_view content,
    const AppConfig& config
) {
    cv::Mat image;
    try {
        image = decodeImage(content);
    } catch (const cv::Exception&) {
        return std::nullopt;
    }

    if (image.empty()) {
        return std::nullopt;
    }

    return preprocessImageMat(image, config);
}

std::optional<TensorInput> preprocessImageMat(
    const cv::Mat& image,
    const AppConfig& config
) {
    if (image.empty()) {
        return std::nullopt;
    }

    LetterBoxInfo letterbox_info;
    cv::Mat model_image;
    const cv::Size target_size(config.input_width, config.input_height);

    if (config.use_letterbox) {
        model_image = letterbox(image, letterbox_info, target_size);
        if (model_image.empty()) {
            return std::nullopt;
        }
    } else {
        if (image.cols != config.input_width || image.rows != config.input_height) {
            return std::nullopt;
        }
        model_image = image;
    }

    return preprocessImage(
        model_image,
        config,
        image.cols,
        image.rows,
        letterbox_info
    );
}

}  // namespace yolo
