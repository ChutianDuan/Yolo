#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "stream/stream_metrics.h"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

yolo::RealtimeStreamSnapshot sample(
    const std::string& id,
    const std::string& status,
    uint64_t scale = 1
) {
    yolo::RealtimeStreamSnapshot stream;
    stream.stream_id = id;
    stream.status = status;
    stream.decoded_frame_count = 100 * scale;
    stream.processed_frame_count = 80 * scale;
    stream.dropped_frame_count = 20 * scale;
    stream.decoder_queue_drop_count = 10 * scale;
    stream.processor_coalesced_frame_count = 5 * scale;
    stream.stale_frame_drop_count = 5 * scale;
    stream.skipped_inference_count = 3 * scale;
    stream.inference_error_count = 2 * scale;
    stream.weak_flow_roi_count = 4 * scale;
    stream.weak_flow_roi_pixels = 256 * scale;
    stream.weak_flow_sampled_points = 32 * scale;
    for (size_t tier = 0; tier < stream.async_inference_diagnostics.size(); ++tier) {
        auto& timing = stream.async_inference_diagnostics[tier];
        timing.completed_count = 3 * scale;
        timing.applied_count = scale;
        timing.expired_count = scale;
        timing.evicted_count = scale;
        timing.stages[static_cast<size_t>(yolo::StreamInferenceStage::Infer)] =
            {scale, 100.0 * scale, 100.0};
    }
    for (auto& timing : stream.processing_diagnostics) {
        timing = {scale, 50.0 * scale, 50.0};
    }
    return stream;
}

void expectTotals(const yolo::RealtimeStreamCounters& totals, uint64_t scale) {
    expect(totals.decoded_frame_count == 100 * scale, "decoded total mismatch");
    expect(totals.processed_frame_count == 80 * scale, "processed total mismatch");
    expect(totals.dropped_frame_count == 20 * scale, "dropped total mismatch");
    expect(totals.decoder_queue_drop_count == 10 * scale, "decoder drop total mismatch");
    expect(totals.processor_coalesced_frame_count == 5 * scale, "coalesced total mismatch");
    expect(totals.stale_frame_drop_count == 5 * scale, "stale drop total mismatch");
    expect(totals.skipped_inference_count == 3 * scale, "skipped inference total mismatch");
    expect(totals.inference_error_count == 2 * scale, "inference error total mismatch");
    expect(totals.weak_flow_roi_count == 4 * scale
               && totals.weak_flow_roi_pixels == 256 * scale
               && totals.weak_flow_sampled_points == 32 * scale,
           "retired/live weak-flow load mismatch");
    for (const auto& timing : totals.processing_diagnostics) {
        expect(timing.count == scale && timing.sum_ms == 50.0 * scale
                   && timing.max_ms == (scale == 0 ? 0.0 : 50.0),
               "retired/live processing timings mismatch");
    }
    for (const auto& timing : totals.async_inference_diagnostics) {
        expect(timing.completed_count == 3 * scale && timing.applied_count == scale
                   && timing.expired_count == scale && timing.evicted_count == scale,
               "retired/live async outcome totals mismatch");
        const auto& stage = timing.stages[static_cast<size_t>(yolo::StreamInferenceStage::Infer)];
        expect(stage.count == scale && stage.sum_ms == 100.0 * scale
                   && stage.max_ms == (scale == 0 ? 0.0 : 100.0),
               "retired/live async stage totals mismatch");
    }
}

}  // namespace

