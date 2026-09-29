#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include "config/app_config.h"
#include "drogon/realtime_stream_handlers.h"
#include "model/inference_scheduler.h"
#include "model/yolo_engine.h"
#include "stream/realtime_stream_manager.h"
#include "stream/stream_inference_replay.h"
#include "stream/single_model_inference_replay.h"
#include "image/image_processing.h"

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

std::filesystem::path makeVideo(
    int frame_count = 120,
    const std::string& suffix = "main"
) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("yolo_realtime_stream_" + suffix + "_" + std::to_string(stamp) + ".avi");
    cv::VideoWriter writer(
        path.string(),
        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
        30.0,
        cv::Size(640, 384)
    );
    if (!writer.isOpened()) {
        fail("failed to create realtime stream test video");
    }
    for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
        cv::Mat frame(384, 640, CV_8UC3, cv::Scalar(30, 40, 50));
        const int x = 20 + (frame_index * 4) % 500;
        cv::rectangle(
            frame,
            cv::Rect(x, 120, 80, 100),
            cv::Scalar(220, 220, 220),
            cv::FILLED
        );
        writer.write(frame);
    }
    writer.release();
    return path;
}

constexpr size_t kMjpegFramesPerConnection = 8;

struct FaultCameraServerState {
    std::atomic<uint64_t> connections{0};
    std::atomic<uint64_t> unavailable_connections{0};
    std::atomic<bool> available{true};
};

