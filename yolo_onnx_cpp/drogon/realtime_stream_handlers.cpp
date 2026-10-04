#include "drogon/realtime_stream_handlers.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon.h>

#include "drogon/inference_handlers.h"
#include "drogon/request_gate.h"
#include "drogon/request_utils.h"
#include "drogon/response_json.h"
#include "model/inference_scheduler.h"
#include "stream/realtime_stream_manager.h"
#include "stream/stream_metrics.h"
#include "drogon/video_job_executor.h"

namespace yolo::api {
namespace {

Json::Value streamSnapshotToJson(const RealtimeStreamSnapshot& snapshot) {
    Json::Value json;
    json["stream_id"] = snapshot.stream_id;
    json["source"] = snapshot.source;
    json["status"] = snapshot.status;
    if (!snapshot.last_error.empty()) {
        json["last_error"] = snapshot.last_error;
    }
    json["width"] = snapshot.width;
    json["height"] = snapshot.height;
    json["source_fps"] = snapshot.source_fps;
    json["decoded_frame_count"] = Json::UInt64(snapshot.counters.frames.decoded_frame_count);
    json["processed_frame_count"] = Json::UInt64(snapshot.counters.frames.processed_frame_count);
    json["detection_frame_count"] = Json::UInt64(snapshot.detections.detection_frame_count);
    json["high_res_detection_count"] =
        Json::UInt64(snapshot.detections.high_res_detection_count);
    json["roi_high_res_detection_count"] =
        Json::UInt64(snapshot.detections.roi_high_res_detection_count);
    json["low_res_detection_count"] =
        Json::UInt64(snapshot.detections.low_res_detection_count);
    json["dropped_frame_count"] = Json::UInt64(snapshot.counters.frames.dropped_frame_count);
    json["decoder_queue_drop_count"] =
        Json::UInt64(snapshot.counters.frames.decoder_queue_drop_count);
    json["processor_coalesced_frame_count"] =
        Json::UInt64(snapshot.counters.frames.processor_coalesced_frame_count);
    json["stale_frame_drop_count"] = Json::UInt64(snapshot.counters.frames.stale_frame_drop_count);
    json["skipped_inference_count"] = Json::UInt64(snapshot.counters.inference.skipped_inference_count);
    json["inference_error_count"] = Json::UInt64(snapshot.counters.inference.inference_error_count);
    json["consecutive_inference_error_count"] =
        Json::UInt64(snapshot.consecutive_inference_error_count);
    json["reconnect_count"] = Json::UInt64(snapshot.reconnect_count);
    json["queue_length"] = Json::UInt64(snapshot.queue.queue_length);
    json["max_queue_length"] = Json::UInt64(snapshot.queue.max_queue_length);
    json["latest_sequence"] = Json::UInt64(snapshot.latest.latest_sequence);
    json["latest_result_age_ms"] = snapshot.latest.latest_result_age_ms;
    return json;
}

drogon::HttpResponsePtr jsonResponse(
    Json::Value json,
    drogon::HttpStatusCode status = drogon::k200OK
) {
    auto response = drogon::HttpResponse::newHttpJsonResponse(std::move(json));
    response->setStatusCode(status);
    return response;
}

drogon::HttpResponsePtr streamNotFound(const std::string& stream_id) {
    return makeJsonResponse(
        404,
        "stream not found: " + stream_id,
        drogon::k404NotFound
    );
}

void appendSchedulerMetrics(
    std::ostringstream& output,
    const char* tier,
    const std::shared_ptr<InferenceScheduler>& scheduler
) {
    if (scheduler == nullptr) {
        return;
    }
    const InferenceSchedulerStats stats = scheduler->stats();
    output << "yolo_scheduler_submitted_total{tier=\"" << tier << "\"} "
           << stats.submitted_count << '\n';
    output << "yolo_scheduler_completed_total{tier=\"" << tier << "\"} "
           << stats.completed_count << '\n';
    output << "yolo_scheduler_replaced_total{tier=\"" << tier << "\"} "
           << stats.replaced_count << '\n';
    output << "yolo_scheduler_updated_total{tier=\"" << tier << "\"} "
           << stats.updated_count << '\n';
    output << "yolo_scheduler_stale_total{tier=\"" << tier << "\"} "
           << stats.stale_count << '\n';
    output << "yolo_scheduler_failed_total{tier=\"" << tier << "\"} "
           << stats.failed_count << '\n';
    output << "yolo_scheduler_cancelled_total{tier=\"" << tier << "\"} "
           << stats.cancelled_count << '\n';
    output << "yolo_scheduler_queue_depth{tier=\"" << tier << "\"} "
           << stats.queued_count << '\n';
    output << "yolo_scheduler_in_flight{tier=\"" << tier << "\"} "
           << stats.in_flight_count << '\n';
}

}  // namespace

void registerRealtimeStreamRoutes(
    const std::shared_ptr<RealtimeStreamManager>& stream_manager,
    const std::shared_ptr<InferenceScheduler>& high_res_scheduler,
    const std::shared_ptr<InferenceScheduler>& low_res_scheduler,
    const std::shared_ptr<VideoJobExecutor>& video_job_executor,
    const std::shared_ptr<RequestGate>& request_gate,
    std::vector<std::string> class_names
) {
    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr&, ResponseCallback&& callback) {
            Json::Value json;
            json["status"] = "ok";
            callback(jsonResponse(std::move(json)));
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/ready",
        [stream_manager](const drogon::HttpRequestPtr&, ResponseCallback&& callback) {
            Json::Value json;
            json["status"] = stream_manager != nullptr ? "ready" : "not_ready";
            callback(jsonResponse(
                std::move(json),
                stream_manager != nullptr
                    ? drogon::k200OK
                    : drogon::k503ServiceUnavailable
            ));
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/streams",
        [stream_manager](const drogon::HttpRequestPtr&, ResponseCallback&& callback) {
            Json::Value streams(Json::arrayValue);
            for (const auto& snapshot : stream_manager->list()) {
                streams.append(streamSnapshotToJson(snapshot));
            }
            Json::Value json;
            json["streams"] = std::move(streams);
            callback(jsonResponse(std::move(json)));
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/streams",
        [stream_manager](
            const drogon::HttpRequestPtr& request,
            ResponseCallback&& callback
        ) {
            const auto& json = request->getJsonObject();
            if (json == nullptr
                || !json->isObject()
                || !(*json)["stream_id"].isString()
                || !(*json)["source"].isString()) {
                callback(makeJsonResponse(
                    400,
                    "JSON body must contain string fields: stream_id and source",
                    drogon::k400BadRequest
                ));
                return;
            }

            const std::string stream_id = (*json)["stream_id"].asString();
            const std::string source = (*json)["source"].asString();
            std::string error;
            if (!stream_manager->create(stream_id, source, error)) {
                const bool conflict = error == "stream_id already exists"
                    || error == "maximum stream count reached";
                callback(makeJsonResponse(
                    conflict ? 409 : 400,
                    error,
                    conflict ? drogon::k409Conflict : drogon::k400BadRequest
                ));
                return;
            }

            RealtimeStreamSnapshot snapshot;
            (void)stream_manager->get(stream_id, snapshot);
            callback(jsonResponse(
                streamSnapshotToJson(snapshot),
                drogon::k201Created
            ));
        },
        {drogon::Post}
    );

    drogon::app().registerHandler(
        "/streams/{1}",
        [stream_manager](
            const drogon::HttpRequestPtr&,
            ResponseCallback&& callback,
            const std::string& stream_id
        ) {
            RealtimeStreamSnapshot snapshot;
            if (!stream_manager->get(stream_id, snapshot)) {
                callback(streamNotFound(stream_id));
                return;
            }
            callback(jsonResponse(streamSnapshotToJson(snapshot)));
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/streams/{1}",
        [stream_manager](
            const drogon::HttpRequestPtr&,
            ResponseCallback&& callback,
            const std::string& stream_id
        ) {
            if (!stream_manager->stop(stream_id)) {
                callback(streamNotFound(stream_id));
                return;
            }
            Json::Value json;
            json["stream_id"] = stream_id;
            json["status"] = "stopped";
            callback(jsonResponse(std::move(json)));
        },
        {drogon::Delete}
    );

    drogon::app().registerHandler(
        "/streams/{1}/events",
        [stream_manager, class_names](
            const drogon::HttpRequestPtr& request,
            ResponseCallback&& callback,
            const std::string& stream_id
        ) {
            RealtimeStreamSnapshot snapshot;
            if (!stream_manager->get(stream_id, snapshot)) {
                callback(streamNotFound(stream_id));
                return;
            }

            std::optional<uint64_t> last_event_id;
            std::string parse_error;
            if (!parseLastEventId(
                    request->getHeader("Last-Event-ID"),
                    last_event_id,
                    parse_error)) {
                callback(makeJsonResponse(
                    400, parse_error, drogon::k400BadRequest
                ));
                return;
            }

            auto response = drogon::HttpResponse::newAsyncStreamResponse(
                [stream_manager, stream_id, class_names, last_event_id](
                    drogon::ResponseStreamPtr stream
                ) {
                    auto holder = std::make_shared<drogon::ResponseStreamPtr>(
                        std::move(stream)
                    );
                    auto event_callback = [holder, class_names](
                        const RealtimeFrameEvent& event
                    ) {
                        if (*holder == nullptr) {
                            return false;
                        }
                        Json::StreamWriterBuilder writer;
                        writer["indentation"] = "";
                        const std::string data = Json::writeString(
                            writer,
                            realtimeFrameEventToJson(event, class_names)
                        );
                        const std::string event_name =
                            event.terminal ? "end" : "detection";
                        const std::string payload =
                            "id: " + std::to_string(event.sequence)
                            + "\nevent: " + event_name
                            + "\ndata: " + data + "\n\n";
                        const bool sent = (*holder)->send(payload);
                        if (!sent || event.terminal) {
                            (*holder)->close();
                            holder->reset();
                            return false;
                        }
                        return true;
                    };

                    RealtimeSubscribeResult subscription;
                    if (last_event_id.has_value()) {
                        subscription = stream_manager->subscribeAfter(
                            stream_id, *last_event_id, std::move(event_callback)
                        );
                    } else {
                        subscription.status = stream_manager->subscribe(
                            stream_id, std::move(event_callback)
                        )
                            ? RealtimeSubscribeStatus::Subscribed
                            : RealtimeSubscribeStatus::StreamUnavailable;
                    }

                    if (subscription.status != RealtimeSubscribeStatus::Subscribed
                        && *holder != nullptr) {
                        Json::Value error;
                        if (subscription.status == RealtimeSubscribeStatus::HistoryExpired) {
                            error["message"] = "event history expired";
                            error["earliest_available_sequence"] =
                                Json::UInt64(subscription.earliest_available_sequence);
                        } else if (subscription.status == RealtimeSubscribeStatus::CursorAhead) {
                            error["message"] = "Last-Event-ID is ahead of this stream";
                            error["latest_sequence"] =
                                Json::UInt64(subscription.latest_sequence);
                        } else {
                            error["message"] = "stream unavailable";
                        }
                        Json::StreamWriterBuilder writer;
                        writer["indentation"] = "";
                        (*holder)->send(
                            "event: error\ndata: "
                            + Json::writeString(writer, error) + "\n\n"
                        );
                        (*holder)->close();
                        holder->reset();
                    }
                },
                true
            );
            response->setContentTypeString("text/event-stream");
            response->addHeader("Cache-Control", "no-cache");
            response->addHeader("X-Accel-Buffering", "no");
            callback(response);
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/metrics",
        [stream_manager, high_res_scheduler, low_res_scheduler,
         video_job_executor, request_gate](
            const drogon::HttpRequestPtr&,
            ResponseCallback&& callback
        ) {
            const auto snapshot = stream_manager->metrics();
            const auto& streams = snapshot.streams;
            const auto& totals = snapshot.totals;

            std::ostringstream metrics;
            metrics << "# TYPE yolo_streams_active gauge\n";
            metrics << "yolo_streams_active " << snapshot.active_count << '\n';
            metrics << "# TYPE yolo_streams_registered gauge\n";
            metrics << "yolo_streams_registered " << streams.size() << '\n';
            metrics << "# TYPE yolo_stream_decoded_frames_total counter\n";
            metrics << "yolo_stream_decoded_frames_total " << totals.frames.decoded_frame_count << '\n';
            metrics << "# TYPE yolo_stream_processed_frames_total counter\n";
            metrics << "yolo_stream_processed_frames_total " << totals.frames.processed_frame_count << '\n';
            metrics << "# TYPE yolo_stream_dropped_frames_total counter\n";
            metrics << "yolo_stream_dropped_frames_total " << totals.frames.dropped_frame_count << '\n';
            metrics << "# TYPE yolo_stream_decoder_queue_drops_total counter\n";
            metrics << "yolo_stream_decoder_queue_drops_total "
                    << totals.frames.decoder_queue_drop_count << '\n';
            metrics << "# TYPE yolo_stream_processor_coalesced_frames_total counter\n";
            metrics << "yolo_stream_processor_coalesced_frames_total "
                    << totals.frames.processor_coalesced_frame_count << '\n';
            metrics << "# TYPE yolo_stream_inference_errors_total counter\n";
            metrics << "yolo_stream_inference_errors_total "
                    << totals.inference.inference_error_count << '\n';
            metrics << "# TYPE yolo_stream_stale_frame_drops_total counter\n";
            metrics << "yolo_stream_stale_frame_drops_total "
                    << totals.frames.stale_frame_drop_count << '\n';
            metrics << "# TYPE yolo_stream_skipped_inferences_total counter\n";
            metrics << "yolo_stream_skipped_inferences_total "
                    << totals.inference.skipped_inference_count << '\n';
            appendStreamInferenceMetrics(metrics, totals.async_inference_diagnostics);
            appendStreamProcessingMetrics(metrics, totals.processing_diagnostics);
            appendWeakFlowLoadMetrics(metrics, totals.weak_flow.weak_flow_roi_count,
                                      totals.weak_flow.weak_flow_roi_pixels,
                                      totals.weak_flow.weak_flow_sampled_points);
            for (const auto& stream : streams) {
                appendStreamProcessingMetrics(metrics, stream.counters.processing_diagnostics, stream.stream_id);
                appendWeakFlowLoadMetrics(metrics, stream.counters.weak_flow.weak_flow_roi_count,
                                          stream.counters.weak_flow.weak_flow_roi_pixels,
                                          stream.counters.weak_flow.weak_flow_sampled_points, stream.stream_id);
                appendStreamInferenceMetrics(metrics, stream.counters.async_inference_diagnostics,
                                             stream.stream_id);
                const std::string labels =
                    "{stream_id=\"" + stream.stream_id + "\"}";
                metrics << "yolo_stream_processed_frames_by_stream_total"
                        << labels << ' ' << stream.counters.frames.processed_frame_count << '\n';
                metrics << "yolo_stream_low_detections_by_stream_total"
                        << labels << ' ' << stream.detections.low_res_detection_count << '\n';
                metrics << "yolo_stream_high_detections_by_stream_total"
                        << labels << ' ' << stream.detections.high_res_detection_count << '\n';
                metrics << "yolo_stream_roi_high_detections_by_stream_total"
                        << labels << ' ' << stream.detections.roi_high_res_detection_count << '\n';
                metrics << "yolo_stream_dropped_frames_by_stream_total"
                        << labels << ' ' << stream.counters.frames.dropped_frame_count << '\n';
                metrics << "yolo_stream_stale_frame_drops_by_stream_total"
                        << labels << ' ' << stream.counters.frames.stale_frame_drop_count << '\n';
                metrics << "yolo_stream_skipped_inferences_by_stream_total"
                        << labels << ' ' << stream.counters.inference.skipped_inference_count << '\n';
                metrics << "yolo_stream_inference_errors_by_stream_total"
                        << labels << ' ' << stream.counters.inference.inference_error_count << '\n';
                metrics << "yolo_stream_queue_depth_by_stream"
                        << labels << ' ' << stream.queue.queue_length << '\n';
                metrics << "yolo_stream_result_age_ms_by_stream"
                        << labels << ' ' << stream.latest.latest_result_age_ms << '\n';
            }
            appendSchedulerMetrics(metrics, "high", high_res_scheduler);
            appendSchedulerMetrics(metrics, "low", low_res_scheduler);
            if (request_gate != nullptr) {
                const auto stats = request_gate->stats();
                metrics << "yolo_api_authentication_enabled "
                        << (request_gate->authenticationEnabled() ? 1 : 0) << '\n';
                metrics << "yolo_api_rate_limit_enabled "
                        << (request_gate->rateLimitEnabled() ? 1 : 0) << '\n';
                metrics << "yolo_api_requests_allowed_total "
                        << stats.allowed_count << '\n';
                metrics << "yolo_api_requests_unauthorized_total "
                        << stats.unauthorized_count << '\n';
                metrics << "yolo_api_requests_rate_limited_total "
                        << stats.rate_limited_count << '\n';
                metrics << "yolo_api_rate_limit_clients "
                        << stats.tracked_client_count << '\n';
            }
            if (video_job_executor != nullptr) {
                const auto stats = video_job_executor->stats();
                metrics << "yolo_video_jobs_submitted_total " << stats.submitted_count << '\n';
                metrics << "yolo_video_jobs_completed_total " << stats.completed_count << '\n';
                metrics << "yolo_video_jobs_rejected_total " << stats.rejected_count << '\n';
                metrics << "yolo_video_jobs_queued " << stats.queued_count << '\n';
                metrics << "yolo_video_jobs_in_flight " << stats.in_flight_count << '\n';
            }

            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::k200OK);
            response->setContentTypeString("text/plain; version=0.0.4");
            response->setBody(metrics.str());
            callback(response);
        },
        {drogon::Get}
    );
}

}  // namespace yolo::api
