#include <algorithm>
#include <cmath>
#include <chrono>
#include <future>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include "stream/high_res_roi.h"
#include "stream/stream_processor.h"
#include "stream/stream_inference_replay.h"
#include "stream/single_model_inference_replay.h"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void expectNear(double actual, double expected, const std::string& message) {
    expect(std::abs(actual - expected) < 1e-4, message);
}

void expectTracks(
    const std::vector<yolo::TrackedDetection>& actual,
    const std::vector<yolo::TrackedDetection>& expected
) {
    expect(actual.size() == expected.size(), "track count changed");
    for (size_t i = 0; i < actual.size(); ++i) {
        expect(actual[i].track_id == expected[i].track_id, "track identity changed");
        const auto& a = actual[i].detection;
        const auto& b = expected[i].detection;
        expect(a.class_id == b.class_id, "track class changed");
        expectNear(a.score, b.score, "track score changed");
        expectNear(a.x1, b.x1, "track x1 changed");
        expectNear(a.y1, b.y1, "track y1 changed");
        expectNear(a.x2, b.x2, "track x2 changed");
        expectNear(a.y2, b.y2, "track y2 changed");
    }
}

cv::Mat makeFrame(int offset = 0) {
    cv::Mat frame(160, 240, CV_8UC3, cv::Scalar(25, 35, 45));
    for (int y = 40; y < 120; y += 10) {
        for (int x = 50; x < 130; x += 10) {
            if (((x - 50) / 10 + (y - 40) / 10) % 2 == 0) {
                cv::rectangle(frame, cv::Rect(x + offset, y, 6, 6),
                              cv::Scalar(220, 210, 200), cv::FILLED);
            }
        }
    }
    return frame;
}

yolo::Detection detection(int offset = 0, int class_id = 2, float score = 0.91F) {
    yolo::Detection result;
    result.class_id = class_id;
    result.score = score;
    result.x1 = 50.0F + offset;
    result.y1 = 40.0F;
    result.x2 = 130.0F + offset;
    result.y2 = 120.0F;
    return result;
}

void testSharedPyramidFlowEquivalence() {
    const auto compare = [](const std::vector<cv::Point2f>& actual,
                            const std::vector<cv::Point2f>& reference,
                            const std::vector<unsigned char>& status,
                            const std::vector<unsigned char>& reference_status,
                            const std::vector<float>& error,
                            const std::vector<float>& reference_error) {
        expect(actual.size() == reference.size() && status == reference_status,
               "shared pyramids changed LK size/status");
        for (size_t i = 0; i < actual.size(); ++i) {
            expectNear(actual[i].x, reference[i].x, "shared pyramids changed LK x");
            expectNear(actual[i].y, reference[i].y, "shared pyramids changed LK y");
            if (status[i]) {
                expectNear(error[i], reference_error[i], "shared pyramids changed valid LK error");
            }
        }
    };
    for (const cv::Size size : {cv::Size(53, 35), cv::Size(240, 160), cv::Size(1280, 720)}) {
        for (int scenario = 0; scenario < 4; ++scenario) {
            // Non-contiguous ROI with enough parent padding exercises input-border reuse.
            cv::Mat previous_parent(size.height + 96, size.width + 96, CV_8UC1);
            cv::RNG rng(5600 + size.width + scenario);
            rng.fill(previous_parent, cv::RNG::UNIFORM, 0, 256);
            if (scenario == 3) {
                previous_parent.setTo(128);
            }
            const cv::Rect roi(48, 48, size.width, size.height);
            const cv::Mat previous = previous_parent(roi);
            cv::Mat current_parent(previous_parent.size(), CV_8UC1, cv::Scalar(17));
            cv::Mat current = current_parent(roi);
            const cv::Mat transform = (cv::Mat_<double>(2, 3) <<
                1, 0, scenario == 0 ? 2.0 : 2.25, 0, 1, -1.5);
            cv::warpAffine(previous, current, transform, size, cv::INTER_LINEAR,
                           cv::BORDER_REFLECT_101);
            if (scenario == 1) {
                cv::rectangle(current, cv::Rect(size.width / 3, size.height / 3,
                              size.width / 3, size.height / 3), cv::Scalar(0), cv::FILLED);
            } else if (scenario == 2) {
                cv::add(current, cv::Scalar(12), current);
            }
            std::vector<cv::Point2f> points{{0, 0}, {static_cast<float>(size.width - 1),
                                                   static_cast<float>(size.height - 1)}};
            for (int y = 4; y < size.height; y += std::max(4, size.height / 10)) {
                for (int x = 4; x < size.width; x += std::max(4, size.width / 12)) {
                    points.emplace_back(static_cast<float>(x), static_cast<float>(y));
                }
            }
            std::vector<cv::Mat> previous_pyramid, current_pyramid;
            cv::buildOpticalFlowPyramid(previous, previous_pyramid, cv::Size(21, 21), 3, false);
            cv::buildOpticalFlowPyramid(current, current_pyramid, cv::Size(21, 21), 3, false);
            std::vector<cv::Point2f> reference, actual, reference_back, actual_back;
            std::vector<unsigned char> reference_status, status, reference_back_status, back_status;
            std::vector<float> reference_error, error, reference_back_error, back_error;
            cv::calcOpticalFlowPyrLK(previous, current, points, reference, reference_status,
                                    reference_error, cv::Size(21, 21), 3);
            cv::calcOpticalFlowPyrLK(previous_pyramid, current_pyramid, points, actual, status,
                                    error, cv::Size(21, 21), 3);
            compare(actual, reference, status, reference_status, error, reference_error);
            cv::calcOpticalFlowPyrLK(current, previous, reference, reference_back,
                                    reference_back_status, reference_back_error, cv::Size(21, 21), 3);
            cv::calcOpticalFlowPyrLK(current_pyramid, previous_pyramid, actual, actual_back,
                                    back_status, back_error, cv::Size(21, 21), 3);
            compare(actual_back, reference_back, back_status, reference_back_status,
                    back_error, reference_back_error);
            cv::buildOpticalFlowPyramid(previous, previous_pyramid, cv::Size(21, 21), 3, true);
            cv::buildOpticalFlowPyramid(current, current_pyramid, cv::Size(21, 21), 3, true);
            cv::calcOpticalFlowPyrLK(previous_pyramid, current_pyramid, points, actual, status,
                                    error, cv::Size(21, 21), 3);
            compare(actual, reference, status, reference_status, error, reference_error);
            cv::calcOpticalFlowPyrLK(current_pyramid, previous_pyramid, actual, actual_back,
                                    back_status, back_error, cv::Size(21, 21), 3);
            compare(actual_back, reference_back, back_status, reference_back_status,
                    back_error, reference_back_error);
        }
    }
}

