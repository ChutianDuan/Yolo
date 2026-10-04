#include "drogon/request_gate.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace yolo::api {
namespace {

constexpr size_t kMaxTrackedClients = 16384;
constexpr uint64_t kCleanupInterval = 1024;
constexpr auto kClientIdleTimeout = std::chrono::minutes(10);
constexpr std::string_view kBearerPrefix = "Bearer ";

bool constantTimeEquals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    // 等长令牌遍历全部字节，避免比较在第一个不同字符处提前返回。
    unsigned char difference = 0;
    for (size_t index = 0; index < lhs.size(); ++index) {
        difference |= static_cast<unsigned char>(lhs[index])
            ^ static_cast<unsigned char>(rhs[index]);
    }
    return difference == 0;
}

}  // namespace

class RequestGate::Impl {
public:
    Impl(std::string bearer_token, double rate, size_t burst)
        : expected_authorization_(
              bearer_token.empty()
                  ? std::string()
                  : std::string(kBearerPrefix) + bearer_token
          ),
          rate_(std::max(0.0, rate)),
          burst_(std::max<size_t>(burst, 1)) {}

    RequestGateDecision check(
        std::string_view authorization_header,
        std::string client_id,
        std::chrono::steady_clock::time_point now
    ) {
        const bool authorized = expected_authorization_.empty()
            || constantTimeEquals(authorization_header, expected_authorization_);

        std::lock_guard<std::mutex> lock(mutex_);
        if (rate_ <= 0.0) {
            if (!authorized) {
                ++stats_.unauthorized_count;
                return {RequestGateStatus::Unauthorized, 0};
            }
            ++stats_.allowed_count;
            return {};
        }

        ++check_count_;
        if (check_count_ % kCleanupInterval == 0
            || buckets_.size() >= kMaxTrackedClients) {
            cleanupLocked(now);
        }

        if (client_id.empty()) {
            client_id = "__unknown__";
        }
        auto found = buckets_.find(client_id);
        if (found == buckets_.end()) {
            if (buckets_.size() >= kMaxTrackedClients) {
                ++stats_.rate_limited_count;
                stats_.tracked_client_count = buckets_.size();
                return {RequestGateStatus::RateLimited, 1};
            }
            found = buckets_.emplace(
                std::move(client_id),
                Bucket{static_cast<double>(burst_), now, now}
            ).first;
        }

        // 每个客户端独立使用令牌桶：按时间补充令牌，最多累积 burst 个。
        Bucket& bucket = found->second;
        const double elapsed_seconds =
            std::chrono::duration<double>(now - bucket.updated_at).count();
        if (elapsed_seconds > 0.0) {
            bucket.tokens = std::min(
                static_cast<double>(burst_),
                bucket.tokens + elapsed_seconds * rate_
            );
            bucket.updated_at = now;
        }
        bucket.last_seen = now;

        // 未通过鉴权的请求也消耗额度，限制无效令牌的反复尝试。
        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            if (!authorized) {
                ++stats_.unauthorized_count;
                stats_.tracked_client_count = buckets_.size();
                return {RequestGateStatus::Unauthorized, 0};
            }
            ++stats_.allowed_count;
            stats_.tracked_client_count = buckets_.size();
            return {};
        }

        ++stats_.rate_limited_count;
        stats_.tracked_client_count = buckets_.size();
        const int retry_after = std::max(
            1,
            static_cast<int>(std::ceil((1.0 - bucket.tokens) / rate_))
        );
        return {RequestGateStatus::RateLimited, retry_after};
    }

    RequestGateStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        RequestGateStats snapshot = stats_;
        snapshot.tracked_client_count = buckets_.size();
        return snapshot;
    }

    bool authenticationEnabled() const {
        return !expected_authorization_.empty();
    }

    bool rateLimitEnabled() const {
        return rate_ > 0.0;
    }

private:
    struct Bucket {
        double tokens = 0.0;
        std::chrono::steady_clock::time_point updated_at;
        std::chrono::steady_clock::time_point last_seen;
    };

    void cleanupLocked(std::chrono::steady_clock::time_point now) {
        for (auto iterator = buckets_.begin(); iterator != buckets_.end();) {
            if (now - iterator->second.last_seen > kClientIdleTimeout) {
                iterator = buckets_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        stats_.tracked_client_count = buckets_.size();
    }

    std::string expected_authorization_;
    double rate_;
    size_t burst_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
    uint64_t check_count_ = 0;
    RequestGateStats stats_;
};

RequestGate::RequestGate(
    std::string bearer_token,
    double rate_limit_requests_per_second,
    size_t rate_limit_burst
) : impl_(std::make_unique<Impl>(
        std::move(bearer_token),
        rate_limit_requests_per_second,
        rate_limit_burst
    )) {}

RequestGate::~RequestGate() = default;

RequestGateDecision RequestGate::check(
    std::string_view authorization_header,
    std::string client_id,
    std::chrono::steady_clock::time_point now
) {
    return impl_->check(authorization_header, std::move(client_id), now);
}

RequestGateStats RequestGate::stats() const {
    return impl_->stats();
}

bool RequestGate::authenticationEnabled() const {
    return impl_->authenticationEnabled();
}

bool RequestGate::rateLimitEnabled() const {
    return impl_->rateLimitEnabled();
}

}  // namespace yolo::api
