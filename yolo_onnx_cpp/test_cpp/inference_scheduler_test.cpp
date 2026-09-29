#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include <opencv2/core.hpp>

#include "config/app_config.h"
#include "image/image_processing.h"
#include "model/inference_scheduler.h"
#include "model/yolo_engine.h"

namespace {

void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

yolo::InferenceContext context(const std::string& stream_id, int64_t frame_index) {
    yolo::InferenceContext value;
    value.stream_id = stream_id;
    value.frame_index = frame_index;
    return value;
}


void testTrackedUpdates(const std::shared_ptr<yolo::YoloEngine>& engine,
                        const yolo::TensorInput& input) {
    yolo::InferenceScheduler scheduler(engine, 1, 2, std::chrono::seconds(5));
    yolo::InferenceScheduler foreign(engine, 1, 2, std::chrono::seconds(5));
    expect(!scheduler.isQueued({}), "empty ticket was queued");
    expect(!scheduler.updateQueued({}, input, context("camera", 2)), "empty ticket updated");

    bool observed_update = false;
    for (int attempt = 0; attempt < 16 && !observed_update; ++attempt) {
        // A same-stream in-flight task keeps both colliding source contexts queued.
        auto blocker = scheduler.submitTracked(input, context("camera", 0));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (scheduler.stats().in_flight_count == 0
               && blocker.result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        expect(!scheduler.updateQueued(blocker.ticket, input, context("camera", 100)),
               "in-flight or completed task was updated");
        auto first = scheduler.submitTracked(input, context("camera", 1));
        auto second = scheduler.submitTracked(input, context("camera", 1));
        const auto before = scheduler.stats();
        expect(!foreign.isQueued(first.ticket)
                   && !foreign.updateQueued(first.ticket, input, context("camera", 2)),
               "foreign scheduler accepted a ticket");
        expect(!scheduler.updateQueued(first.ticket, input, context("other", 2)),
               "ticket changed stream identity");
        expect(!scheduler.updateQueued(first.ticket, input, context("camera", 1)),
               "ticket accepted a non-newer frame");
        auto malformed = input;
        malformed.shape[0] = -1;
        expect(!scheduler.updateQueued(first.ticket, std::move(malformed), context("camera", 2)),
               "ticket changed compiled input shape");
        observed_update = scheduler.updateQueued(first.ticket, input, context("camera", 2));
        const auto after = scheduler.stats();
        expect(after.submitted_count == before.submitted_count
                   && after.replaced_count == before.replaced_count
                   && after.updated_count == before.updated_count + (observed_update ? 1 : 0),
               "update fabricated submissions/replacements or lost update count");
        const auto a = first.result.get();
        const auto b = second.result.get();
        (void)blocker.result.get();
        expect(a.status == yolo::ScheduledInferenceStatus::Completed
                   && b.status == yolo::ScheduledInferenceStatus::Completed,
               "tracked tasks did not complete");
        expect(a.result.context.frame_index == (observed_update ? 2 : 1)
                   && b.result.context.frame_index == 1,
               "colliding source contexts shared identity or a lost race changed provenance");
        expect(!scheduler.isQueued(first.ticket)
                   && !scheduler.updateQueued(first.ticket, input, context("camera", 3)),
               "completed ticket was reusable");
    }
    expect(observed_update, "bounded real-inference test never exercised a queued update");
    expect(scheduler.stats().queued_count == 0 && scheduler.stats().in_flight_count == 0,
           "tracked queue did not drain");
}


void testQueuedCancellation(const std::shared_ptr<yolo::YoloEngine>& engine,
                            const yolo::TensorInput& input) {
    yolo::InferenceScheduler scheduler(engine, 1, 4, std::chrono::seconds(5));
    yolo::InferenceScheduler foreign(engine, 1, 4, std::chrono::seconds(5));
    expect(!scheduler.cancelQueued({}), "empty ticket was cancelled");
    size_t cancelled = 0;
    bool front = false, middle_entry = false, back = false;
    for (int attempt = 0; attempt < 16 && !(front && middle_entry && back); ++attempt) {
        auto blocker = scheduler.submitTracked(input, context("cancel-camera", 0));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (scheduler.stats().in_flight_count == 0
               && blocker.result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        expect(!scheduler.cancelQueued(blocker.ticket), "in-flight or completed task cancelled");
        auto first = scheduler.submitTracked(input, context("cancel-camera", 1), true);
        auto middle = scheduler.submitTracked(input, context("cancel-camera", 2));
        auto last = scheduler.submitTracked(input, context("cancel-camera", 3), true);
        expect(!foreign.cancelQueued(middle.ticket), "foreign scheduler cancelled task");
        const auto before = scheduler.stats().cancelled_count;
        for (auto* submission : {&middle, &first, &last}) {
            const bool won = scheduler.cancelQueued(submission->ticket);
            if (won) {
                ++cancelled;
                front = front || submission == &first;
                middle_entry = middle_entry || submission == &middle;
                back = back || submission == &last;
                expect(submission->result.wait_for(std::chrono::milliseconds(0))
                           == std::future_status::ready, "cancelled promise not immediately ready");
            }
            expect(!scheduler.cancelQueued(submission->ticket)
                       && !scheduler.updateQueued(submission->ticket, input, context("cancel-camera", 99)),
                   "cancelled/in-flight/completed identity became reusable");
            const auto result = submission->result.get();
            expect(result.status == (won ? yolo::ScheduledInferenceStatus::Stopped
                                         : yolo::ScheduledInferenceStatus::Completed),
                   "dequeue/cancel race returned wrong status");
            if (won) {
                expect(result.execution_ms == 0.0
                           && result.completed_at == std::chrono::steady_clock::time_point{},
                       "cancel fabricated model execution");
            }
        }
        const auto after = scheduler.stats().cancelled_count;
        expect(after >= before && after == cancelled, "successful cancellation count mismatch");
        auto rebuilt = scheduler.submitTracked(input, context("cancel-camera", 2));
        expect(!scheduler.cancelQueued(middle.ticket), "old ticket cancelled same-context new task");
        expect(rebuilt.result.get().status == yolo::ScheduledInferenceStatus::Completed,
               "new lifecycle task failed after old cancellation");
        expect(blocker.result.get().status == yolo::ScheduledInferenceStatus::Completed,
               "cancellation interrupted model already running");
    }
    expect(front && middle_entry && back, "bounded test missed front/middle/back cancellation");
    const auto stats = scheduler.stats();
    expect(stats.queued_count == 0 && stats.in_flight_count == 0
               && stats.failed_count == 0 && stats.replaced_count == 0 && stats.stale_count == 0
               && stats.submitted_count == stats.completed_count + stats.cancelled_count,
           "cancellation fabricated completions/errors or left queue work");
}

}  // namespace

int main() {
    yolo::AppConfig config;
    config.model_path =
        (std::filesystem::path(YOLO_TEST_SOURCE_DIR) / "deploy/best_640x384.onnx").string();
    config.input_width = 640;
    config.input_height = 384;
    config.num_classes = 10;
    config.thread_num = 8;
    config.infer_request_count = 1;
#if YOLO_ENABLE_OPENVINO
    config.model_backend = "openvino";
    config.openvino_performance_mode = "throughput";
#else
    config.model_backend = "onnx";
#endif

    cv::Mat image(config.input_height, config.input_width, CV_8UC3, cv::Scalar::all(0));
    const auto input = yolo::preprocessImageMat(image, config);
    expect(input.has_value(), "failed to create scheduler test input");

    auto engine = std::make_shared<yolo::YoloEngine>(config);
    yolo::InferenceScheduler scheduler(
        engine,
        1,
        1,
        std::chrono::seconds(5)
    );

    auto first = scheduler.submit(*input, context("stream-a", 0));
    const auto wait_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (scheduler.stats().in_flight_count != 1
           && std::chrono::steady_clock::now() < wait_deadline) {
        std::this_thread::yield();
    }
    expect(scheduler.stats().in_flight_count == 1, "first task never entered inference");

    auto replaced = scheduler.submit(*input, context("stream-b", 10));
    auto latest = scheduler.submit(*input, context("stream-b", 11), true);

    const auto replaced_result = replaced.get();
    const auto first_result = first.get();
    const auto latest_result = latest.get();

    expect(
        replaced_result.status == yolo::ScheduledInferenceStatus::Replaced,
        "old queued frame was not replaced"
    );
    expect(
        first_result.status == yolo::ScheduledInferenceStatus::Completed,
        "in-flight frame did not complete"
    );
    expect(
        latest_result.status == yolo::ScheduledInferenceStatus::Completed,
        "latest frame did not complete"
    );
    expect(
        latest_result.result.context.stream_id == "stream-b"
            && latest_result.result.context.frame_index == 11,
        "latest frame context was not preserved"
    );

    const auto received_at = std::chrono::steady_clock::now();
    for (const auto* result : {&first_result, &latest_result}) {
        expect(result->execution_ms >= result->result.infer_ms
                   && result->completed_at != std::chrono::steady_clock::time_point{}
                   && result->completed_at <= received_at,
               "completed task execution/pickup timing invalid");
    }
    expect(replaced_result.execution_ms == 0.0
               && replaced_result.completed_at == std::chrono::steady_clock::time_point{},
           "replaced task fabricated an execution sample");
    const auto stats = scheduler.stats();
    expect(stats.submitted_count == 3, "scheduler submitted count mismatch");
    expect(stats.completed_count == 2, "scheduler completed count mismatch");
    expect(stats.replaced_count == 1, "scheduler replacement count mismatch");
    expect(stats.queued_count == 0, "scheduler queue did not drain");
    expect(stats.in_flight_count == 0, "scheduler in-flight count did not drain");

    testTrackedUpdates(engine, *input);
    testQueuedCancellation(engine, *input);
    std::cout << "inference_scheduler_test passed\n";
    return 0;
}