void testSequenceMatchesExistingFunctions(bool high_low, bool estimate_motion) {
    yolo::StreamProcessor processor(high_low);
    yolo::ByteTracker byte_tracker;
    yolo::AuthorityTracker authority_tracker;
    cv::Mat previous_gray;
    std::vector<yolo::TrackedDetection> previous_tracks;
    for (int index = 0; index < 12; ++index) {
        const auto frame = makeFrame(index);
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        yolo::WeakTrackResult weak;
        yolo::FrameMotion motion;
        motion.frame_index = index;
        std::vector<yolo::ProjectedTrack> projected;
        if (!previous_gray.empty() && !previous_tracks.empty()) {
            weak = yolo::weakTrackWithOpticalFlow(
                previous_gray, gray, previous_tracks, frame.cols, frame.rows
            );
            if (estimate_motion) {
                motion = yolo::frameMotionForCurrentFrame(
                    previous_gray, gray, index, weak.quality, previous_tracks
                );
            }
            if (high_low) {
                projected = yolo::projectTracks(
                    previous_tracks, yolo::directTrackMotions(previous_tracks, weak),
                    motion, !yolo::isSevereWeakQualityDrop(weak.quality), frame.cols, frame.rows
                );
            }
        }

        auto prepared = processor.prepareFrame(frame, index, estimate_motion);
        expectTracks(processor.tracks(), previous_tracks);
        expect(prepared.frame_index == index && prepared.motion.frame_index == index,
               "frame context was lost");
        expect(prepared.gray.type() == CV_8UC1 && prepared.gray.size() == frame.size(),
               "prepared gray image shape/type changed");
        expect(cv::norm(prepared.gray, gray, cv::NORM_INF) == 0.0, "gray conversion changed");
        expect(prepared.motion.valid == motion.valid, "frame motion validity changed");
        expectNear(prepared.motion.dx, motion.dx, "frame motion dx changed");
        expectNear(prepared.motion.dy, motion.dy, "frame motion dy changed");
        expectTracks(prepared.weak.tracks, weak.tracks);
        expect(prepared.weak.roi_count == weak.roi_count
                   && prepared.weak.roi_pixels == weak.roi_pixels
                   && prepared.weak.quality.sampled_point_count
                       == weak.quality.sampled_point_count,
               "prepared weak-flow load differs from direct flow");
        expectTracks(yolo::projectedDetections(prepared.projected_tracks),
                     yolo::projectedDetections(projected));
        expect(std::isfinite(prepared.optical_flow_ms) && prepared.optical_flow_ms >= 0,
               "flow timing is invalid");
        const bool has_tracks = !previous_gray.empty() && !previous_tracks.empty();
        for (size_t detail = 0; detail < prepared.preparation_ms.size(); ++detail) {
            const bool expected = detail == 0 || (has_tracks
                && (detail == 1 || (detail == 2 && estimate_motion)
                    || (detail == 3 && high_low)));
            expect(prepared.preparation_ms[detail].has_value() == expected,
                   "conditional preparation timing coverage changed");
            if (expected) {
                expect(std::isfinite(*prepared.preparation_ms[detail])
                           && *prepared.preparation_ms[detail] >= 0.0,
                       "preparation stage timing is invalid");
            }
        }
        for (size_t detail = 0; detail < prepared.weak.stage_ms.size(); ++detail) {
            const bool expected = has_tracks
                && (detail < 2 || prepared.weak.quality.sampled_point_count > 0);
            expect(prepared.weak.stage_ms[detail].has_value() == expected,
                   "weak-flow substage timing coverage changed");
            if (expected) {
                expect(std::isfinite(*prepared.weak.stage_ms[detail])
                           && *prepared.weak.stage_ms[detail] >= 0.0,
                       "weak-flow substage timing is invalid");
            }
        }

        std::vector<yolo::TrackedDetection> expected;
        if (index % 3 == 0) {
            const bool high = !high_low || index % 6 == 0;
            const std::vector<yolo::Detection> detections{
                detection(index, index >= 6 ? 4 : 2, high ? 0.91F : 0.99F)
            };
            expected = high_low
                ? (high ? authority_tracker.updateHighRes(detections, index)
                        : authority_tracker.updateLowRes(detections, projected, index))
                : byte_tracker.update(detections);
            expectTracks(processor.applyDetections(
                detections, index, high, prepared.projected_tracks
            ), expected);
        } else {
            expected = high_low ? authority_tracker.updateFlow(projected, index)
                                : byte_tracker.updateTracked(weak.tracks);
            expectTracks(processor.applyFlow(prepared), expected);
        }
        processor.finishFrame(std::move(prepared));
        previous_gray = gray;
        previous_tracks = std::move(expected);
    }
}

