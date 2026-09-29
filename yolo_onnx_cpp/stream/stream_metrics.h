#pragma once

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "stream/realtime_stream_manager.h"

namespace yolo {

inline bool isTerminalRealtimeStatus(std::string_view status) {
    return status == "completed" || status == "stopped"
        || status == "failed" || status == "expired";
}

inline void addStreamCounters(
    RealtimeStreamCounters& totals,
    const RealtimeStreamSnapshot& stream
) {
    totals.decoded_frame_count += stream.decoded_frame_count;
    totals.processed_frame_count += stream.processed_frame_count;
    totals.dropped_frame_count += stream.dropped_frame_count;
    totals.decoder_queue_drop_count += stream.decoder_queue_drop_count;
    totals.processor_coalesced_frame_count += stream.processor_coalesced_frame_count;
    totals.stale_frame_drop_count += stream.stale_frame_drop_count;
    totals.skipped_inference_count += stream.skipped_inference_count;
    totals.inference_error_count += stream.inference_error_count;
    totals.weak_flow_roi_count += stream.weak_flow_roi_count;
    totals.weak_flow_roi_pixels += stream.weak_flow_roi_pixels;
    totals.weak_flow_sampled_points += stream.weak_flow_sampled_points;
    for (size_t i = 0; i < totals.processing_diagnostics.size(); ++i) {
        totals.processing_diagnostics[i].merge(stream.processing_diagnostics[i]);
    }
    for (size_t i = 0; i < totals.async_inference_diagnostics.size(); ++i) {
        totals.async_inference_diagnostics[i].merge(stream.async_inference_diagnostics[i]);
    }
}

// IDs are validated by the manager; stage/tier/outcome labels are fixed allowlists.
// Global sums survive removal; per-stream metrics follow the registered lifecycle.
inline void appendStreamInferenceMetrics(
    std::ostream& output,
    const StreamInferenceDiagnosticsByTier& diagnostics,
    const std::string& stream_id = {}
) {
    const std::string scope = stream_id.empty() ? "" : "_by_stream";
    const std::string stream_label = stream_id.empty()
        ? "" : ",stream_id=\"" + stream_id + "\"";
    for (size_t tier = 0; tier < diagnostics.size(); ++tier) {
        const auto& item = diagnostics[tier];
        const std::string labels = "{tier=\"" + std::string(tier == 0 ? "high" : "low") + "\"";
        const auto outcome = [&](const char* name, uint64_t count) {
            output << "yolo_stream_async_results" << scope << "_total" << labels
                   << ",outcome=\"" << name << "\"" << stream_label << "} " << count << '\n';
        };
        outcome("completed", item.completed_count);
        outcome("applied", item.applied_count);
        outcome("expired", item.expired_count);
        outcome("evicted", item.evicted_count);
        for (size_t stage = 0; stage < item.stages.size(); ++stage) {
            const auto& sample = item.stages[stage];
            const std::string stage_labels = labels + ",stage=\""
                + kStreamInferenceStageNames[stage] + "\"" + stream_label + "} ";
            output << "yolo_stream_async_stage" << scope << "_seconds_count"
                   << stage_labels << sample.count << '\n';
            output << "yolo_stream_async_stage" << scope << "_seconds_sum"
                   << stage_labels << sample.sum_ms / 1000.0 << '\n';
            output << "yolo_stream_async_stage" << scope << "_seconds_max"
                   << stage_labels << sample.max_ms / 1000.0 << '\n';
        }
    }
}

inline void appendStreamProcessingMetrics(
    std::ostream& output,
    const StreamProcessingDiagnostics& diagnostics,
    const std::string& stream_id = {}
) {
    const std::string scope = stream_id.empty() ? "" : "_by_stream";
    const std::string stream_label = stream_id.empty()
        ? "" : ",stream_id=\"" + stream_id + "\"";
    for (size_t stage = 0; stage < diagnostics.size(); ++stage) {
        const auto& sample = diagnostics[stage];
        const std::string labels = "{stage=\""
            + std::string(kStreamProcessingStageNames[stage]) + "\"" + stream_label + "} ";
        output << "yolo_stream_processing_stage" << scope << "_seconds_count"
               << labels << sample.count << '\n';
        output << "yolo_stream_processing_stage" << scope << "_seconds_sum"
               << labels << sample.sum_ms / 1000.0 << '\n';
        output << "yolo_stream_processing_stage" << scope << "_seconds_max"
               << labels << sample.max_ms / 1000.0 << '\n';
    }
}

inline void appendWeakFlowLoadMetrics(
    std::ostream& output,
    uint64_t roi_count,
    uint64_t roi_pixels,
    uint64_t sampled_points,
    const std::string& stream_id = {}
) {
    const std::string scope = stream_id.empty() ? "" : "_by_stream";
    const std::string labels = stream_id.empty()
        ? "" : "{stream_id=\"" + stream_id + "\"}";
    output << "yolo_stream_weak_flow_rois" << scope << "_total"
           << labels << ' ' << roi_count << '\n';
    output << "yolo_stream_weak_flow_roi_pixels" << scope << "_total"
           << labels << ' ' << roi_pixels << '\n';
    output << "yolo_stream_weak_flow_sampled_points" << scope << "_total"
           << labels << ' ' << sampled_points << '\n';
}

inline RealtimeStreamMetrics summarizeStreamMetrics(
    RealtimeStreamCounters retired,
    std::vector<RealtimeStreamSnapshot> streams
) {
    RealtimeStreamMetrics metrics;
    metrics.totals = retired;
    metrics.streams = std::move(streams);
    for (const auto& stream : metrics.streams) {
        if (!isTerminalRealtimeStatus(stream.status)) {
            ++metrics.active_count;
        }
        addStreamCounters(metrics.totals, stream);
    }
    std::sort(metrics.streams.begin(), metrics.streams.end(), [](const auto& a, const auto& b) {
        return a.stream_id < b.stream_id;
    });
    return metrics;
}

}  // namespace yolo
