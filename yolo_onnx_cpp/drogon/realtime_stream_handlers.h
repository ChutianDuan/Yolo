#pragma once

#include <memory>
#include <string>
#include <vector>

namespace yolo {

class InferenceScheduler;
class RealtimeStreamManager;
namespace api {
class RequestGate;
class VideoJobExecutor;
}

namespace api {

void registerRealtimeStreamRoutes(
    const std::shared_ptr<RealtimeStreamManager>& stream_manager,
    const std::shared_ptr<InferenceScheduler>& high_res_scheduler,
    const std::shared_ptr<InferenceScheduler>& low_res_scheduler,
    const std::shared_ptr<VideoJobExecutor>& video_job_executor,
    const std::shared_ptr<RequestGate>& request_gate,
    std::vector<std::string> class_names
);

}  // namespace api
}  // namespace yolo
