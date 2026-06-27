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

Json::Value stageTimingToJson(const VideoInferResult& result) {
    const double onnx_ms = result.onnx_inference_ms;
    const double flow_ms = result.optical_flow_ms;
    const double postprocess_ms =
        result.onnx_postprocess_ms + result.tracking_postprocess_ms;
    const double profiled_ms = onnx_ms + flow_ms + postprocess_ms;
    const double other_ms = result.total_elapsed_ms > profiled_ms
        ? result.total_elapsed_ms - profiled_ms
        : 0.0;

    Json::Value timing;
    timing["total_elapsed_ms"] = result.total_elapsed_ms;
    timing["onnx_inference_ms"] = onnx_ms;
    timing["optical_flow_ms"] = flow_ms;
    timing["postprocess_ms"] = postprocess_ms;
    timing["onnx_decode_nms_ms"] = result.onnx_postprocess_ms;
    timing["tracking_postprocess_ms"] = result.tracking_postprocess_ms;
    timing["other_ms"] = other_ms;
    timing["profiled_stage_ms"] = profiled_ms;
    return timing;
}

Json::Value stageRatioToJson(const VideoInferResult& result) {
    const double onnx_ms = result.onnx_inference_ms;
    const double flow_ms = result.optical_flow_ms;
    const double postprocess_ms =
        result.onnx_postprocess_ms + result.tracking_postprocess_ms;
    const double profiled_ms = onnx_ms + flow_ms + postprocess_ms;

    Json::Value ratio;
    ratio["onnx_inference"] = profiled_ms > 0.0 ? onnx_ms / profiled_ms : 0.0;
    ratio["optical_flow"] = profiled_ms > 0.0 ? flow_ms / profiled_ms : 0.0;
    ratio["postprocess"] = profiled_ms > 0.0 ? postprocess_ms / profiled_ms : 0.0;
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
    ret["onnx_async"] = result.onnx_async;
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
    ret["timing_ms"] = stageTimingToJson(result);
    ret["timing_ratio"] = stageRatioToJson(result);
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
