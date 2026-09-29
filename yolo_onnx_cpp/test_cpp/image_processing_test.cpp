#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    expectNear(
        result.tracks[0].detection.x2 - result.tracks[0].detection.x1,
        detection.x2 - detection.x1,
        kTolerance,
        "Optical flow changed bbox width"
    );
    expectNear(
        result.tracks[0].detection.y2 - result.tracks[0].detection.y1,
        detection.y2 - detection.y1,
        kTolerance,
        "Optical flow changed bbox height"
    );
}

void testOpticalFlowRejectsOnlyTwoFeaturePoints() {
    constexpr int kWidth = 160;
    constexpr int kHeight = 120;
    cv::Mat previous = cv::Mat::zeros(kHeight, kWidth, CV_8UC1);
    cv::circle(previous, cv::Point(45, 55), 1, cv::Scalar(255), cv::FILLED);
    cv::circle(previous, cv::Point(95, 55), 1, cv::Scalar(255), cv::FILLED);
    cv::Mat current;
    const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1.0, 0.0, 3.0, 0.0, 1.0, 2.0);
    cv::warpAffine(previous, current, transform, previous.size());

    yolo::Detection detection;
    detection.class_id = 2;
    detection.score = 0.9F;
    detection.x1 = 20.0F;
    detection.y1 = 30.0F;
    detection.x2 = 120.0F;
    detection.y2 = 90.0F;
    const auto result = yolo::weakTrackWithOpticalFlow(
        previous,
        current,
        {yolo::TrackedDetection{8, detection}},
        kWidth,
        kHeight
    );

    expect(result.track_qualities.size() == 1, "Missing per-track flow quality");
    expect(result.track_qualities[0].sampled_point_count < 4,
           "Two-point fixture unexpectedly generated four corners");
    expect(result.tracks.empty(), "Flow accepted fewer than four feature points");
    expect(result.quality.low_point_track_count == 1,
           "Low-point rejection was not diagnosed");
}

void testOpticalFlowRejectsInconsistentPointMotion() {
    constexpr int kWidth = 260;
    constexpr int kHeight = 150;
    cv::Mat previous = cv::Mat::zeros(kHeight, kWidth, CV_8UC1);
    cv::Mat current = cv::Mat::zeros(kHeight, kWidth, CV_8UC1);
    cv::Mat left_patch(40, 40, CV_8UC1);
    cv::Mat right_patch(40, 40, CV_8UC1);
    cv::RNG rng(12345);
    rng.fill(left_patch, cv::RNG::UNIFORM, 0, 256);
    rng.fill(right_patch, cv::RNG::UNIFORM, 0, 256);
    left_patch.copyTo(previous(cv::Rect(40, 50, 40, 40)));
    left_patch.copyTo(current(cv::Rect(70, 50, 40, 40)));
    right_patch.copyTo(previous(cv::Rect(170, 50, 40, 40)));
    right_patch.copyTo(current(cv::Rect(140, 50, 40, 40)));

    yolo::Detection detection;
    detection.class_id = 2;
    detection.score = 0.9F;
    detection.x1 = 25.0F;
    detection.y1 = 35.0F;
    detection.x2 = 220.0F;
    detection.y2 = 110.0F;
    const auto result = yolo::weakTrackWithOpticalFlow(
        previous,
        current,
        {yolo::TrackedDetection{9, detection}},
        kWidth,
        kHeight
    );

    expect(result.tracks.empty(), "Inconsistent point motion moved the bbox");
    expect(
        result.quality.motion_dispersion_rejection_count > 0
            || result.quality.forward_backward_rejection_count > 0,
        "Inconsistent flow rejection was not diagnosed"
    );
}

void testGlobalMotionRequiresBackgroundInliers() {
    constexpr int kWidth = 180;
    constexpr int kHeight = 120;
    cv::Mat previous = cv::Mat::zeros(kHeight, kWidth, CV_8UC1);
    for (int y = 40; y <= 75; y += 12) {
        for (int x = 50; x <= 110; x += 12) {
            cv::rectangle(previous, cv::Rect(x, y, 4, 4), cv::Scalar(255), cv::FILLED);
        }
    }
    cv::Mat current;
    const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1.0, 0.0, 4.0, 0.0, 1.0, 1.0);
    cv::warpAffine(previous, current, transform, previous.size());

    yolo::Detection detection;
    detection.x1 = 35.0F;
    detection.y1 = 25.0F;
    detection.x2 = 135.0F;
    detection.y2 = 95.0F;
    const yolo::FrameMotion motion = yolo::frameMotionForCurrentFrame(
        previous,
        current,
        1,
        yolo::WeakTrackQuality{},
        {yolo::TrackedDetection{3, detection}}
    );
    expect(!motion.valid, "Foreground-only points enabled global motion fallback");
}

void expectSameDetections(
    const std::vector<yolo::Detection>& actual,
    const std::vector<yolo::Detection>& expected
) {
    expect(actual.size() == expected.size(), "Scalar compatibility detection count mismatch");
    for (size_t i = 0; i < actual.size(); ++i) {
        expect(actual[i].class_id == expected[i].class_id, "Scalar compatibility class mismatch");
        expectNear(actual[i].score, expected[i].score, kTolerance, "Scalar compatibility score");
        expectNear(actual[i].x1, expected[i].x1, kTolerance, "Scalar compatibility x1");
        expectNear(actual[i].y1, expected[i].y1, kTolerance, "Scalar compatibility y1");
        expectNear(actual[i].x2, expected[i].x2, kTolerance, "Scalar compatibility x2");
        expectNear(actual[i].y2, expected[i].y2, kTolerance, "Scalar compatibility y2");
    }
}

