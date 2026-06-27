#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "config/app_config.h"
#include "image/image_processing.h"
#include "model/inference_types.h"
#include "video/optical_flow_tracker.h"

namespace {

constexpr float kTolerance = 1.0e-3F;

void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void expectNear(float actual, float expected, float tolerance, const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        fail(
            message + ": expected " + std::to_string(expected)
            + ", got " + std::to_string(actual)
        );
    }
}

std::vector<yolo::Detection> decodeSingleNmsDetection(
    const yolo::TensorInput& input,
    float x1,
    float y1,
    float x2,
    float y2
) {
    constexpr size_t kCandidateCount = 300;
    constexpr size_t kFeatureCount = 6;
    std::array<float, kCandidateCount * kFeatureCount> output{};
    output[0] = x1;
    output[1] = y1;
    output[2] = x2;
    output[3] = y2;
    output[4] = 0.9F;
    output[5] = 2.0F;

    return yolo::decode(
        output.data(),
        {1, static_cast<int64_t>(kCandidateCount), static_cast<int64_t>(kFeatureCount)},
        input,
        10,
        0.25F
    );
}

void testDirectResizeRestoresOriginalCoordinates() {
    const cv::Mat source(720, 1280, CV_8UC3, cv::Scalar(10, 20, 30));
    yolo::AppConfig config;
    config.input_width = 640;
    config.input_height = 384;
    config.use_letterbox = false;

    const auto input = yolo::preprocessImageMat(source, config);
    expect(input.has_value(), "Direct resize preprocessing failed");
    expect(input->shape == std::vector<int64_t>({1, 3, 384, 640}), "Unexpected tensor shape");
    expect(input->image_width == 1280, "Original image width was not preserved");
    expect(input->image_height == 720, "Original image height was not preserved");
    expectNear(input->letterbox.scale_x, 0.5F, kTolerance, "Unexpected X scale");
    expectNear(
        input->letterbox.scale_y,
        384.0F / 720.0F,
        kTolerance,
        "Unexpected Y scale"
    );
    expectNear(input->letterbox.pad_w, 0.0F, kTolerance, "Unexpected horizontal padding");
    expectNear(input->letterbox.pad_h, 0.0F, kTolerance, "Unexpected vertical padding");

    const auto detections = decodeSingleNmsDetection(
        input.value(),
        160.0F,
        96.0F,
        480.0F,
        288.0F
    );
    expect(detections.size() == 1, "Direct resize detection was not decoded");
    expectNear(detections[0].x1, 320.0F, kTolerance, "Direct resize x1 mapping failed");
    expectNear(detections[0].y1, 180.0F, kTolerance, "Direct resize y1 mapping failed");
    expectNear(detections[0].x2, 960.0F, kTolerance, "Direct resize x2 mapping failed");
    expectNear(detections[0].y2, 540.0F, kTolerance, "Direct resize y2 mapping failed");
}

void testLetterboxRestoresOriginalCoordinates() {
    const cv::Mat source(720, 1280, CV_8UC3, cv::Scalar(10, 20, 30));
    yolo::AppConfig config;
    config.input_width = 640;
    config.input_height = 384;
    config.use_letterbox = true;

    const auto input = yolo::preprocessImageMat(source, config);
    expect(input.has_value(), "Letterbox preprocessing failed");
    expectNear(input->letterbox.scale_x, 0.5F, kTolerance, "Unexpected letterbox X scale");
    expectNear(input->letterbox.scale_y, 0.5F, kTolerance, "Unexpected letterbox Y scale");
    expectNear(input->letterbox.pad_w, 0.0F, kTolerance, "Unexpected letterbox X padding");
    expectNear(input->letterbox.pad_h, 12.0F, kTolerance, "Unexpected letterbox Y padding");

    const auto detections = decodeSingleNmsDetection(
        input.value(),
        160.0F,
        102.0F,
        480.0F,
        282.0F
    );
    expect(detections.size() == 1, "Letterbox detection was not decoded");
    expectNear(detections[0].x1, 320.0F, kTolerance, "Letterbox x1 mapping failed");
    expectNear(detections[0].y1, 180.0F, kTolerance, "Letterbox y1 mapping failed");
    expectNear(detections[0].x2, 960.0F, kTolerance, "Letterbox x2 mapping failed");
    expectNear(detections[0].y2, 540.0F, kTolerance, "Letterbox y2 mapping failed");
}

