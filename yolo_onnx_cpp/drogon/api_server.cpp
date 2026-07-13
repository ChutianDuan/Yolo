#include "api_server.h"

#include "drogon/api_gateway.h"

namespace yolo {

void runApiServer(
    const std::shared_ptr<YoloEngine>& engine,
    const std::shared_ptr<YoloEngine>& low_res_engine,
    const AppConfig& config,
    uint16_t port
) {
    api::ApiGateway(engine, low_res_engine, config).run(port);
}

}  // namespace yolo