void testRawClassThresholds() {
    yolo::TensorInput input;
    input.image_width = 512;
    input.image_height = 512;
    input.letterbox.scale_x = 0.5F;
    input.letterbox.scale_y = 0.5F;
    input.letterbox.pad_w = 10.0F;
    input.letterbox.pad_h = 20.0F;
    constexpr int kCandidates = 20;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::array<float, 7> scores{0.2F, 0.6F, 0.3F, 0.6F, 0.1F, nan, inf};
    const std::array<int, 7> classes{0, 1, 2, 1, 0, 0, 0};
    for (bool channel_first : {false, true}) {
        for (bool has_objectness : {false, true}) {
            const int features = has_objectness ? 8 : 7;
            const int class_start = has_objectness ? 5 : 4;
            const float objectness = has_objectness ? 0.8F : 1.0F;
            std::vector<float> output(kCandidates * features, 0.0F);
            const auto set = [&](int candidate, int feature, float value) {
                output[channel_first ? feature * kCandidates + candidate
                                     : candidate * features + feature] = value;
            };
            for (int i = 0; i < static_cast<int>(scores.size()); ++i) {
                set(i, 0, 100.0F + 30.0F * i);
                set(i, 1, 100.0F);
                set(i, 2, 20.0F);
                set(i, 3, 20.0F);
                if (has_objectness) {
                    set(i, 4, objectness);
                }
                set(i, class_start + classes[i], scores[i] / objectness);
            }
            // Reject the winning class without relabeling to a lower-threshold runner-up.
            set(3, class_start, 0.5F / objectness);
            const std::vector<int64_t> shape = channel_first
                ? std::vector<int64_t>{1, features, kCandidates}
                : std::vector<int64_t>{1, kCandidates, features};
            const auto legacy = yolo::decode(output.data(), shape, input, 3, 0.25F);
            expect(legacy.size() == 3, "Raw scalar baseline should keep three candidates");
            expectSameDetections(yolo::decode(output.data(), shape, input, 3, 0.25F, {}), legacy);
            expectSameDetections(
                yolo::decode(output.data(), shape, input, 3, 0.25F, {-1.0F, -1.0F, -1.0F}), legacy
            );

            const auto filtered = yolo::decode(
                output.data(), shape, input, 3, 0.25F, {0.1F, 0.7F, -1.0F}
            );
            expect(filtered.size() == 3, "Raw per-class threshold detection count mismatch");
            expect(filtered[0].class_id == 0 && filtered[1].class_id == 2
                       && filtered[2].class_id == 0, "Raw per-class threshold classes mismatch");
            expectNear(filtered[0].score, 0.2F, kTolerance, "Lower class threshold was ignored");
            expectNear(filtered[1].score, 0.3F, kTolerance, "Class scalar fallback was ignored");
            expectNear(filtered[2].score, 0.1F, kTolerance, "Threshold equality should be accepted");
            expectNear(filtered[0].x1, 160.0F, kTolerance, "Class filtering changed x mapping");
            expectNear(filtered[0].y1, 140.0F, kTolerance, "Class filtering changed y mapping");
        }
    }
}

void testNmsClassThresholds() {
    yolo::TensorInput input;
    input.image_width = 512;
    input.image_height = 512;
    std::array<float, 300 * 6> output{};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::array<float, 11> scores{0.2F, 0.6F, 0.3F, 0.1F, 0.9F, 0.9F,
                                     0.9F, 0.9F, 0.9F, nan, inf};
    const std::array<float, 11> classes{0, 1, 2, 0, -1, 3, nan, inf, 2147483648.0F, 0, 0};
    for (size_t i = 0; i < scores.size(); ++i) {
        output[i * 6] = 20.0F + 20.0F * i;
        output[i * 6 + 1] = 40.0F;
        output[i * 6 + 2] = 30.0F + 20.0F * i;
        output[i * 6 + 3] = 60.0F;
        output[i * 6 + 4] = scores[i];
        output[i * 6 + 5] = classes[i];
    }
    const std::vector<int64_t> shape{1, 300, 6};
    const auto legacy = yolo::decode(output.data(), shape, input, 3, 0.25F);
    expect(legacy.size() == 2, "NMS scalar baseline should keep two candidates");
    expectSameDetections(yolo::decode(output.data(), shape, input, 3, 0.25F, {}), legacy);
    expectSameDetections(
        yolo::decode(output.data(), shape, input, 3, 0.25F, {-1.0F, -1.0F, -1.0F}), legacy
    );
    const auto filtered = yolo::decode(
        output.data(), shape, input, 3, 0.25F, {0.1F, 0.7F, -1.0F}
    );
    expect(filtered.size() == 3, "NMS per-class threshold detection count mismatch");
    expect(filtered[0].class_id == 0 && filtered[1].class_id == 2 && filtered[2].class_id == 0,
           "NMS per-class threshold classes mismatch");
    expectNear(filtered[0].score, 0.2F, kTolerance, "NMS lower class threshold was ignored");
    expectNear(filtered[1].score, 0.3F, kTolerance, "NMS scalar fallback was ignored");
    expectNear(filtered[2].score, 0.1F, kTolerance, "NMS threshold equality should be accepted");
    expectNear(filtered[0].x1, 20.0F, kTolerance, "NMS filtering changed coordinates");
}

}  // namespace

int main() {
    testDirectResizeRestoresOriginalCoordinates();
    testLetterboxRestoresOriginalCoordinates();
    testPreprocessWritesRgbChwTensor();
    testRawClassThresholds();
    testNmsClassThresholds();
    testOpticalFlowUsesOriginalCoordinates();
    testOpticalFlowRejectsOnlyTwoFeaturePoints();
    testOpticalFlowRejectsInconsistentPointMotion();
    testGlobalMotionRequiresBackgroundInliers();
    std::cout << "image_processing_test passed\n";
    return 0;
}