void testWeakTimingSkippedNoPoints() {
    const cv::Mat flat(160, 240, CV_8UC1, cv::Scalar(128));
    const std::vector<yolo::TrackedDetection> tracks{{1, detection()}};
    const auto result = yolo::weakTrackWithOpticalFlow(flat, flat, tracks, flat.cols, flat.rows);
    expect(result.quality.sampled_point_count == 0, "flat frame unexpectedly yielded corners");
    expect(result.roi_count == 1 && result.roi_pixels == 6400,
           "ROI work excluded a valid flat-frame box");
    for (size_t i = 0; i < result.stage_ms.size(); ++i) {
        expect(result.stage_ms[i].has_value() == (i < 2),
               "no-corner flow reported an unexecuted LK or quality stage");
    }
}

void testOverlappingWeakReference() {
    cv::Mat previous, current;
    cv::cvtColor(makeFrame(0), previous, cv::COLOR_BGR2GRAY);
    cv::cvtColor(makeFrame(2), current, cv::COLOR_BGR2GRAY);
    const std::vector<yolo::TrackedDetection> tracks{{1, detection()}, {2, detection()}};
    const auto result = yolo::weakTrackWithOpticalFlow(
        previous, current, tracks, previous.cols, previous.rows);
    expect(result.quality.sampled_point_count == 40
               && result.quality.valid_point_count == 40
               && result.quality.tracked_track_count == 2
               && result.roi_count == 2 && result.roi_pixels == 12800
               && result.tracks.size() == 2,
           "overlapping ROI changed point counts or accepted tracks");
    for (size_t i = 0; i < result.tracks.size(); ++i) {
        const auto& item = result.tracks[i];
        expect(item.track_id == static_cast<int>(i + 1), "overlap track ID changed");
        expectNear(item.detection.x1, 52.0000534, "overlap x1 changed");
        expectNear(item.detection.y1, 39.9999161, "overlap y1 changed");
        expectNear(item.detection.x2, 132.000061, "overlap x2 changed");
        expectNear(item.detection.y2, 119.999916, "overlap y2 changed");
    }
}

void testDiscardDoesNotAdvanceState() {
    yolo::StreamProcessor actual(true);
    yolo::StreamProcessor baseline(true);
    const auto frame = makeFrame();
    for (auto* processor : {&actual, &baseline}) {
        for (int index = 0; index < 3; ++index) {
            auto prepared = processor->prepareFrame(frame, index, true);
            processor->applyDetections({detection()}, index, true);
            processor->finishFrame(std::move(prepared));
        }
    }
    const cv::Mat unrelated(frame.size(), frame.type(), cv::Scalar(255, 255, 255));
    const auto discarded = actual.prepareFrame(unrelated, 3, true);
    (void)discarded;
    expectTracks(actual.tracks(), baseline.tracks());
    bool rejected = false;
    try {
        actual.prepareFrame(cv::Mat{}, 3, true);
    } catch (const cv::Exception&) {
        rejected = true;
    }
    expect(rejected, "empty image preparation did not fail");
    auto a = actual.prepareFrame(frame, 4, true);
    auto b = baseline.prepareFrame(frame, 4, true);
    expectNear(a.weak.quality.mean_frame_diff, 0.0, "discarded image became the flow anchor");
    expectTracks(a.weak.tracks, b.weak.tracks);
    expectTracks(actual.applyFlow(a), baseline.applyFlow(b));
}

void testTemporalPyramidCache() {
    for (const cv::Size size : {cv::Size(53, 35), cv::Size(240, 160), cv::Size(1280, 720)}) {
        cv::Mat textured(size, CV_8UC1);
        cv::RNG rng(6200 + size.width);
        rng.fill(textured, cv::RNG::UNIFORM, 0, 256);
        yolo::Detection box;
        box.x1 = 4; box.y1 = 4;
        box.x2 = static_cast<float>(size.width - 4);
        box.y2 = static_cast<float>(size.height - 4);
        box.score = 0.9F; box.class_id = 2;
        const std::vector<yolo::TrackedDetection> tracks{{1, box}, {2, box}};
        cv::Mat previous = textured;
        std::vector<cv::Mat> previous_pyramid;
        for (int index = 0; index < 6; ++index) {
            // Flat frames force an empty cache and the next textured frame must rebuild it.
            cv::Mat current = index == 2 || index == 3
                ? cv::Mat(size, CV_8UC1, cv::Scalar(128)) : textured.clone();
            if (index == 1) {
                const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1, 0, 2.25, 0, 1, -1.5);
                cv::warpAffine(textured, current, transform, size, cv::INTER_LINEAR,
                               cv::BORDER_REFLECT_101);
            }
            const auto reference = yolo::weakTrackWithOpticalFlow(
                previous, current, tracks, size.width, size.height);
            std::vector<cv::Mat> current_pyramid;
            const auto actual = yolo::weakTrackWithOpticalFlow(
                previous, current, tracks, size.width, size.height,
                previous_pyramid, current_pyramid);
            expectTracks(actual.tracks, reference.tracks);
            expect(actual.quality.sampled_point_count == reference.quality.sampled_point_count
                       && actual.quality.valid_point_count == reference.quality.valid_point_count
                       && actual.track_qualities.size() == reference.track_qualities.size(),
                   "temporal cache changed point counts");
            expectNear(actual.quality.median_forward_backward_error,
                       reference.quality.median_forward_backward_error, "temporal FB error changed");
            expect(yolo::isSevereWeakQualityDrop(actual.quality)
                       == yolo::isSevereWeakQualityDrop(reference.quality)
                       && yolo::isComplexWeakQuality(actual.quality)
                       == yolo::isComplexWeakQuality(reference.quality)
                       && yolo::isStableWeakQuality(actual.quality)
                       == yolo::isStableWeakQuality(reference.quality),
                   "temporal cache changed refresh predicates");
            for (size_t i = 0; i < actual.track_qualities.size(); ++i) {
                const auto& a = actual.track_qualities[i];
                const auto& b = reference.track_qualities[i];
                expect(a.accepted == b.accepted && a.valid_point_count == b.valid_point_count,
                       "temporal cache changed per-track validity");
                expectNear(a.median_forward_backward_error, b.median_forward_backward_error,
                           "temporal per-track FB error changed");
                expectNear(a.displacement_spread_ratio, b.displacement_spread_ratio,
                           "temporal per-track spread changed");
            }
            expect(current_pyramid.empty() == (actual.quality.sampled_point_count == 0),
                   "no-point frame retained a stale pyramid");
            expect(current_pyramid.size() <= 8, "cache exceeded four gray/derivative levels");
            previous = current;
            previous_pyramid = std::move(current_pyramid);
        }
    }
}