void testPreprocessWritesRgbChwTensor() {
    cv::Mat source(2, 2, CV_8UC3);
    source.at<cv::Vec3b>(0, 0) = cv::Vec3b(10, 20, 30);
    source.at<cv::Vec3b>(0, 1) = cv::Vec3b(40, 50, 60);
    source.at<cv::Vec3b>(1, 0) = cv::Vec3b(70, 80, 90);
    source.at<cv::Vec3b>(1, 1) = cv::Vec3b(100, 110, 120);

    yolo::AppConfig config;
    config.input_width = 2;
    config.input_height = 2;
    config.use_letterbox = false;

    const auto input = yolo::preprocessImageMat(source, config);
    expect(input.has_value(), "RGB CHW preprocessing failed");
    expect(input->values.size() == 12, "Unexpected tensor value count");

    const std::array<float, 12> expected = {
        30.0F / 255.0F,
        60.0F / 255.0F,
        90.0F / 255.0F,
        120.0F / 255.0F,
        20.0F / 255.0F,
        50.0F / 255.0F,
        80.0F / 255.0F,
        110.0F / 255.0F,
        10.0F / 255.0F,
        40.0F / 255.0F,
        70.0F / 255.0F,
        100.0F / 255.0F,
    };
    for (size_t i = 0; i < expected.size(); ++i) {
        expectNear(
            input->values[i],
            expected[i],
            kTolerance,
            "Unexpected RGB CHW tensor value at " + std::to_string(i)
        );
    }
}

void testOpticalFlowUsesOriginalCoordinates() {
    constexpr int kWidth = 640;
    constexpr int kHeight = 360;
    constexpr float kDx = 7.0F;
    constexpr float kDy = 4.0F;

    cv::Mat previous = cv::Mat::zeros(kHeight, kWidth, CV_8UC1);
    for (int y = 110; y <= 210; y += 20) {
        for (int x = 190; x <= 290; x += 20) {
            cv::rectangle(previous, cv::Rect(x, y, 8, 8), cv::Scalar(255), cv::FILLED);
        }
    }

    cv::Mat current;
    const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1.0, 0.0, kDx, 0.0, 1.0, kDy);
    cv::warpAffine(previous, current, transform, previous.size());

    yolo::Detection detection;
    detection.class_id = 2;
    detection.score = 0.9F;
    detection.x1 = 170.0F;
    detection.y1 = 90.0F;
    detection.x2 = 320.0F;
    detection.y2 = 240.0F;

    const auto result = yolo::weakTrackWithOpticalFlow(
        previous,
        current,
        {yolo::TrackedDetection{5, detection}},
        kWidth,
        kHeight
    );

    expect(result.tracks.size() == 1, "Optical flow did not preserve the original-space track");
    expectNear(result.tracks[0].detection.x1, detection.x1 + kDx, 0.75F, "Optical flow x1");
    expectNear(result.tracks[0].detection.y1, detection.y1 + kDy, 0.75F, "Optical flow y1");
    expectNear(result.tracks[0].detection.x2, detection.x2 + kDx, 0.75F, "Optical flow x2");
    expectNear(result.tracks[0].detection.y2, detection.y2 + kDy, 0.75F, "Optical flow y2");
}

}  // namespace

int main() {
    testDirectResizeRestoresOriginalCoordinates();
    testLetterboxRestoresOriginalCoordinates();
    testPreprocessWritesRgbChwTensor();
    testOpticalFlowUsesOriginalCoordinates();
    std::cout << "image_processing_test passed\n";
    return 0;
}