std::string makeFiniteMjpegBody() {
    std::string body;
    for (size_t frame_index = 0; frame_index < kMjpegFramesPerConnection;
         ++frame_index) {
        cv::Mat frame(120, 160, CV_8UC3, cv::Scalar(25, 35, 45));
        cv::rectangle(
            frame,
            cv::Rect(10 + static_cast<int>(frame_index) * 8, 35, 32, 45),
            cv::Scalar(220, 220, 220),
            cv::FILLED
        );
        std::vector<unsigned char> jpeg;
        if (!cv::imencode(".jpg", frame, jpeg, {cv::IMWRITE_JPEG_QUALITY, 80})) {
            fail("failed to encode MJPEG fault fixture");
        }
        body += "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ";
        body += std::to_string(jpeg.size());
        body += "\r\n\r\n";
        body.append(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
        body += "\r\n";
    }
    body += "--frame--\r\n";
    return body;
}

struct CopyFailureState {
    explicit CopyFailureState(bool nonstandard) : throw_nonstandard(nonstandard) {}
    const bool throw_nonstandard;
    std::atomic<bool> armed{false};
    std::atomic<bool> fail_next_copy{false};
    std::atomic<uint64_t> terminal_count{0};
};

// Trigger a processor exception outside the already-protected callback invocation.
// The failure is one-shot so final terminal delivery can still be verified.
struct ThrowOnceOnCopy {
    std::shared_ptr<CopyFailureState> state;

    explicit ThrowOnceOnCopy(std::shared_ptr<CopyFailureState> shared) : state(std::move(shared)) {}

    ThrowOnceOnCopy(const ThrowOnceOnCopy& other) : state(other.state) {
        if (state->fail_next_copy.exchange(false)) {
            if (state->throw_nonstandard) {
                throw 7;
            }
            throw std::runtime_error("synthetic worker failure with private backend details");
        }
    }

    bool operator()(const yolo::RealtimeFrameEvent& event) const {
        if (event.terminal) {
            ++state->terminal_count;
        } else if (!state->armed.exchange(true)) {
            state->fail_next_copy = true;
        }
        return true;
    }
};

void checkWorkerFailureIsolation(
    yolo::RealtimeStreamManager& manager,
    const std::filesystem::path& video_path,
    bool nonstandard
) {
    const std::string failing_id = nonstandard ? "fault-unknown" : "fault-standard";
    const std::string healthy_id = nonstandard ? "healthy-unknown" : "healthy-standard";
    auto state = std::make_shared<CopyFailureState>(nonstandard);
    std::string error;
    expect(manager.create(failing_id, video_path.string(), error), error);
    expect(manager.subscribe(failing_id, ThrowOnceOnCopy(state)), "fault subscription was rejected");
    expect(manager.create(healthy_id, video_path.string(), error), error);

    yolo::RealtimeStreamSnapshot failing;
    yolo::RealtimeStreamSnapshot healthy;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        expect(manager.get(failing_id, failing), "failing stream disappeared before removal");
        expect(manager.get(healthy_id, healthy), "healthy stream disappeared before removal");
        if (failing.status == "failed" && healthy.status == "completed"
            && state->terminal_count.load() == 1) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    expect(state->armed.load(), "worker exception was never armed");
    expect(failing.status == "failed", "uncaught processor exception did not fail its stream");
    expect(failing.last_error == "processor worker failed", "worker failure reason leaked or was lost");
    expect(failing.inference_error_count == 0, "worker failure was miscounted as a model failure");
    expect(failing.queue_length == 0, "failed stream retained queued frames");
    expect(state->terminal_count.load() == 1, "worker failure did not publish exactly one terminal event");
    expect(healthy.status == "completed", "one worker failure interrupted another stream");
    expect(healthy.decoded_frame_count == 120 && healthy.processed_frame_count == 120,
           "healthy stream lost file frames during another stream's failure");
    expect(healthy.inference_error_count == 0 && healthy.dropped_frame_count == 0,
           "healthy stream reported errors or drops during worker isolation test");
    expect(!manager.subscribe(failing_id, [](const yolo::RealtimeFrameEvent&) { return true; }),
           "failed stream accepted a new subscriber");
    expect(manager.metrics().active_count == 0, "terminal isolation fixtures remained active");
    // This joins both failed-stream workers, including a decoder that may be waiting for queue space.
    expect(manager.stop(failing_id), "failed stream could not be joined and removed");
    expect(manager.stop(healthy_id), "healthy stream could not be removed");
    expect(manager.metrics().streams.empty(), "worker-isolation cleanup retained stream records");
}

void checkTerminalReplayDuringLogging(yolo::RealtimeStreamManager& manager, bool fail_worker) {
    const auto video_path = makeVideo(10, "terminal-window");
    const std::string stream_id = fail_worker ? "terminal-window-failed" : "terminal-window-completed";
    const std::string id_field = "\"stream_id\":\"" + stream_id + "\"";
    std::promise<void> logging_started;
    auto logging_future = logging_started.get_future();
    std::promise<void> release_logging;
    const auto release_future = release_logging.get_future().share();
    std::atomic<bool> intercepted{false};
    const auto previous_level = trantor::Logger::logLevel();
    trantor::Logger::setLogLevel(trantor::Logger::kInfo);
    trantor::Logger::setOutputFunction(
        [&](const char* data, uint64_t length) {
            const std::string_view line(data, static_cast<size_t>(length));
            if (line.find("\"event\":\"stream_finished\"") != std::string_view::npos
                && line.find(id_field) != std::string_view::npos
                && !intercepted.exchange(true)) {
                logging_started.set_value();
                release_future.wait();
            }
        },
        []() {}
    );
    std::string error;
    expect(manager.create(stream_id, video_path.string(), error), error);
    auto failure = std::make_shared<CopyFailureState>(false);
    if (fail_worker) {
        expect(manager.subscribe(stream_id, ThrowOnceOnCopy(failure)),
               "terminal-window fault subscription was rejected");
    }
    expect(logging_future.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
           "terminal-window finish log was not intercepted");
    yolo::RealtimeStreamSnapshot snapshot;
    expect(manager.get(stream_id, snapshot), "terminal-window stream disappeared");
    expect(snapshot.status == (fail_worker ? "failed" : "completed"),
           "terminal-window stream did not reach its expected status");
    expect(snapshot.latest_sequence > 0, "terminal-window fixture published no frames");
    const auto frame_sequence = snapshot.latest_sequence;
    expect(!manager.subscribe(stream_id, [](const yolo::RealtimeFrameEvent&) { return true; }),
           "terminal-window accepted a plain new subscription");

    std::vector<yolo::RealtimeFrameEvent> latest_events;
    std::vector<yolo::RealtimeFrameEvent> tail_events;
    std::promise<void> latest_terminal;
    std::promise<void> tail_terminal;
    auto latest_future = latest_terminal.get_future();
    auto tail_future = tail_terminal.get_future();
    const auto latest = manager.subscribeAfter(stream_id, frame_sequence,
        [&](const yolo::RealtimeFrameEvent& event) {
            latest_events.push_back(event);
            if (event.terminal) { latest_terminal.set_value(); }
            return true;
        });
    const auto tail = manager.subscribeAfter(stream_id, frame_sequence - 1,
        [&](const yolo::RealtimeFrameEvent& event) {
            tail_events.push_back(event);
            if (event.terminal) { tail_terminal.set_value(); }
            return true;
        });
    expect(latest.status == yolo::RealtimeSubscribeStatus::Subscribed,
           "terminal-window rejected a cursor waiting for terminal delivery");
    expect(tail.status == yolo::RealtimeSubscribeStatus::Subscribed,
           "terminal-window rejected retained frame replay");
    release_logging.set_value();
    expect(latest_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
           "terminal-window latest cursor lost its terminal event");
    expect(tail_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
           "terminal-window replay tail lost its terminal event");
    expect(manager.stop(stream_id), "terminal-window stream could not be removed");
    expect(latest_events.size() == 1 && latest_events.front().terminal
               && latest_events.front().sequence == frame_sequence + 1,
           "terminal-window latest cursor received duplicate or wrong events");
    expect(tail_events.size() == 2 && !tail_events.front().terminal
               && tail_events.front().sequence == frame_sequence
               && tail_events.back().terminal
               && tail_events.back().sequence == frame_sequence + 1,
           "terminal-window retained frame and terminal were not delivered in order");
    trantor::Logger::setOutputFunction(
        [](const char* data, uint64_t length) { std::fwrite(data, 1, static_cast<size_t>(length), stdout); },
        []() { std::fflush(stdout); }
    );
    trantor::Logger::setLogLevel(previous_level);
    std::error_code ignored;
    std::filesystem::remove(video_path, ignored);
}

void checkEventReplay(yolo::RealtimeStreamManager& manager) {
    constexpr int kReplayFrameCount = 280;
    const auto video_path = makeVideo(kReplayFrameCount, "replay");
    const std::string stream_id = "replay-history";
    std::string error;
    expect(manager.create(stream_id, video_path.string(), error), error);

    yolo::RealtimeStreamSnapshot snapshot;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        expect(manager.get(stream_id, snapshot), "replay stream disappeared");
        if (snapshot.status == "failed"
            || (snapshot.status == "completed"
                && snapshot.latest_sequence == kReplayFrameCount + 1)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(snapshot.status == "completed", "replay fixture did not complete");
    expect(snapshot.latest_sequence == kReplayFrameCount + 1,
           "replay fixture sequence count mismatch");

    std::vector<yolo::RealtimeFrameEvent> tail_events;
    const auto tail = manager.subscribeAfter(
        stream_id,
        snapshot.latest_sequence - 3,
        [&tail_events](const yolo::RealtimeFrameEvent& event) {
            tail_events.push_back(event);
            return true;
        }
    );
    expect(tail.status == yolo::RealtimeSubscribeStatus::Subscribed,
           "retained replay tail was rejected");
    expect(tail_events.size() == 3, "replay tail size mismatch");
    for (size_t index = 0; index < tail_events.size(); ++index) {
        expect(tail_events[index].sequence == snapshot.latest_sequence - 2 + index,
               "replayed event sequence was missing or out of order");
    }
    expect(tail_events.back().terminal, "terminal event was not replayed");

    const auto expired = manager.subscribeAfter(
        stream_id, 0, [](const yolo::RealtimeFrameEvent&) { return true; }
    );
    expect(expired.status == yolo::RealtimeSubscribeStatus::HistoryExpired,
           "expired replay cursor was accepted");
    expect(expired.earliest_available_sequence > 1,
           "event buffer did not evict old history");
    expect(expired.latest_sequence == snapshot.latest_sequence,
           "history error reported the wrong latest sequence");

    uint64_t replay_count = 0;
    uint64_t first_sequence = 0;
    uint64_t last_sequence = 0;
    const auto full_window = manager.subscribeAfter(
        stream_id,
        expired.earliest_available_sequence - 1,
        [&](const yolo::RealtimeFrameEvent& event) {
            if (replay_count++ == 0) {
                first_sequence = event.sequence;
            }
            last_sequence = event.sequence;
            return true;
        }
    );
    expect(full_window.status == yolo::RealtimeSubscribeStatus::Subscribed,
           "earliest retained replay cursor was rejected");
    expect(replay_count == 256, "retained replay window size changed");
    expect(first_sequence == expired.earliest_available_sequence
               && last_sequence == snapshot.latest_sequence,
           "retained replay window boundaries were incorrect");

    const auto ahead = manager.subscribeAfter(
        stream_id, snapshot.latest_sequence + 1,
        [](const yolo::RealtimeFrameEvent&) { return true; }
    );
    expect(ahead.status == yolo::RealtimeSubscribeStatus::CursorAhead,
           "future replay cursor was accepted");
    const auto exhausted = manager.subscribeAfter(
        stream_id, snapshot.latest_sequence,
        [](const yolo::RealtimeFrameEvent&) { return true; }
    );
    expect(exhausted.status == yolo::RealtimeSubscribeStatus::StreamUnavailable,
           "terminal stream accepted an exhausted replay cursor");

    expect(manager.stop(stream_id), "replay fixture could not be removed");
    std::error_code ignored;
    std::filesystem::remove(video_path, ignored);
}

bool countersAtLeast(
    const yolo::RealtimeStreamCounters& current,
    const yolo::RealtimeStreamCounters& previous
) {
    for (size_t i = 0; i < current.processing_diagnostics.size(); ++i) {
        const auto& a = current.processing_diagnostics[i];
        const auto& b = previous.processing_diagnostics[i];
        if (a.count < b.count || a.sum_ms < b.sum_ms || a.max_ms < b.max_ms
            || !std::isfinite(a.sum_ms) || !std::isfinite(a.max_ms)) {
            return false;
        }
    }
    return current.decoded_frame_count >= previous.decoded_frame_count
        && current.processed_frame_count >= previous.processed_frame_count
        && current.dropped_frame_count >= previous.dropped_frame_count
        && current.decoder_queue_drop_count >= previous.decoder_queue_drop_count
        && current.processor_coalesced_frame_count
            >= previous.processor_coalesced_frame_count
        && current.stale_frame_drop_count >= previous.stale_frame_drop_count
        && current.skipped_inference_count >= previous.skipped_inference_count
        && current.inference_error_count >= previous.inference_error_count
        && current.weak_flow_roi_count >= previous.weak_flow_roi_count
        && current.weak_flow_roi_pixels >= previous.weak_flow_roi_pixels
        && current.weak_flow_sampled_points >= previous.weak_flow_sampled_points;
}

void checkConcurrentMetricsAndRemoval(
    yolo::RealtimeStreamManager& manager,
    const std::filesystem::path& video_path
) {
    const auto baseline = manager.metrics();
    expect(baseline.streams.empty(), "concurrency fixture started with registered streams");

    std::string error;
    constexpr const char* kStreamId = "metrics-race";
    expect(manager.create(kStreamId, video_path.string(), error), error);

    std::atomic<bool> readers_done{false};
    std::atomic<unsigned> failure_mask{0};
    std::atomic<uint64_t> read_count{0};
    std::vector<std::thread> readers;
    for (int reader_index = 0; reader_index < 3; ++reader_index) {
        readers.emplace_back([&] {
            auto previous = baseline.totals;
            while (!readers_done.load(std::memory_order_relaxed)) {
                const auto metrics = manager.metrics();
                if (metrics.active_count > metrics.streams.size()) {
                    failure_mask.fetch_or(1U, std::memory_order_relaxed);
                }
                if (!countersAtLeast(metrics.totals, previous)) {
                    failure_mask.fetch_or(2U, std::memory_order_relaxed);
                }
                previous = metrics.totals;

                const auto streams = manager.list();
                for (size_t index = 1; index < streams.size(); ++index) {
                    if (streams[index - 1].stream_id >= streams[index].stream_id) {
                        failure_mask.fetch_or(4U, std::memory_order_relaxed);
                    }
                }
                read_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
    }

    const auto read_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (read_count.load(std::memory_order_relaxed) < 300
           && std::chrono::steady_clock::now() < read_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(read_count.load(std::memory_order_relaxed) >= 30,
           "metrics readers did not overlap the stream lifecycle");

    yolo::RealtimeStreamSnapshot completed;
    const auto completion_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < completion_deadline) {
        expect(manager.get(kStreamId, completed),
               "concurrency fixture disappeared before explicit removal");
        if (completed.status == "completed" || completed.status == "failed") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(completed.status == "completed", "concurrency fixture did not complete");

    const auto before_removal = manager.metrics();
    expect(manager.stop(kStreamId), "concurrent metrics fixture could not be removed");
    readers_done.store(true, std::memory_order_relaxed);
    for (auto& reader : readers) {
        reader.join();
    }

    expect(failure_mask.load(std::memory_order_relaxed) == 0,
           "concurrent metrics observed an invalid or decreasing snapshot");
    const auto after_removal = manager.metrics();
    expect(after_removal.streams.empty(), "concurrent removal retained the stream record");
    expect(countersAtLeast(after_removal.totals, before_removal.totals)
               && countersAtLeast(before_removal.totals, after_removal.totals),
           "concurrent removal lost or duplicated manager-lifetime counters");
}

struct HttpReply {
    bool ok = false;
    int status = 0;
    std::string body;
};

HttpReply sendHttp(
    const drogon::HttpClientPtr& client,
    drogon::HttpMethod method,
    const std::string& path,
    const Json::Value* json = nullptr
) {
    auto request = json == nullptr
        ? drogon::HttpRequest::newHttpRequest()
        : drogon::HttpRequest::newHttpJsonRequest(*json);
    request->setMethod(method);
    request->setPath(path);
    const auto [result, response] = client->sendRequest(request, 3.0);
    if (result != drogon::ReqResult::Ok || response == nullptr) {
        return {};
    }
    return {
        true,
        static_cast<int>(response->getStatusCode()),
        std::string(response->body()),
    };
}

std::optional<uint64_t> unsignedMetric(
    const std::string& body,
    const std::string& name
) {
    std::istringstream input(body);
    std::string line;
    const std::string prefix = name + " ";
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) != 0) {
            continue;
        }
        try {
            size_t consumed = 0;
            const uint64_t value = std::stoull(line.substr(prefix.size()), &consumed);
            if (consumed == line.size() - prefix.size()) {
                return value;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<double> processingMetric(const std::string& body, const std::string& name) {
    std::istringstream input(body);
    std::string line;
    const std::string prefix = name + " ";
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) != 0) {
            continue;
        }
        try {
            size_t consumed = 0;
            const double value = std::stod(line.substr(prefix.size()), &consumed);
            if (consumed == line.size() - prefix.size() && std::isfinite(value) && value >= 0.0) {
                return value;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void checkHttpMetricsAndRecovery(
    const std::shared_ptr<yolo::YoloEngine>& high_engine,
    const std::shared_ptr<yolo::YoloEngine>& low_engine,
    const std::shared_ptr<yolo::InferenceScheduler>& high_scheduler,
    const std::shared_ptr<yolo::InferenceScheduler>& low_scheduler,
    const yolo::AppConfig& config,
    const std::filesystem::path& video_path
) {
    auto recovery_config = config;
    recovery_config.max_streams = 4;
    auto manager = std::make_shared<yolo::RealtimeStreamManager>(
        high_engine, low_engine, high_scheduler, low_scheduler, recovery_config
    );
    yolo::api::registerRealtimeStreamRoutes(
        manager, high_scheduler, low_scheduler, nullptr, nullptr,
        recovery_config.class_names
    );

    auto& app = drogon::app();
    constexpr size_t kFaultStreamCount = 4;
    std::array<FaultCameraServerState, kFaultStreamCount> camera_states;
    const std::string mjpeg_body = makeFiniteMjpegBody();
    for (size_t index = 0; index < kFaultStreamCount; ++index) {
        auto* const state = &camera_states[index];
        app.registerHandler(
            "/test/fault-camera-" + std::to_string(index + 1) + ".mjpg",
            [mjpeg_body, state](
                const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback
            ) {
                state->connections.fetch_add(1, std::memory_order_relaxed);
                auto response = drogon::HttpResponse::newHttpResponse();
                if (!state->available.load(std::memory_order_relaxed)) {
                    state->unavailable_connections.fetch_add(
                        1, std::memory_order_relaxed
                    );
                    response->setStatusCode(drogon::k503ServiceUnavailable);
                    response->setCloseConnection(true);
                    callback(response);
                    return;
                }
                response->setStatusCode(drogon::k200OK);
                response->setContentTypeString(
                    "multipart/x-mixed-replace; boundary=frame"
                );
                response->setCloseConnection(true);
                response->setBody(mjpeg_body);
                callback(response);
            },
            {drogon::Get}
        );
    }

    std::promise<uint16_t> listener_ready;
    auto listener_future = listener_ready.get_future();
    app.setThreadNum(4);
    app.addListener("127.0.0.1", 0);
    app.registerBeginningAdvice([&listener_ready] {
        const auto listeners = drogon::app().getListeners();
        listener_ready.set_value(listeners.empty() ? 0 : listeners.front().toPort());
    });
    std::thread server([&app] { app.run(); });

    const auto shutdown = [&] {
        app.quit();
        if (server.joinable()) {
            server.join();
        }
    };
    if (listener_future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        shutdown();
        fail("Drogon test listener did not start");
    }
    const uint16_t port = listener_future.get();
    if (port == 0) {
        shutdown();
        fail("Drogon test listener did not expose a dynamic port");
    }

    auto control = drogon::HttpClient::newHttpClient("127.0.0.1", port);

    std::array<std::string, kFaultStreamCount> fault_stream_ids;
    std::vector<std::string> created_fault_streams;
    for (size_t index = 0; index < kFaultStreamCount; ++index) {
        fault_stream_ids[index] =
            "http-camera-recovery-" + std::to_string(index + 1);
    }
    const auto cleanupFaultStreams = [&]() {
        bool all_deleted = true;
        for (auto item = created_fault_streams.rbegin();
             item != created_fault_streams.rend(); ++item) {
            const auto deleted = sendHttp(
                control, drogon::Delete, "/streams/" + *item
            );
            if (!deleted.ok || deleted.status != 200) {
                all_deleted = false;
                (void)manager->stop(*item);
            }
        }
        created_fault_streams.clear();
        return all_deleted;
    };

    for (size_t index = 0; index < kFaultStreamCount; ++index) {
        Json::Value fault_payload;
        fault_payload["stream_id"] = fault_stream_ids[index];
        fault_payload["source"] =
            "http://127.0.0.1:" + std::to_string(port)
            + "/test/fault-camera-" + std::to_string(index + 1) + ".mjpg";
        const auto created =
            sendHttp(control, drogon::Post, "/streams", &fault_payload);
        if (!created.ok || created.status != 201) {
            (void)cleanupFaultStreams();
            shutdown();
            fail("HTTP camera recovery fixture could not create all streams");
        }
        created_fault_streams.push_back(fault_stream_ids[index]);
    }

    std::array<std::optional<uint64_t>, kFaultStreamCount> decoded_at_disconnect;
    std::array<yolo::RealtimeStreamSnapshot, kFaultStreamCount> fault_snapshots;
    bool recovered = false;
    bool outage_started = false;
    bool recovery_enabled = false;
    const auto recovery_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < recovery_deadline) {
        bool all_present = true;
        bool all_disconnected = true;
        for (size_t index = 0; index < kFaultStreamCount; ++index) {
            if (!manager->get(fault_stream_ids[index], fault_snapshots[index])) {
                all_present = false;
                break;
            }
            if (!decoded_at_disconnect[index].has_value()
                && fault_snapshots[index].reconnect_count >= 1
                && fault_snapshots[index].decoded_frame_count > 0) {
                decoded_at_disconnect[index] =
                    fault_snapshots[index].decoded_frame_count;
            }
            all_disconnected =
                all_disconnected && decoded_at_disconnect[index].has_value();
        }
        if (!all_present) {
            break;
        }
        if (all_disconnected && !outage_started) {
            for (auto& state : camera_states) {
                state.available.store(false, std::memory_order_relaxed);
            }
            outage_started = true;
        }
        if (outage_started && !recovery_enabled) {
            bool all_failed_twice = true;
            for (const auto& state : camera_states) {
                all_failed_twice =
                    all_failed_twice
                    && state.unavailable_connections.load(
                           std::memory_order_relaxed
                       ) >= 2;
            }
            if (all_failed_twice) {
                for (auto& state : camera_states) {
                    state.available.store(true, std::memory_order_relaxed);
                }
                recovery_enabled = true;
            }
        }

        bool all_recovered = recovery_enabled;
        for (size_t index = 0; index < kFaultStreamCount; ++index) {
            all_recovered =
                all_recovered
                && decoded_at_disconnect[index].has_value()
                && camera_states[index].connections.load(
                       std::memory_order_relaxed
                   ) >= 4
                && fault_snapshots[index].reconnect_count >= 3
                && fault_snapshots[index].decoded_frame_count
                    > *decoded_at_disconnect[index];
        }
        if (all_recovered) {
            recovered = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!recovered) {
        std::ostringstream details;
        details << "multi-camera recovery failed";
        for (size_t index = 0; index < kFaultStreamCount; ++index) {
            details << " [" << index
                    << ": connections="
                    << camera_states[index].connections.load(
                           std::memory_order_relaxed
                       )
                    << ", unavailable="
                    << camera_states[index].unavailable_connections.load(
                           std::memory_order_relaxed
                       )
                    << ", reconnects=" << fault_snapshots[index].reconnect_count
                    << ", decoded=" << fault_snapshots[index].decoded_frame_count
                    << ']';
        }
        (void)cleanupFaultStreams();
        shutdown();
        fail(details.str());
    }
    if (!cleanupFaultStreams()) {
        shutdown();
        fail("recovered HTTP camera streams could not be removed");
    }
    for (const auto& stream_id : fault_stream_ids) {
        yolo::RealtimeStreamSnapshot removed;
        if (manager->get(stream_id, removed)) {
            shutdown();
            fail("recovered HTTP camera stream remained registered");
        }
    }
    const uint64_t decoded_before_http_churn =
        manager->metrics().totals.decoded_frame_count;

    constexpr const char* kStreamId = "http-metrics-race";
    const std::string stream_path = std::string("/streams/") + kStreamId;
    Json::Value create_payload;
    create_payload["stream_id"] = kStreamId;
    create_payload["source"] = video_path.string();
    const auto created = sendHttp(control, drogon::Post, "/streams", &create_payload);
    if (!created.ok || created.status != 201) {
        shutdown();
        fail("HTTP concurrency fixture could not create its first stream");
    }

    auto waitForDecodedAbove = [&](uint64_t minimum) -> std::optional<uint64_t> {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto reply = sendHttp(control, drogon::Get, "/metrics");
            const auto decoded = reply.ok && reply.status == 200
                ? unsignedMetric(reply.body, "yolo_stream_decoded_frames_total")
                : std::nullopt;
            if (decoded.has_value() && *decoded > minimum) {
                return decoded;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return std::nullopt;
    };

    const auto first_decoded = waitForDecodedAbove(decoded_before_http_churn);
    if (!first_decoded.has_value()) {
        (void)manager->stop(kStreamId);
        shutdown();
        fail("first HTTP stream produced no decoded metrics");
    }

    std::atomic<bool> scrapers_done{false};
    std::atomic<unsigned> failure_mask{0};
    std::atomic<uint64_t> scrape_count{0};
    std::vector<std::thread> scrapers;
    for (int scraper_index = 0; scraper_index < 4; ++scraper_index) {
        scrapers.emplace_back([&, port] {
            try {
                auto client = drogon::HttpClient::newHttpClient("127.0.0.1", port);
                uint64_t previous_decoded = 0;
                uint64_t previous_processed = 0;
                std::array<double, yolo::kStreamProcessingStageNames.size() * 3> previous_timings{};
                std::array<uint64_t, 3> previous_weak_load{};
                while (!scrapers_done.load(std::memory_order_relaxed)) {
                    const auto reply = sendHttp(client, drogon::Get, "/metrics");
                    if (!reply.ok || reply.status != 200
                        || reply.body.find("# TYPE yolo_streams_active gauge")
                            == std::string::npos) {
                        failure_mask.fetch_or(1U, std::memory_order_relaxed);
                        continue;
                    }
                    const auto active = unsignedMetric(reply.body, "yolo_streams_active");
                    const auto registered =
                        unsignedMetric(reply.body, "yolo_streams_registered");
                    const auto decoded =
                        unsignedMetric(reply.body, "yolo_stream_decoded_frames_total");
                    const auto processed =
                        unsignedMetric(reply.body, "yolo_stream_processed_frames_total");
                    if (!active || !registered || !decoded || !processed) {
                        failure_mask.fetch_or(2U, std::memory_order_relaxed);
                    } else {
                        if (*active > *registered || *registered > 1) {
                            failure_mask.fetch_or(4U, std::memory_order_relaxed);
                        }
                        if (*decoded < previous_decoded || *processed < previous_processed) {
                            failure_mask.fetch_or(8U, std::memory_order_relaxed);
                        }
                        previous_decoded = *decoded;
                        previous_processed = *processed;
                    }
                    size_t timing_index = 0;
                    for (const char* stage : yolo::kStreamProcessingStageNames) {
                        for (const char* field : {"count", "sum", "max"}) {
                            const auto value = processingMetric(reply.body,
                                "yolo_stream_processing_stage_seconds_" + std::string(field)
                                + "{stage=\"" + stage + "\"}");
                            if (!value || *value < previous_timings[timing_index]) {
                                failure_mask.fetch_or(32U, std::memory_order_relaxed);
                            } else {
                                previous_timings[timing_index] = *value;
                            }
                            ++timing_index;
                        }
                    }
                    size_t load_index = 0;
                    for (const char* name : {"yolo_stream_weak_flow_rois_total",
                                             "yolo_stream_weak_flow_roi_pixels_total",
                                             "yolo_stream_weak_flow_sampled_points_total"}) {
                        const auto value = unsignedMetric(reply.body, name);
                        if (!value || *value < previous_weak_load[load_index]) {
                            failure_mask.fetch_or(2048U, std::memory_order_relaxed);
                        } else {
                            previous_weak_load[load_index] = *value;
                        }
                        ++load_index;
                    }
                    scrape_count.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                }
            } catch (...) {
                failure_mask.fetch_or(16U, std::memory_order_relaxed);
            }
        });
    }

    const auto scrape_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (scrape_count.load(std::memory_order_relaxed) < 100
           && std::chrono::steady_clock::now() < scrape_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    constexpr unsigned kLifecycleCycles = 10;
    std::optional<uint64_t> retired_decoded;
    bool stream_registered = true;
    for (unsigned cycle = 0; cycle < kLifecycleCycles; ++cycle) {
        const auto deleted = sendHttp(control, drogon::Delete, stream_path);
        if (!deleted.ok || deleted.status != 200) {
            failure_mask.fetch_or(32U, std::memory_order_relaxed);
            break;
        }
        stream_registered = false;

        const auto retired_reply = sendHttp(control, drogon::Get, "/metrics");
        retired_decoded = retired_reply.ok && retired_reply.status == 200
            ? unsignedMetric(retired_reply.body, "yolo_stream_decoded_frames_total")
            : std::nullopt;
        if (!retired_decoded.has_value()) {
            failure_mask.fetch_or(64U, std::memory_order_relaxed);
            break;
        }
        if (cycle + 1 == kLifecycleCycles) {
            break;
        }

        const auto recreated = sendHttp(control, drogon::Post, "/streams", &create_payload);
        if (!recreated.ok || recreated.status != 201) {
            failure_mask.fetch_or(128U, std::memory_order_relaxed);
            break;
        }
        stream_registered = true;
        if (!waitForDecodedAbove(*retired_decoded).has_value()) {
            failure_mask.fetch_or(256U, std::memory_order_relaxed);
            break;
        }
    }
    if (stream_registered) {
        (void)manager->stop(kStreamId);
    }

    scrapers_done.store(true, std::memory_order_relaxed);
    for (auto& scraper : scrapers) {
        scraper.join();
    }

    const auto final_reply = sendHttp(control, drogon::Get, "/metrics");
    const auto final_active = final_reply.ok
        ? unsignedMetric(final_reply.body, "yolo_streams_active")
        : std::nullopt;
    const auto final_registered = final_reply.ok
        ? unsignedMetric(final_reply.body, "yolo_streams_registered")
        : std::nullopt;
    const auto final_decoded = final_reply.ok
        ? unsignedMetric(final_reply.body, "yolo_stream_decoded_frames_total")
        : std::nullopt;
    if (!final_reply.ok || final_reply.status != 200
        || !final_active || !final_registered || !final_decoded
        || *final_active != 0 || *final_registered != 0
        || !retired_decoded || *retired_decoded <= *first_decoded
        || *final_decoded != *retired_decoded) {
        failure_mask.fetch_or(512U, std::memory_order_relaxed);
    }
    if (scrape_count.load(std::memory_order_relaxed) < 40) {
        failure_mask.fetch_or(1024U, std::memory_order_relaxed);
    }

    control.reset();
    shutdown();
    const unsigned failures = failure_mask.load(std::memory_order_relaxed);
    expect(failures == 0,
           "concurrent HTTP /metrics scrape with DELETE/recreate failed, mask="
               + std::to_string(failures));
}


void testQueuedReplaySourceUpdate(const std::shared_ptr<yolo::YoloEngine>& engine,
                                 const yolo::AppConfig& config) {
    const auto input = yolo::preprocessImageMat(
        cv::Mat(config.input_height, config.input_width, CV_8UC3, cv::Scalar::all(0)), config);
    expect(input.has_value(), "queued replay input missing");
    yolo::InferenceScheduler scheduler(engine, 1, 1, std::chrono::seconds(5));
    bool observed_update = false;
    for (int attempt = 0; attempt < 16 && !observed_update; ++attempt) {
        auto blocker = scheduler.submitTracked(*input, {"camera", 0, 0.0});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (scheduler.stats().in_flight_count == 0
               && blocker.result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        auto submitted = scheduler.submitTracked(*input, {"camera", 1, 33.0});

        yolo::StreamInferenceReplay replay("camera", 3000);
        for (int frame_index : {1, 2}) {
            yolo::AuthorityReplayFrame frame;
            frame.frame_index = frame_index;
            frame.image_width = config.input_width;
            frame.image_height = config.input_height;
            frame.motion.frame_index = frame_index;
            replay.append(std::move(frame));
        }
        const auto old_capture = std::chrono::steady_clock::now() - std::chrono::seconds(5);
        replay.submit(std::move(submitted.result), {"camera", 1, 33.0}, old_capture,
                      true, std::nullopt, 1.0, submitted.ticket);
        const yolo::HighResRoiSelection roi{cv::Rect(10, 20, 100, 80), {10, 20, 110, 100}};
        const auto new_capture = std::chrono::steady_clock::now();
        observed_update = replay.updateQueued(scheduler, *input, {"camera", 2, 66.0},
                                             new_capture, true, roi, 3.0);
        (void)blocker.result.get();
        yolo::StreamInferencePoll polled;
        while (replay.busy(true) && std::chrono::steady_clock::now() < deadline) {
            polled = replay.poll();
            std::this_thread::yield();
        }
        expect(!replay.busy(true) && polled.failed.empty(), "queued replay did not settle");
        if (observed_update) {
            expect(polled.applied.size() == 1 && polled.applied[0].context.frame_index == 2
                       && polled.applied[0].context.timestamp_ms == 66.0
                       && polled.applied[0].high_res_roi,
                   "updated result used old provenance/ROI or old capture deadline");
            const auto& preprocess = polled.diagnostics[0].stages[
                static_cast<size_t>(yolo::StreamInferenceStage::Preprocess)];
            expect(preprocess.count == 1 && preprocess.sum_ms == 3.0,
                   "updated result retained old preprocessing timing");
        } else {
            expect(polled.applied.empty() && polled.skipped_count == 1,
                   "lost update race refreshed old result capture/provenance");
        }
    }
    expect(observed_update, "bounded test never exercised updated source replay");

    auto completed = scheduler.submitTracked(*input, {"camera", 1, 33.0});
    completed.result.wait(); // Keep the ready future, but force its queued ticket to be invalid.
    yolo::StreamInferenceReplay unchanged("camera", 3000);
    yolo::AuthorityReplayFrame frame;
    frame.frame_index = 1;
    frame.image_width = config.input_width;
    frame.image_height = config.input_height;
    unchanged.append(std::move(frame));
    unchanged.submit(std::move(completed.result), {"camera", 1, 33.0},
                     std::chrono::steady_clock::now() - std::chrono::seconds(5),
                     true, std::nullopt, 1.0, completed.ticket);
    expect(!unchanged.updateQueued(scheduler, *input, {"camera", 2, 66.0},
                                  std::chrono::steady_clock::now(), true),
           "ready task accepted a source update");
    const auto rejected = unchanged.poll();
    expect(rejected.failed.empty() && rejected.applied.empty() && rejected.skipped_count == 1,
           "rejected update changed old source context or refreshed its capture deadline");
}


void testQueuedSingleModelSourceUpdate(const std::shared_ptr<yolo::YoloEngine>& engine,
                                      const yolo::AppConfig& config) {
    cv::Mat image(config.input_height, config.input_width, CV_8UC3, cv::Scalar::all(0));
    const auto input = yolo::preprocessImageMat(image, config);
    expect(input.has_value(), "single-model queued input missing");
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    yolo::InferenceScheduler scheduler(engine, 1, 1, std::chrono::seconds(5));
    bool updated = false;
    for (int attempt = 0; attempt < 16 && !updated; ++attempt) {
        auto blocker = scheduler.submitTracked(*input, {"single", 0, 0.0});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (scheduler.stats().in_flight_count == 0
               && blocker.result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        auto submitted = scheduler.submitTracked(*input, {"single", 1, 33.0});
        yolo::SingleModelInferenceReplay replay("single", 3000);
        replay.append(1, gray);
        replay.append(2, gray);
        const auto old_capture = std::chrono::steady_clock::now() - std::chrono::seconds(5);
        replay.submit(std::move(submitted.result), {"single", 1, 33.0},
                      old_capture, 1.0, submitted.ticket);
        updated = replay.updateQueued(scheduler, *input, {"single", 2, 66.0},
                                      std::chrono::steady_clock::now(), 3.0,
                                      yolo::SingleModelStreamState{}, gray);
        (void)blocker.result.get();
        yolo::SingleModelInferencePoll polled;
        while (replay.busy() && std::chrono::steady_clock::now() < deadline) {
            polled = replay.poll();
            std::this_thread::yield();
        }
        expect(!replay.busy() && polled.failed.empty(), "single-model queued result did not settle");
        if (updated) {
            expect(replay.frameCount() == 1, "single-model queued update retained old prefix");
            expect(polled.tracker && polled.applied.size() == 1
                       && polled.applied[0].context.frame_index == 2
                       && polled.applied[0].context.timestamp_ms == 66.0
                       && polled.diagnostics.stages[
                           static_cast<size_t>(yolo::StreamInferenceStage::Preprocess)].sum_ms == 3.0,
                   "single-model queued update lost provenance/capture/preprocessing metadata");
        } else {
            expect(replay.frameCount() == 2, "single-model failed update discarded old prefix");
            expect(!polled.tracker && polled.skipped_count == 1,
                   "single-model lost update race changed the old capture deadline");
        }
    }
    expect(updated, "single-model bounded regression never exercised a successful queued update");
    auto completed = scheduler.submitTracked(*input, {"single", 1, 33.0});
    completed.result.wait();
    yolo::SingleModelInferenceReplay ready("single", 300);
    ready.append(1, gray);
    ready.append(2, gray);
    ready.submit(std::move(completed.result), {"single", 1, 33.0},
                 std::chrono::steady_clock::now() - std::chrono::seconds(5), 1.0, completed.ticket);
    expect(!ready.updateQueued(scheduler, *input, {"single", 2, 66.0},
                              std::chrono::steady_clock::now(), 3.0,
                              yolo::SingleModelStreamState{}, gray),
           "single-model ready inference accepted a source update");
    expect(ready.frameCount() == 2, "single-model ready rejection discarded source history");
    auto rejected = ready.poll();
    expect(rejected.failed.empty() && !rejected.tracker && rejected.skipped_count == 1,
           "single-model rejected update changed context or refreshed the capture deadline");
}

}  // namespace

int main() {
    const auto video_path = makeVideo();
    const auto model_path =
        (std::filesystem::path(YOLO_TEST_SOURCE_DIR) / "deploy/best_640x384.onnx").string();

    yolo::AppConfig config;
    config.model_path = model_path;
    config.low_res_model_path = model_path;
    config.input_width = 640;
    config.input_height = 384;
    config.low_res_input_width = 640;
    config.low_res_input_height = 384;
    config.num_classes = 10;
    config.thread_num = 4;
    config.infer_request_count = 1;
    config.low_res_infer_request_count = 1;
    config.video_detect_fps = 4.0F;
    config.max_streams = 2;
    config.per_stream_queue_depth = 2;
    config.max_request_age_ms = 5000;
    // Even a tiny live deadline must not discard offline file frames.
    config.max_result_age_ms = 1;
#if YOLO_ENABLE_OPENVINO
    config.model_backend = "openvino";
    config.openvino_performance_mode = "throughput";
#else
    config.model_backend = "onnx";
#endif

    auto high_engine = std::make_shared<yolo::YoloEngine>(
        yolo::makeHighResAppConfig(config)
    );
    auto low_engine = std::make_shared<yolo::YoloEngine>(
        yolo::makeLowResAppConfig(config)
    );
    auto high_scheduler = std::make_shared<yolo::InferenceScheduler>(
        high_engine, high_engine->maxConcurrency(), 2, std::chrono::seconds(5)
    );
    testQueuedReplaySourceUpdate(high_engine, yolo::makeHighResAppConfig(config));
    testQueuedSingleModelSourceUpdate(high_engine, yolo::makeHighResAppConfig(config));
    auto low_scheduler = std::make_shared<yolo::InferenceScheduler>(
        low_engine, low_engine->maxConcurrency(), 2, std::chrono::seconds(5)
    );

    yolo::RealtimeStreamManager manager(
        high_engine,
        low_engine,
        high_scheduler,
        low_scheduler,
        config
    );

    std::string error;
    expect(
        !manager.create("bad stream id", video_path.string(), error),
        "invalid stream ID was accepted"
    );
    expect(manager.create("camera-1", video_path.string(), error), error);
    expect(
        !manager.create("camera-1", video_path.string(), error),
        "duplicate stream ID was accepted"
    );

    std::atomic<uint64_t> event_count{0};
    std::atomic<uint64_t> terminal_count{0};
    std::atomic<bool> callback_stop_checked{false};
    expect(
        manager.subscribe(
            "camera-1",
            [&manager, &event_count, &terminal_count, &callback_stop_checked](
                const yolo::RealtimeFrameEvent& event
            ) {
                ++event_count;
                if (!event.terminal && !callback_stop_checked.exchange(true)) {
                    expect(!manager.stop("camera-1"), "worker callback attempted to join itself");
                }
                if (event.terminal) {
                    ++terminal_count;
                }
                return true;
            }
        ),
        "failed to subscribe to stream events"
    );

    yolo::RealtimeStreamSnapshot snapshot;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        expect(manager.get("camera-1", snapshot), "stream disappeared before completion");
        if (snapshot.status == "completed" || snapshot.status == "failed") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    expect(snapshot.status == "completed", "local stream did not complete");
    const double completed_age_ms = snapshot.latest_result_age_ms;
    const auto completed_at = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const double idle_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - completed_at
    ).count();
    expect(manager.get("camera-1", snapshot), "completed stream disappeared");
    expect(
        snapshot.latest_result_age_ms >= completed_age_ms + idle_ms,
        "result freshness froze after the last frame"
    );
    expect(snapshot.decoded_frame_count == 120, "decoded frame count mismatch");
    expect(snapshot.processed_frame_count == 120, "offline stream did not apply backpressure");
    for (size_t i = 0; i < snapshot.processing_diagnostics.size(); ++i) {
        const auto& timing = snapshot.processing_diagnostics[i];
        const bool poll = i == static_cast<size_t>(yolo::StreamProcessingStage::Poll);
        const bool mandatory = i == static_cast<size_t>(yolo::StreamProcessingStage::FrameWork)
            || i == static_cast<size_t>(yolo::StreamProcessingStage::Prepare)
            || i == static_cast<size_t>(yolo::StreamProcessingStage::Publish)
            || i == static_cast<size_t>(yolo::StreamProcessingStage::Gray);
        expect((mandatory ? timing.count == 120
                   : poll ? timing.count == 0 : timing.count <= 120)
                   && timing.sum_ms >= timing.max_ms && timing.max_ms >= 0.0,
               "offline processing timing coverage mismatch");
    }
    expect(snapshot.weak_flow_sampled_points <= snapshot.weak_flow_roi_count * 20
               && (snapshot.weak_flow_roi_count == 0
                   || snapshot.weak_flow_roi_pixels >= snapshot.weak_flow_roi_count * 16),
           "offline weak-flow load violates per-ROI bounds");
    expect(snapshot.dropped_frame_count == 0, "offline stream unexpectedly dropped frames");
    expect(snapshot.decoder_queue_drop_count == 0, "offline decoder reported queue drops");
    expect(
        snapshot.processor_coalesced_frame_count == 0,
        "offline processor coalesced frames"
    );
    expect(snapshot.stale_frame_drop_count == 0, "live deadline dropped an offline frame");
    expect(snapshot.skipped_inference_count == 0, "offline fixture unexpectedly skipped inference");
    expect(snapshot.inference_error_count == 0, "offline stream inference failed");
    expect(snapshot.max_queue_length <= 2, "per-stream frame queue exceeded its bound");
    expect(snapshot.latest_sequence > 0, "no stream events were published");
    expect(event_count.load() > 0, "subscriber received no events");
    expect(terminal_count.load() == 1, "subscriber did not receive one terminal event");
    expect(callback_stop_checked.load(), "worker self-stop guard was not exercised");
    const auto before_removal = manager.metrics();
    expect(before_removal.active_count == 0, "completed stream was counted as active");
    expect(before_removal.streams.size() == 1, "completed record released its admission slot early");
    expect(before_removal.totals.decoded_frame_count == 120, "live-record decoded total mismatch");
    expect(before_removal.totals.processed_frame_count == 120, "live-record processed total mismatch");
    expect(manager.stop("camera-1"), "completed stream could not be removed");
    expect(!manager.get("camera-1", snapshot), "removed stream is still visible");
    const auto after_removal = manager.metrics();
    expect(after_removal.active_count == 0 && after_removal.streams.empty(),
           "removed stream still occupied a slot or appeared active");
    expect(after_removal.totals.decoded_frame_count == 120, "decoded counter decreased on removal");
    expect(after_removal.totals.processed_frame_count == 120, "processed counter decreased on removal");
    expect(!manager.stop("camera-1"), "removed stream was stopped a second time");
    expect(manager.metrics().totals.processed_frame_count == 120, "repeat stop archived counters twice");

    checkWorkerFailureIsolation(manager, video_path, false);
    checkWorkerFailureIsolation(manager, video_path, true);
    checkEventReplay(manager);
    checkTerminalReplayDuringLogging(manager, false);
    checkTerminalReplayDuringLogging(manager, true);
    checkConcurrentMetricsAndRemoval(manager, video_path);
    checkHttpMetricsAndRecovery(
        high_engine, low_engine, high_scheduler, low_scheduler, config, video_path
    );

    std::error_code ignored;
    std::filesystem::remove(video_path, ignored);
    std::cout << "realtime_stream_manager_test passed\n";
    return 0;
}
