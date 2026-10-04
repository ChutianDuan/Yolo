#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace yolo::api {

enum class RequestGateStatus {
    Allowed,
    Unauthorized,
    RateLimited,
};

struct RequestGateDecision {
    RequestGateStatus status = RequestGateStatus::Allowed;
    int retry_after_seconds = 0;
};

// 门禁生命周期内的统计，只包含实际进入 check() 的请求。
struct RequestGateStats {
    // 按最终门禁决定累计；被限流的请求不再计入鉴权失败次数。
    uint64_t allowed_count = 0;
    uint64_t unauthorized_count = 0;
    uint64_t rate_limited_count = 0;
    // 当前保留的客户端令牌桶数量，会随空闲清理减少，不是累计客户端数。
    size_t tracked_client_count = 0;
};

class RequestGate final {
public:
    RequestGate(
        std::string bearer_token,
        double rate_limit_requests_per_second,
        size_t rate_limit_burst
    );
    ~RequestGate();

    RequestGate(const RequestGate&) = delete;
    RequestGate& operator=(const RequestGate&) = delete;

    RequestGateDecision check(
        std::string_view authorization_header,
        std::string client_id,
        std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()
    );
    RequestGateStats stats() const;
    bool authenticationEnabled() const;
    bool rateLimitEnabled() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace yolo::api
