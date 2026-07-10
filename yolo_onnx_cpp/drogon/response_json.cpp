#include "response_json.h"

#include <algorithm>
#include <cstddef>

namespace yolo {
namespace {

void setDetectionJsonFields(
    Json::Value& item,
    const Detection& detection,
    const std::vector<std::string>& class_names
) {
    item["class_id"] = detection.class_id;
    if (detection.class_id >= 0
        && static_cast<size_t>(detection.class_id) < class_names.size()) {
        item["class_name"] = class_names[static_cast<size_t>(detection.class_id)];
    }
    item["score"] = detection.score;

    Json::Value box;
    box["x1"] = detection.x1;
    box["y1"] = detection.y1;
    box["x2"] = detection.x2;
    box["y2"] = detection.y2;
    item["box"] = box;
}

Json::Value videoFramesToJson(
    const std::vector<VideoFrameTracks>& frames,
    const std::vector<std::string>& class_names,
    size_t begin,
    size_t end
) {
    Json::Value frames_json(Json::arrayValue);

    begin = std::min(begin, frames.size());
    end = std::min(std::max(begin, end), frames.size());

    for (size_t index = begin; index < end; ++index) {
        const auto& frame = frames[index];
        Json::Value frame_json;
        frame_json["frame_index"] = Json::Int64(frame.frame_index);
        frame_json["timestamp_ms"] = frame.timestamp_ms;
        frame_json["is_detection_frame"] = frame.is_detection_frame;
        if (frame.corrected_from_frame_index >= 0) {
            frame_json["corrected_from_frame_index"] =
                Json::Int64(frame.corrected_from_frame_index);
            frame_json["correction_latency_frames"] =
                Json::Int64(frame.correction_latency_frames);
        }
        frame_json["tracks_source"] = frame.tracks_source;
        frame_json["tracks"] = tracksToJson(frame.tracks, class_names);
        frames_json.append(frame_json);
    }

    return frames_json;
}

size_t frameWindowEnd(size_t frame_count, const VideoFrameJsonOptions& options) {
    const size_t begin = std::min(options.frame_offset, frame_count);
    if (!options.include_frames) {
        return begin;
    }
    if (options.frame_limit == 0) {
        return frame_count;
    }
    const size_t remaining = frame_count - begin;
    return begin + std::min(remaining, options.frame_limit);
}

Json::Value percentilesToJson(const LatencyPercentiles& percentiles) {
    Json::Value json;
    json["p50"] = percentiles.p50;
    json["p95"] = percentiles.p95;
    json["p99"] = percentiles.p99;
    return json;
}

Json::Value latencyPercentilesToJson(const PerformanceMetrics& metrics) {
    Json::Value json;
    json["decode_ms"] = percentilesToJson(metrics.decode_percentiles_ms);
    json["preprocess_ms"] = percentilesToJson(metrics.preprocess_percentiles_ms);
    json["infer_ms"] = percentilesToJson(metrics.infer_percentiles_ms);
    json["postprocess_ms"] = percentilesToJson(metrics.postprocess_percentiles_ms);
    json["tracker_ms"] = percentilesToJson(metrics.tracker_percentiles_ms);
    json["queue_wait_ms"] = percentilesToJson(metrics.queue_wait_percentiles_ms);
    json["end_to_end_ms"] = percentilesToJson(metrics.end_to_end_percentiles_ms);
    return json;
}

Json::Value performanceTimingToJson(const PerformanceMetrics& metrics) {
    Json::Value timing;
    timing["decode_ms"] = metrics.decode_ms;
    timing["preprocess_ms"] = metrics.preprocess_ms;
    timing["infer_ms"] = metrics.infer_ms;
    timing["postprocess_ms"] = metrics.postprocess_ms;
    timing["tracker_ms"] = metrics.tracker_ms;
    timing["queue_wait_ms"] = metrics.queue_wait_ms;
    timing["end_to_end_ms"] = metrics.end_to_end_ms;
    return timing;
}

Json::Value performanceMetricsToJson(const PerformanceMetrics& metrics) {
    Json::Value json = performanceTimingToJson(metrics);
    json["latency_percentiles_ms"] = latencyPercentilesToJson(metrics);
    json["average_fps"] = metrics.average_fps;
    json["cpu_utilization_percent"] = metrics.cpu_utilization_percent;
    json["rss_memory_mb"] = metrics.rss_memory_mb;
    json["queue_length"] = Json::UInt64(metrics.queue_length);
    json["max_queue_length"] = Json::UInt64(metrics.max_queue_length);
    json["dropped_frame_count"] = Json::Int64(metrics.dropped_frame_count);
    return json;
}

Json::Value highLowDiagnosticsToJson(const HighLowDiagnostics& diagnostics) {
    Json::Value json;
    json["stable_track_count"] = Json::Int64(diagnostics.stable_track_count);
    json["provisional_track_count"] = Json::Int64(diagnostics.provisional_track_count);
    json["max_stable_track_count"] = Json::Int64(diagnostics.max_stable_track_count);
    json["max_provisional_track_count"] =
        Json::Int64(diagnostics.max_provisional_track_count);
    json["provisional_created_count"] =
        Json::Int64(diagnostics.provisional_created_count);
    json["provisional_promoted_count"] =
        Json::Int64(diagnostics.provisional_promoted_count);
    json["provisional_expired_count"] =
        Json::Int64(diagnostics.provisional_expired_count);
    json["provisional_deduplicated_count"] =
        Json::Int64(diagnostics.provisional_deduplicated_count);
    json["stable_stable_duplicate_count"] =
        Json::Int64(diagnostics.stable_stable_duplicate_count);
    json["stable_provisional_duplicate_count"] =
        Json::Int64(diagnostics.stable_provisional_duplicate_count);
    json["provisional_provisional_duplicate_count"] =
        Json::Int64(diagnostics.provisional_provisional_duplicate_count);
    json["output_suppressed_duplicate_count"] =
        Json::Int64(diagnostics.output_suppressed_duplicate_count);
    json["low_res_geometry_rejection_count"] =
        Json::Int64(diagnostics.low_res_geometry_rejection_count);
    json["low_res_class_conflict_count"] =
        Json::Int64(diagnostics.low_res_class_conflict_count);
    json["flow_track_count"] = Json::Int64(diagnostics.flow_track_count);
    json["flow_rejected_track_count"] =
        Json::Int64(diagnostics.flow_rejected_track_count);
    json["flow_low_point_rejection_count"] =
        Json::Int64(diagnostics.flow_low_point_rejection_count);
    json["flow_invalid_ratio_rejection_count"] =
        Json::Int64(diagnostics.flow_invalid_ratio_rejection_count);
    json["flow_forward_backward_rejection_count"] =
        Json::Int64(diagnostics.flow_forward_backward_rejection_count);
    json["flow_motion_dispersion_rejection_count"] =
        Json::Int64(diagnostics.flow_motion_dispersion_rejection_count);
    json["flow_motion_jump_rejection_count"] =
        Json::Int64(diagnostics.flow_motion_jump_rejection_count);
    json["flow_boundary_rejection_count"] =
        Json::Int64(diagnostics.flow_boundary_rejection_count);
    json["direct_flow_update_count"] =
        Json::Int64(diagnostics.direct_flow_update_count);
    json["global_flow_update_count"] =
        Json::Int64(diagnostics.global_flow_update_count);
    json["flow_age_output_suppression_count"] =
        Json::Int64(diagnostics.flow_age_output_suppression_count);
    json["flow_age_expired_count"] =
        Json::Int64(diagnostics.flow_age_expired_count);
    json["exiting_track_suppression_count"] =
        Json::Int64(diagnostics.exiting_track_suppression_count);
    json["urgent_low_res_detection_count"] =
        Json::Int64(diagnostics.urgent_low_res_detection_count);
    json["urgent_high_res_detection_count"] =
        Json::Int64(diagnostics.urgent_high_res_detection_count);
    json["urgent_flow_quality_count"] =
        Json::Int64(diagnostics.urgent_flow_quality_count);
    json["urgent_track_change_count"] =
        Json::Int64(diagnostics.urgent_track_change_count);
    json["urgent_duplicate_count"] =
        Json::Int64(diagnostics.urgent_duplicate_count);
    json["urgent_flow_age_count"] =
        Json::Int64(diagnostics.urgent_flow_age_count);
    json["urgent_geometry_count"] =
        Json::Int64(diagnostics.urgent_geometry_count);
    json["urgent_class_conflict_count"] =
        Json::Int64(diagnostics.urgent_class_conflict_count);
    return json;
}

Json::Value stageTimingToJson(const VideoInferResult& result) {
    const double preprocess_ms = result.preprocess_ms;
    const double decode_ms = result.decode_ms;
    const double infer_ms = result.infer_ms;
    const double model_postprocess_ms = result.postprocess_ms;
    const double queue_wait_ms = result.queue_wait_ms;
    const double tracker_ms = result.tracker_ms;
    const double flow_ms = result.optical_flow_ms;
    const double legacy_postprocess_ms =
        result.model_postprocess_ms + result.tracking_postprocess_ms;
    const double profiled_ms = preprocess_ms
        + infer_ms
        + decode_ms
        + model_postprocess_ms
        + tracker_ms
        + queue_wait_ms
        + flow_ms;
    const double other_ms = result.total_elapsed_ms > profiled_ms
        ? result.total_elapsed_ms - profiled_ms
        : 0.0;

    Json::Value timing;
    timing["total_elapsed_ms"] = result.total_elapsed_ms;
    timing["end_to_end_ms"] = result.end_to_end_ms;
    timing["preprocess_ms"] = preprocess_ms;
    timing["infer_ms"] = infer_ms;
    timing["onnx_inference_ms"] = result.model_inference_ms;
    timing["decode_ms"] = decode_ms;
    timing["postprocess_ms"] = model_postprocess_ms;
    timing["tracker_ms"] = tracker_ms;
    timing["queue_wait_ms"] = queue_wait_ms;
    timing["optical_flow_ms"] = flow_ms;
    timing["onnx_decode_nms_ms"] = result.model_postprocess_ms;
    timing["legacy_postprocess_ms"] = legacy_postprocess_ms;
    timing["tracking_postprocess_ms"] = result.tracking_postprocess_ms;
    timing["other_ms"] = other_ms;
    timing["profiled_stage_ms"] = profiled_ms;
    return timing;
}

Json::Value stageRatioToJson(const VideoInferResult& result) {
    const double preprocess_ms = result.preprocess_ms;
    const double infer_ms = result.infer_ms;
    const double decode_ms = result.decode_ms;
    const double model_postprocess_ms = result.postprocess_ms;
    const double queue_wait_ms = result.queue_wait_ms;
    const double tracker_ms = result.tracker_ms;
    const double flow_ms = result.optical_flow_ms;
    const double profiled_ms = preprocess_ms
        + infer_ms
        + decode_ms
        + model_postprocess_ms
        + tracker_ms
        + queue_wait_ms
        + flow_ms;

    Json::Value ratio;
    ratio["preprocess"] = profiled_ms > 0.0 ? preprocess_ms / profiled_ms : 0.0;
    ratio["onnx_inference"] = profiled_ms > 0.0 ? infer_ms / profiled_ms : 0.0;
    ratio["decode"] = profiled_ms > 0.0 ? decode_ms / profiled_ms : 0.0;
    ratio["postprocess"] = profiled_ms > 0.0
        ? model_postprocess_ms / profiled_ms
        : 0.0;
    ratio["tracker"] = profiled_ms > 0.0 ? tracker_ms / profiled_ms : 0.0;
    ratio["queue_wait"] = profiled_ms > 0.0 ? queue_wait_ms / profiled_ms : 0.0;
    ratio["optical_flow"] = profiled_ms > 0.0 ? flow_ms / profiled_ms : 0.0;
    return ratio;
}

}  // namespace

drogon::HttpResponsePtr makeJsonResponse(
    int code,
    const std::string& message,
    drogon::HttpStatusCode status
) {
    Json::Value ret;
    ret["code"] = code;
    ret["message"] = message;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(status);
    return resp;
}

Json::Value shapesToJson(const std::vector<std::vector<int64_t>>& shapes) {
    Json::Value shapes_json(Json::arrayValue);

    for (const auto& shape : shapes) {
        Json::Value one_shape(Json::arrayValue);
        for (auto dim : shape) {
            one_shape.append(Json::Int64(dim));
        }
        shapes_json.append(one_shape);
    }

    return shapes_json;
}

Json::Value detectionsToJson(
    const std::vector<Detection>& detections,
    const std::vector<std::string>& class_names
) {
    Json::Value detections_json(Json::arrayValue);

    for (const auto& detection : detections) {
        Json::Value item;
        setDetectionJsonFields(item, detection, class_names);
        detections_json.append(item);
    }

    return detections_json;
}

Json::Value tracksToJson(
    const std::vector<TrackedDetection>& tracks,
    const std::vector<std::string>& class_names
) {
    Json::Value tracks_json(Json::arrayValue);

    for (const auto& track : tracks) {
        Json::Value item;
        item["track_id"] = track.track_id;
        setDetectionJsonFields(item, track.detection, class_names);
        tracks_json.append(item);
    }

    return tracks_json;
}

Json::Value inferResultToJson(
    const InferResult& result,
    const std::vector<std::string>& class_names
) {
    Json::Value ret;
    ret["code"] = 0;
    ret["message"] = "success";
    ret["output_shapes"] = shapesToJson(result.output_shapes);
    ret["detections"] = detectionsToJson(result.detections, class_names);
    ret["timing_ms"] = performanceTimingToJson(result.metrics);
    ret["latency_percentiles_ms"] = latencyPercentilesToJson(result.metrics);
    ret["metrics"] = performanceMetricsToJson(result.metrics);
    return ret;
}

Json::Value videoInferResultToJson(
    const VideoInferResult& result,
    const std::vector<std::string>& class_names
) {
    return videoInferResultToJson(result, class_names, VideoFrameJsonOptions{});
}

Json::Value videoInferResultToJson(
    const VideoInferResult& result,
    const std::vector<std::string>& class_names,
    const VideoFrameJsonOptions& options
) {
    const size_t total_frames = result.frames.size();
    const size_t frame_begin = std::min(options.frame_offset, total_frames);
    const size_t frame_end = frameWindowEnd(total_frames, options);
    const size_t frames_returned = options.include_frames
        ? frame_end - frame_begin
        : 0;

    Json::Value ret;
    ret["code"] = 0;
    ret["message"] = "success";
    ret["tracking_status"] = result.tracking_status;
    ret["fps"] = result.fps;
    ret["source_fps"] = result.source_fps;
    ret["target_detect_fps"] = result.target_detect_fps;
    ret["effective_detect_fps"] = result.effective_detect_fps;
    ret["frame_stride"] = result.frame_stride;
    ret["stride_mode"] = result.stride_mode;
    ret["onnx_async"] = result.model_async;
    ret["base_frame_stride"] = result.base_frame_stride;
    ret["min_frame_stride_used"] = result.min_frame_stride_used;
    ret["max_frame_stride_used"] = result.max_frame_stride_used;
    ret["final_frame_stride"] = result.final_frame_stride;
    ret["width"] = result.width;
    ret["height"] = result.height;
    ret["frame_count"] = Json::Int64(result.frame_count);
    ret["source_frame_count"] = Json::Int64(result.source_frame_count);
    ret["processed_frame_count"] = Json::Int64(result.processed_frame_count);
    ret["display_frame_count"] = Json::Int64(result.display_frame_count);
    ret["detected_frame_count"] = Json::Int64(result.detected_frame_count);
    ret["async_infer_request_count"] = Json::Int64(result.async_infer_request_count);
    ret["async_correction_count"] = Json::Int64(result.async_correction_count);
    ret["async_corrected_frame_count"] =
        Json::Int64(result.async_corrected_frame_count);
    ret["forced_detection_count"] = Json::Int64(result.forced_detection_count);
    ret["scheduled_detection_count"] = Json::Int64(result.scheduled_detection_count);
    ret["skipped_detection_count"] = Json::Int64(result.skipped_detection_count);
    ret["weak_tracked_frame_count"] = Json::Int64(result.weak_tracked_frame_count);
    ret["interpolated_frame_count"] = Json::Int64(result.interpolated_frame_count);
    ret["empty_frame_count"] = Json::Int64(result.empty_frame_count);
    ret["average_fps"] = result.metrics.average_fps;
    ret["cpu_utilization_percent"] = result.metrics.cpu_utilization_percent;
    ret["rss_memory_mb"] = result.metrics.rss_memory_mb;
    ret["queue_length"] = Json::UInt64(result.metrics.queue_length);
    ret["max_queue_length"] = Json::UInt64(result.metrics.max_queue_length);
    ret["dropped_frame_count"] = Json::Int64(result.metrics.dropped_frame_count);
    ret["timing_ms"] = stageTimingToJson(result);
    ret["timing_ratio"] = stageRatioToJson(result);
    ret["latency_percentiles_ms"] = latencyPercentilesToJson(result.metrics);
    ret["metrics"] = performanceMetricsToJson(result.metrics);
    ret["high_low_diagnostics"] = highLowDiagnosticsToJson(
        result.high_low_diagnostics
    );
    ret["output_shapes"] = shapesToJson(result.output_shapes);
    ret["frames_returned"] = Json::UInt64(frames_returned);
    ret["frame_offset"] = Json::UInt64(options.frame_offset);
    ret["frame_limit"] = options.frame_limit > 0
        ? Json::Value(Json::UInt64(options.frame_limit))
        : Json::Value(Json::nullValue);
    ret["has_more_frames"] = options.include_frames
        ? frame_end < total_frames
        : total_frames > 0;
    ret["frames"] = options.include_frames
        ? videoFramesToJson(result.frames, class_names, frame_begin, frame_end)
        : Json::Value(Json::arrayValue);
    return ret;
}

}  // namespace yolo