void testSingleModelEmptyFlowRule(bool advance_empty) {
    yolo::StreamProcessor processor(false);
    yolo::ByteTracker reference;
    expectTracks(processor.applyDetections({detection()}, 0, true),
                 reference.update({detection()}));
    yolo::PreparedStreamFrame empty;
    for (int index = 1; index <= 35; ++index) {
        empty.frame_index = index;
        const auto expected = reference.updateTracked({});
        expectTracks(processor.applyFlow(empty, advance_empty), expected);
    }
    for (int index = 36; index < 39; ++index) {
        expectTracks(processor.applyDetections({detection()}, index, true),
                     reference.update({detection()}));
    }
}

void testSingleModelSameFrameCorrection() {
    yolo::StreamProcessor processor(false);
    const auto initial = processor.applyDetections({detection()}, 0, true);
    expect(initial.size() == 1, "initial single-model track missing");
    const int id = initial.front().track_id;
    yolo::PreparedStreamFrame frame;
    for (int index = 1; index <= 8; ++index) {
        frame.frame_index = index;
        frame.weak.tracks = {{id, detection(index * 3)}};
        const auto flowed = processor.applyFlow(frame);
        expect(flowed.size() == 1 && flowed.front().track_id == id,
               "moving flow track lost its identity");
        // Zero-innovation detection must not move a moving track into another time step.
        const auto corrected = processor.applyDetections({flowed.front().detection}, index, true);
        expectTracks(corrected, flowed);
    }
    auto snapshot = processor.singleModelState();
    yolo::StreamProcessor restored(false);
    restored.restoreSingleModel(std::move(snapshot.tracker), std::move(snapshot.tracks));
    const auto same_frame_detection = processor.tracks().front().detection;
    expectTracks(restored.applyDetections({same_frame_detection}, 8, true),
                 processor.applyDetections({same_frame_detection}, 8, true));
    frame.frame_index = 9;
    frame.weak.tracks = {{id, detection(27)}};
    expectTracks(restored.applyFlow(frame), processor.applyFlow(frame));
    bool rejected = false;
    try {
        restored.applyDetections({detection()}, 8, true);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "older observation silently moved the tracker clock backwards");
}

void testSingleModelSameFrameLostAge() {
    yolo::StreamProcessor processor(false);
    const auto initial = processor.applyDetections({detection()}, 0, true);
    const int id = initial.front().track_id;
    yolo::PreparedStreamFrame frame;
    for (int index = 1; index <= 20; ++index) {
        frame.frame_index = index;
        processor.applyFlow(frame, false);
        processor.applyDetections({}, index, true);
    }
    const auto recovered = processor.applyDetections({detection()}, 20, true);
    expect(recovered.size() == 1 && recovered.front().track_id == id,
           "same-frame corrections prematurely exhausted the lost-track buffer");
    // Empty offline flow still advances aging, even without any model calls.
    for (int index = 21; index <= 51; ++index) {
        frame.frame_index = index;
        processor.applyFlow(frame, false);
    }
    const auto expired = processor.applyDetections({detection()}, 51, true);
    expect(expired.size() == 1 && expired.front().track_id != id,
           "empty offline flow froze lost-track aging");
}

void testReplayAndStreamIsolation() {
    yolo::StreamProcessor first(true);
    yolo::StreamProcessor second(true);
    const auto frame = makeFrame();
    auto prepared = first.prepareFrame(frame, 0, true);
    first.applyDetections({detection()}, 0, true);
    first.finishFrame(std::move(prepared));
    second.applyDetections({detection(20, 7)}, 0, true);
    const auto second_before = second.tracks();
    yolo::AuthorityTracker replay = first.authority();
    replay.updateHighRes({detection(0, 4)}, 1);
    first.restoreAuthority(std::move(replay));
    expect(first.tracks().size() == 1 && first.tracks().front().detection.class_id == 4,
           "replay authority and visible tracks diverged");
    expectTracks(second.tracks(), second_before);
    auto next = first.prepareFrame(frame, 2, true);
    expectNear(next.weak.quality.mean_frame_diff, 0.0, "replay replaced the committed gray frame");

    yolo::PreparedStreamFrame gap;
    gap.frame_index = 30;
    gap.projected_tracks.push_back({first.tracks().front(), true});
    expect(first.applyFlow(gap).empty(), "skipped source indices did not advance authority age");

    yolo::StreamProcessor single(false);
    bool rejected = false;
    try {
        single.applyDetections({detection()}, 0, false);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected && single.tracks().empty(), "single-model processor accepted low-model input");
    rejected = false;
    try {
        single.restoreAuthority(second.authority());
    } catch (const std::logic_error&) {
        rejected = true;
    }
    expect(rejected && single.tracks().empty(), "single-model processor accepted authority replay");
}

