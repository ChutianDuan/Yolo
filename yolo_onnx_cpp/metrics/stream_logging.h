#pragma once

#include <cstdint>
#include <string>

#include "model/inference_types.h"
#include "stream/realtime_stream_manager.h"

namespace yolo {

enum class StreamLogEvent {
    Created,
    StartFailed,
    SourceConnected,
    SourceUnavailable,
    SourceDisconnected,
    InferenceFailed,
    DecoderFailed,
    ProcessorFailed,
    CircuitOpened,
    LifetimeExpired,
    StopRequested,
    Finished,
    Removed,
};

// Log the first failure and powers of two within one consecutive failure streak.
bool shouldLogRepeatedFailure(uint64_t count);

std::string formatStreamLog(
    StreamLogEvent event,
    const RealtimeStreamSnapshot& snapshot,
    const InferenceContext& inference = {},
    const char* model_tier = nullptr
);

void logStreamEvent(
    StreamLogEvent event,
    const RealtimeStreamSnapshot& snapshot,
    const InferenceContext& inference = {},
    const char* model_tier = nullptr
) noexcept;

}  // namespace yolo