int main() {
    const auto empty = yolo::summarizeStreamMetrics({}, {});
    expect(empty.active_count == 0 && empty.streams.empty(), "empty manager has live streams");
    expectTotals(empty.totals, 0);

    std::vector<yolo::RealtimeStreamSnapshot> states;
    for (const std::string status : {
             "starting", "connecting", "running", "reconnecting", "stopping",
             "completed", "stopped", "failed", "expired"}) {
        states.push_back(sample(status, status));
    }
    const auto all_states = yolo::summarizeStreamMetrics({}, states);
    expect(all_states.active_count == 5, "terminal records inflated active stream count");
    expect(all_states.streams.size() == 9, "terminal records disappeared from registered count");
    expectTotals(all_states.totals, 9);
    for (size_t i = 1; i < all_states.streams.size(); ++i) {
        expect(all_states.streams[i - 1].stream_id < all_states.streams[i].stream_id,
               "list order is no longer deterministic");
    }

    yolo::RealtimeStreamCounters retired;
    auto running = sample("same-id", "running");
    const auto completed = sample("completed", "completed", 2);
    const auto before = yolo::summarizeStreamMetrics(retired, {running, completed});
    expect(before.active_count == 1, "completed peer counted as active");
    expectTotals(before.totals, 3);

    // The manager moves a joined record from this vector into retired totals under one lock.
    yolo::addStreamCounters(retired, completed);
    const auto after_first_removal = yolo::summarizeStreamMetrics(retired, {running});
    expect(after_first_removal.active_count == 1 && after_first_removal.streams.size() == 1,
           "removal changed the surviving stream's active/registered state");
    expectTotals(after_first_removal.totals, 3);
    expectTotals(retired, 2);

    running.status = "stopping";
    const auto stopping = yolo::summarizeStreamMetrics(retired, {running});
    expect(stopping.active_count == 1 && stopping.streams.size() == 1,
           "joining stream released its admission slot early");
    expectTotals(stopping.totals, 3);
    running.status = "stopped";
    expect(yolo::summarizeStreamMetrics(retired, {running}).active_count == 0,
           "stopped record became active");

    yolo::addStreamCounters(retired, running);
    const auto no_records = yolo::summarizeStreamMetrics(retired, {});
    expect(no_records.active_count == 0 && no_records.streams.empty(),
           "retired totals kept a stream registration");
    expectTotals(no_records.totals, 3);
    const auto recreated = yolo::summarizeStreamMetrics(retired, {sample("same-id", "running", 4)});
    expect(recreated.active_count == 1 && recreated.streams.size() == 1,
           "same-ID new lifecycle inherited old active state");
    expectTotals(recreated.totals, 7);
    expectTotals(retired, 3);

    const uint64_t large_scale = uint64_t{1} << 32;
    yolo::RealtimeStreamCounters large_retired;
    yolo::addStreamCounters(large_retired, sample("large", "completed", large_scale));
    expectTotals(yolo::summarizeStreamMetrics(large_retired, {}).totals, large_scale);
    yolo::StreamInferenceStageTotal finite;
    finite.observe(-1.0);
    finite.observe(std::numeric_limits<double>::quiet_NaN());
    finite.observe(std::numeric_limits<double>::infinity());
    finite.observe(0.0);
    finite.observe(100.0);
    expect(finite.count == 2 && finite.sum_ms == 100.0 && finite.max_ms == 100.0,
           "non-finite/negative timing contaminated metrics");
    std::ostringstream text;
    appendStreamInferenceMetrics(text, recreated.totals.async_inference_diagnostics);
    appendStreamInferenceMetrics(text, recreated.streams.front().async_inference_diagnostics, "same-id");
    expect(text.str().find("yolo_stream_async_results_total{tier=\"high\",outcome=\"completed\"} 21")
               != std::string::npos,
           "global outcome metric missing");
    expect(text.str().find("yolo_stream_async_stage_by_stream_seconds_sum{tier=\"low\",stage=\"infer\",stream_id=\"same-id\"} 0.4")
               != std::string::npos,
           "tier/stream labels or milliseconds-to-seconds conversion changed");
    appendStreamProcessingMetrics(text, recreated.totals.processing_diagnostics);
    appendStreamProcessingMetrics(text, recreated.streams.front().processing_diagnostics, "same-id");
    expect(text.str().find("yolo_stream_processing_stage_seconds_count{stage=\"frame_work\"} 7")
               != std::string::npos, "global processing count missing");
    expect(text.str().find("yolo_stream_processing_stage_by_stream_seconds_sum{stage=\"prepare\",stream_id=\"same-id\"} 0.2")
               != std::string::npos, "processing seconds or labels changed");
    yolo::appendWeakFlowLoadMetrics(text, recreated.totals.weak_flow_roi_count,
                              recreated.totals.weak_flow_roi_pixels,
                              recreated.totals.weak_flow_sampled_points);
    yolo::appendWeakFlowLoadMetrics(text, recreated.streams.front().weak_flow_roi_count,
                              recreated.streams.front().weak_flow_roi_pixels,
                              recreated.streams.front().weak_flow_sampled_points, "same-id");
    expect(text.str().find("yolo_stream_weak_flow_rois_total 28") != std::string::npos
               && text.str().find("yolo_stream_weak_flow_roi_pixels_total 1792")
                   != std::string::npos
               && text.str().find("yolo_stream_weak_flow_sampled_points_total 224")
                   != std::string::npos, "global weak-flow load metrics missing");
    expect(text.str().find("yolo_stream_weak_flow_rois_by_stream_total{stream_id=\"same-id\"} 16")
               != std::string::npos, "per-stream weak-flow load metric missing");
    std::cout << "stream_metrics_test passed\n";
}