void testEmptyTrackRefreshPredicates() {
    yolo::StreamProcessor processor(true);
    const cv::Mat dark(32, 48, CV_8UC3, cv::Scalar::all(0));
    const cv::Mat bright(32, 48, CV_8UC3, cv::Scalar::all(255));
    auto previous = processor.prepareFrame(dark, 0, true);
    processor.finishFrame(std::move(previous));
    const auto actual = processor.prepareFrame(bright, 1, true);
    cv::Mat old_gray;
    cv::cvtColor(dark, old_gray, cv::COLOR_BGR2GRAY);
    const auto legacy = yolo::weakTrackWithOpticalFlow(old_gray, actual.gray, {}, 48, 32);
    expectNear(legacy.quality.mean_frame_diff, 1.0, "empty-track fixture did not exercise frame difference");
    expect(yolo::isSevereWeakQualityDrop(actual.weak.quality)
               == yolo::isSevereWeakQualityDrop(legacy.quality)
           && yolo::isComplexWeakQuality(actual.weak.quality)
               == yolo::isComplexWeakQuality(legacy.quality)
           && yolo::isStableWeakQuality(actual.weak.quality)
               == yolo::isStableWeakQuality(legacy.quality),
           "skipping empty-track frame difference changed refresh predicates");
}

void testHighResRoiSelectionAndTranslation() {
    yolo::AppConfig config;
    config.high_res_roi_enabled = true;
    config.high_res_roi_x = 0.10F;
    config.high_res_roi_y = 0.20F;
    config.high_res_roi_width = 0.50F;
    config.high_res_roi_height = 0.50F;
    config.high_res_roi_full_frame_interval = 4;
    const cv::Size frame_size(101, 51);

    expect(!yolo::selectHighResRoi(config, frame_size, false, 0).has_value(),
           "first high-res refresh must stay full-frame");
    expect(!yolo::selectHighResRoi(config, frame_size, true, 1).has_value(),
           "urgent high-res refresh must stay full-frame");
    expect(!yolo::selectHighResRoi(config, frame_size, false, 4).has_value(),
           "periodic full-frame fallback was not honored");

    const auto selection = yolo::selectHighResRoi(
        config, frame_size, false, 1
    );
    expect(selection.has_value(), "scheduled ROI refresh was not selected");
    expect(selection->crop == cv::Rect(10, 10, 51, 26),
           "normalized ROI did not cover the expected integer pixels");

    yolo::Detection local = detection();
    local.x1 = -20.0F;
    local.y1 = 2.0F;
    local.x2 = 80.0F;
    local.y2 = 60.0F;
    const auto translated = yolo::translateHighResRoiDetections(
        {local}, selection->crop, frame_size
    );
    expect(translated.size() == 1, "valid translated ROI detection was dropped");
    expectNear(translated.front().x1, 10.0, "ROI x1 was not crop-clipped/offset");
    expectNear(translated.front().y1, 12.0, "ROI y1 was not offset");
    expectNear(translated.front().x2, 61.0, "ROI x2 was not crop-clipped");
    expectNear(translated.front().y2, 36.0, "ROI y2 was not crop-clipped");

    config.high_res_roi_x = 0.0F;
    config.high_res_roi_y = 0.0F;
    config.high_res_roi_width = 1.0F;
    config.high_res_roi_height = 1.0F;
    expect(!yolo::selectHighResRoi(config, frame_size, false, 1).has_value(),
           "full-frame ROI should use the normal full-frame path");
}


yolo::AuthorityReplayFrame replayFrame(int index) {
    yolo::AuthorityReplayFrame frame;
    frame.frame_index = index;
    frame.image_width = 240;
    frame.image_height = 160;
    frame.motion.frame_index = index;
    frame.motion.valid = true;
    frame.motion.inlier_ratio = 1.0;
    frame.motion.dx = 2.0;
    frame.allow_global_motion = true;
    return frame;
}

yolo::ScheduledInferenceResult scheduledResult(
    const yolo::InferenceContext& context,
    const std::vector<yolo::Detection>& detections = {detection()}
) {
    yolo::ScheduledInferenceResult result;
    result.status = yolo::ScheduledInferenceStatus::Completed;
    result.result.context = context;
    result.result.detections = detections;
    return result;
}

void testNonblockingSlowInferenceReplay() {
    const auto captured = std::chrono::steady_clock::now();
    yolo::StreamInferenceReplay replay("camera", 300);
    const yolo::InferenceContext source{"camera", 0, 123.0};
    std::promise<yolo::ScheduledInferenceResult> slow;
    replay.submit(slow.get_future(), source, captured, true);
    const auto started = std::chrono::steady_clock::now();
    for (int frame_index = 0; frame_index < 9; ++frame_index) {
        replay.append(replayFrame(frame_index));
        auto polled = replay.poll(captured + std::chrono::milliseconds(frame_index * 10));
        expect(polled.applied.empty() && !polled.tracker && polled.failed.empty(),
               "unfinished inference was applied or failed");
        expect(replay.busy(true) && !replay.busy(false), "slow slot lost its bound");
    }
    expect(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(200),
           "slow inference blocked per-frame progress");
    auto measured = scheduledResult(source);
    measured.result.queue_wait_ms = 12.0;
    measured.result.infer_ms = 40.0;
    measured.result.decode_ms = 2.0;
    measured.result.postprocess_ms = 3.0;
    measured.execution_ms = 47.0;
    measured.completed_at = captured + std::chrono::milliseconds(70);
    slow.set_value(std::move(measured));
    auto polled = replay.poll(captured + std::chrono::milliseconds(100));
    expect(polled.applied.size() == 1 && polled.tracker.has_value()
               && polled.completed_count == 1 && polled.skipped_count == 0,
           "ready slow inference was not replayed");
    expect(!replay.busy(true), "ready inference did not release its slot");
    const auto& diagnostics = polled.diagnostics[0];
    expect(diagnostics.outcomes.completed_count == 1 && diagnostics.outcomes.applied_count == 1
               && diagnostics.outcomes.expired_count == 0 && diagnostics.outcomes.evicted_count == 0,
           "accepted correction diagnostics mismatch");
    const auto stage = [&](yolo::StreamInferenceStage key) -> const auto& {
        return diagnostics.stages[static_cast<size_t>(key)];
    };
    expectNear(stage(yolo::StreamInferenceStage::QueueWait).sum_ms, 12.0,
               "queue timing changed");
    expectNear(stage(yolo::StreamInferenceStage::Infer).sum_ms, 40.0,
               "model timing changed");
    expectNear(stage(yolo::StreamInferenceStage::Execution).sum_ms, 47.0,
               "full engine-call timing changed");
    expectNear(stage(yolo::StreamInferenceStage::Postprocess).sum_ms, 5.0,
               "decode/postprocess timing was not combined");
    expect(stage(yolo::StreamInferenceStage::CompletionPickup).sum_ms >= 30.0
               && stage(yolo::StreamInferenceStage::Replay).count == 1
               && stage(yolo::StreamInferenceStage::Preprocess).count == 0,
           "pickup/replay timing or missing-stage handling changed");
    expect(polled.applied.front().context.frame_index == 0
               && polled.applied.front().context.timestamp_ms == 123.0,
           "correction pretended to belong to the latest frame");
    expect(polled.tracker->tracks().size() == 1, "replay lost the detected target");
    // Existing authority policy blends global displacement with weight 0.65.
    expectNear(polled.tracker->tracks().front().detection.x1, 50.0 + 8 * 2 * 0.65,
               "old detection was pasted onto the latest frame instead of replayed");

    yolo::StreamInferenceReplay other("other", 300);
    other.append(replayFrame(0));
    expect(other.poll(captured).applied.empty(), "another stream inherited a correction");
}

void testReplayExpiryAndHistoryBound() {
    const auto captured = std::chrono::steady_clock::now();
    for (const bool evicted : {false, true}) {
        yolo::StreamInferenceReplay replay("camera", 300);
        const yolo::InferenceContext source{"camera", 0, 0.0};
        std::promise<yolo::ScheduledInferenceResult> promise;
        replay.submit(promise.get_future(), source, captured, true);
        for (int frame_index = 0; frame_index < (evicted ? 61 : 1); ++frame_index) {
            replay.append(replayFrame(frame_index));
        }
        expect(replay.frameCount() == (evicted ? 60U : 1U), "replay history exceeded 60 frames");
        promise.set_value(scheduledResult(source));
        auto polled = replay.poll(captured + std::chrono::milliseconds(evicted ? 1 : 301));
        expect(polled.applied.empty() && !polled.tracker && polled.failed.empty()
                   && polled.skipped_count == 1 && polled.completed_count == 1,
               "expired or evicted result was applied or counted as a model failure");
        expect(!replay.busy(true), "discarded result retained its inference slot");
        const auto& timing = polled.diagnostics[0];
        expect(timing.outcomes.completed_count == 1 && timing.outcomes.applied_count == 0
                   && timing.outcomes.expired_count == (evicted ? 0U : 1U)
                   && timing.outcomes.evicted_count == (evicted ? 1U : 0U)
                   && timing.stages[static_cast<size_t>(yolo::StreamInferenceStage::Replay)].count == 0,
               "discard reasons or pre-replay timing were lost");
    }
}

void testReplayContextAndIndependentTiers() {
    const auto captured = std::chrono::steady_clock::now();
    yolo::StreamInferenceReplay replay("camera", 300);
    const yolo::InferenceContext high_context{"camera", 0, 0.0};
    const yolo::InferenceContext low_context{"camera", 1, 33.0};
    std::promise<yolo::ScheduledInferenceResult> high;
    std::promise<yolo::ScheduledInferenceResult> low;
    replay.submit(high.get_future(), high_context, captured, true);
    replay.submit(low.get_future(), low_context, captured, false);
    expect(replay.busy(true) && replay.busy(false), "model tiers shared a single blocking slot");
    for (int index = 0; index < 3; ++index) {
        replay.append(replayFrame(index));
    }
    low.set_value(scheduledResult(low_context));
    auto first = replay.poll(captured + std::chrono::milliseconds(70));
    expect(first.applied.size() == 1 && !first.applied.front().high_res
               && replay.busy(true) && !replay.busy(false),
           "ready low result waited for unfinished high inference");
    high.set_value(scheduledResult(high_context));
    auto second = replay.poll(captured + std::chrono::milliseconds(80));
    expect(second.applied.size() == 1 && second.applied.front().high_res
               && second.tracker && !second.tracker->tracks().empty(),
           "out-of-order high result did not replay through the later low frame");

    std::promise<yolo::ScheduledInferenceResult> wrong;
    replay.submit(wrong.get_future(), low_context, captured, false);
    wrong.set_value(scheduledResult({"different-camera", 1, 33.0}));
    auto invalid = replay.poll(captured + std::chrono::milliseconds(90));
    expect(invalid.diagnostics[1].outcomes.completed_count == 0,
           "foreign context contaminated completed timing");
    expect(invalid.applied.empty() && !invalid.tracker && invalid.failed.size() == 1,
           "foreign stream context was accepted");

    std::promise<yolo::ScheduledInferenceResult> broken;
    replay.submit(broken.get_future(), low_context, captured, false);
    broken.set_exception(std::make_exception_ptr(std::runtime_error("synthetic backend failure")));
    auto failed = replay.poll(captured + std::chrono::milliseconds(100));
    expect(failed.failed.size() == 1 && failed.skipped_count == 0,
           "failed future was silently classified as a stale result");
}

void testReplayRoiAndDisabledDeadline() {
    const auto captured = std::chrono::steady_clock::now();
    yolo::StreamInferenceReplay replay("camera", 0);
    replay.append(replayFrame(0));
    std::promise<yolo::ScheduledInferenceResult> promise;
    const yolo::InferenceContext source{"camera", 0, 0.0};
    yolo::HighResRoiSelection roi{cv::Rect(10, 20, 100, 100),
                                yolo::HighResRegion{10, 20, 110, 120}};
    replay.submit(promise.get_future(), source, captured, true, roi);
    auto local = detection();
    local.x1 = 4; local.y1 = 5; local.x2 = 20; local.y2 = 25;
    promise.set_value(scheduledResult(source, {local}));
    auto result = replay.poll(captured + std::chrono::seconds(10));
    expect(result.applied.size() == 1 && result.applied.front().high_res_roi
               && result.tracker && result.tracker->tracks().size() == 1,
           "ROI correction or disabled deadline was lost");
    expectNear(result.tracker->tracks().front().detection.x1, 14.0,
               "ROI detection did not return to source-image coordinates");
    expectNear(result.tracker->tracks().front().detection.y1, 25.0,
               "ROI correction lost its source crop offset");
}


void testSingleModelNonblockingExactReplay() {
    const auto captured = std::chrono::steady_clock::now();
    yolo::SingleModelInferenceReplay replay("single", 300);
    yolo::StreamProcessor reference(false);
    const yolo::InferenceContext source{"single", 0, 123.0};
    std::promise<yolo::ScheduledInferenceResult> slow;
    replay.submit(slow.get_future(), source, captured, 3.0);
    const auto started = std::chrono::steady_clock::now();
    for (int index = 0; index < 9; ++index) {
        auto prepared = reference.prepareFrame(makeFrame(index), index, false);
        replay.append(index, prepared.gray);
        if (index == 0) {
            reference.applyDetections({detection()}, index, true);
        } else {
            reference.applyFlow(prepared);
        }
        reference.finishFrame(std::move(prepared));
        const auto waiting = replay.poll(captured + std::chrono::milliseconds(index * 10));
        expect(waiting.applied.empty() && !waiting.tracker && waiting.failed.empty()
                   && waiting.completed_count == 0 && replay.busy(),
               "single-model unfinished inference changed state");
    }
    expect(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(200),
           "single-model unfinished future blocked frame processing");
    slow.set_value(scheduledResult(source));
    auto ready = replay.poll(captured + std::chrono::milliseconds(100));
    expect(ready.tracker && ready.applied.size() == 1 && ready.failed.empty()
               && ready.diagnostics.outcomes.applied_count == 1 && !replay.busy(),
           "single-model delayed correction was not committed");
    expectTracks(ready.tracks, reference.tracks());
    expect(ready.applied.front().context.frame_index == 0
               && ready.applied.front().context.timestamp_ms == 123.0
               && ready.applied.front().high_res && !ready.applied.front().high_res_roi,
           "single-model correction lost its original source");
    expect(ready.tracks.size() == 1 && ready.tracks.front().detection.x1 > 55.0F,
           "single-model old box was pasted without optical-flow replay");
    yolo::StreamProcessor actual(false);
    auto committed = actual.prepareFrame(makeFrame(8), 8, false);
    actual.finishFrame(std::move(committed));
    actual.restoreSingleModel(std::move(*ready.tracker), std::move(ready.tracks));
    auto next = actual.prepareFrame(makeFrame(9), 9, false);
    auto expected = reference.prepareFrame(makeFrame(9), 9, false);
    expectTracks(next.weak.tracks, expected.weak.tracks);
    expectTracks(actual.applyFlow(next), reference.applyFlow(expected));
    bool rejected = false;
    yolo::StreamProcessor high_low(true);
    try {
        high_low.restoreSingleModel(yolo::ByteTracker{}, {});
    } catch (const std::logic_error&) {
        rejected = true;
    }
    expect(rejected && high_low.tracks().empty(),
           "single-model replay replaced high-low authority");
}

void testSingleModelReplayRejectionsAndBound() {
    const auto captured = std::chrono::steady_clock::now();
    const yolo::InferenceContext source{"single", 0, 0.0};
    for (const bool evicted : {false, true}) {
        yolo::SingleModelInferenceReplay replay("single", 300);
        std::promise<yolo::ScheduledInferenceResult> promise;
        replay.submit(promise.get_future(), source, captured);
        for (size_t index = 0; index <= (evicted ? replay.kMaxReplayFrames : 0); ++index) {
            cv::Mat gray;
            cv::cvtColor(makeFrame(), gray, cv::COLOR_BGR2GRAY);
            replay.append(static_cast<int64_t>(index), std::move(gray));
        }
        expect(replay.frameCount() == (evicted ? replay.kMaxReplayFrames : 1U),
               "single-model gray history exceeded its fixed bound");
        promise.set_value(scheduledResult(source));
        auto rejected = replay.poll(captured + std::chrono::milliseconds(evicted ? 1 : 301));
        expect(!rejected.tracker && rejected.applied.empty() && rejected.failed.empty()
                   && rejected.skipped_count == 1 && rejected.diagnostics.outcomes.completed_count == 1
                   && rejected.diagnostics.outcomes.expired_count == (evicted ? 0U : 1U)
                   && rejected.diagnostics.outcomes.evicted_count == (evicted ? 1U : 0U) && !replay.busy(),
               "single-model expired/evicted result contaminated state");
    }
    for (const auto status : {yolo::ScheduledInferenceStatus::Stale,
                             yolo::ScheduledInferenceStatus::Replaced,
                             yolo::ScheduledInferenceStatus::Stopped}) {
        yolo::SingleModelInferenceReplay replay("single", 0);
        std::promise<yolo::ScheduledInferenceResult> promise;
        replay.submit(promise.get_future(), source, captured);
        auto result = scheduledResult(source);
        result.status = status;
        promise.set_value(std::move(result));
        auto rejected = replay.poll(captured);
        expect(!rejected.tracker && rejected.applied.empty() && !replay.busy()
                   && rejected.completed_count == 0
                   && (status == yolo::ScheduledInferenceStatus::Stopped
                       ? rejected.failed.size() == 1 : rejected.skipped_count == 1),
               "single-model failed/stale result classification changed");
    }
    yolo::SingleModelInferenceReplay replay("single", 0);
    std::promise<yolo::ScheduledInferenceResult> foreign;
    replay.submit(foreign.get_future(), source, captured);
    foreign.set_value(scheduledResult({"other", 0, 0.0}));
    const auto wrong = replay.poll(captured);
    expect(wrong.failed.size() == 1 && wrong.completed_count == 0 && !wrong.tracker,
           "single-model foreign context entered replay diagnostics");
    std::promise<yolo::ScheduledInferenceResult> broken;
    replay.submit(broken.get_future(), source, captured);
    broken.set_exception(std::make_exception_ptr(std::runtime_error("synthetic failure")));
    expect(replay.poll(captured).failed.size() == 1 && !replay.busy(),
           "single-model failed future retained a slot");
}

void testSingleModelReplayPostDeadlineRollback() {
    const auto captured = std::chrono::steady_clock::now();
    yolo::SingleModelInferenceReplay replay("single", 300);
    for (int index = 0; index < 32; ++index) {
        cv::Mat gray;
        cv::cvtColor(makeFrame(index), gray, cv::COLOR_BGR2GRAY);
        replay.append(index, std::move(gray));
    }
    std::promise<yolo::ScheduledInferenceResult> old;
    replay.submit(old.get_future(), {"single", 0, 0.0}, captured);
    old.set_value(scheduledResult({"single", 0, 0.0}));
    auto expired = replay.poll(captured + std::chrono::microseconds(299500));
    const auto& stages = expired.diagnostics.stages;
    expect(stages[static_cast<size_t>(yolo::StreamInferenceStage::ResultAge)].sum_ms < 300.0
               && stages[static_cast<size_t>(yolo::StreamInferenceStage::CommitAge)].sum_ms > 300.0
               && expired.diagnostics.outcomes.expired_count == 1 && !expired.tracker,
           "single-model replay overrun was not rejected after computation");
    std::promise<yolo::ScheduledInferenceResult> fresh;
    const yolo::InferenceContext source{"single", 31, 1033.0};
    replay.submit(fresh.get_future(), source, captured);
    fresh.set_value(scheduledResult(source, {detection(31, 7)}));
    auto accepted = replay.poll(captured);
    expect(accepted.tracker && accepted.tracks.size() == 1
               && accepted.tracks.front().track_id == 1
               && accepted.tracks.front().detection.class_id == 7,
           "single-model expired correction polluted replay track identity");
}


void testSingleModelReplayCheckpoint() {
    const auto captured = std::chrono::steady_clock::now();
    yolo::StreamProcessor reference(false);
    for (int index = 0; index < 31; ++index) {
        auto prepared = reference.prepareFrame(makeFrame(index), index, false);
        if (index == 0) {
            reference.applyDetections({detection()}, index, true);
        } else {
            reference.applyFlow(prepared);
        }
        reference.finishFrame(std::move(prepared));
    }
    yolo::SingleModelInferenceReplay replay("single", 300);
    replay.resetBase(reference.singleModelState());
    std::promise<yolo::ScheduledInferenceResult> promise;
    const yolo::InferenceContext source{"single", 31, 1033.0};
    for (int index : {31, 32}) {
        auto prepared = reference.prepareFrame(makeFrame(index), index, false);
        replay.append(index, prepared.gray);
        if (index == 31) {
            replay.submit(promise.get_future(), source, captured);
            reference.applyDetections({detection(index)}, index, true);
        } else {
            reference.applyFlow(prepared);
        }
        reference.finishFrame(std::move(prepared));
    }
    bool rejected = false;
    try {
        replay.resetBase(yolo::SingleModelStreamState{});
    } catch (const std::logic_error&) {
        rejected = true;
    }
    expect(rejected && replay.busy() && replay.frameCount() == 2,
           "single-model pending source was discarded by a new checkpoint");
    promise.set_value(scheduledResult(source, {detection(31)}));
    auto polled = replay.poll(captured);
    expect(polled.tracker && polled.applied.size() == 1 && polled.failed.empty(),
           "single-model checkpoint correction did not commit");
    expectTracks(polled.tracks, reference.tracks());
    replay.resetBase(reference.singleModelState());
    expect(replay.frameCount() == 0, "single-model checkpoint retained an unnecessary old prefix");
}

}  // namespace

int main() {
    cv::setNumThreads(1);
    testSharedPyramidFlowEquivalence();
    testWeakTimingSkippedNoPoints();
    testOverlappingWeakReference();
    testTemporalPyramidCache();
    testSequenceMatchesExistingFunctions(false, false);
    testSequenceMatchesExistingFunctions(false, true);
    testSequenceMatchesExistingFunctions(true, true);
    testDiscardDoesNotAdvanceState();
    testSingleModelEmptyFlowRule(false);
    testSingleModelEmptyFlowRule(true);
    testSingleModelSameFrameCorrection();
    testSingleModelSameFrameLostAge();
    testReplayAndStreamIsolation();
    testEmptyTrackRefreshPredicates();
    testHighResRoiSelectionAndTranslation();
    testNonblockingSlowInferenceReplay();
    testReplayExpiryAndHistoryBound();
    testReplayContextAndIndependentTiers();
    testReplayRoiAndDisabledDeadline();
    testSingleModelNonblockingExactReplay();
    testSingleModelReplayRejectionsAndBound();
    testSingleModelReplayPostDeadlineRollback();
    testSingleModelReplayCheckpoint();
    std::cout << "stream_processor_test passed\n";
}
