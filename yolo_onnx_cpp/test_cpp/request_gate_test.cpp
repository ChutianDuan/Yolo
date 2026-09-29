#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "drogon/request_gate.h"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const auto start = std::chrono::steady_clock::time_point{};

    yolo::api::RequestGate open_gate("", 0.0, 1);
    expect(!open_gate.authenticationEnabled(), "authentication unexpectedly enabled");
    expect(!open_gate.rateLimitEnabled(), "rate limit unexpectedly enabled");
    expect(
        open_gate.check("", "client-a", start).status
            == yolo::api::RequestGateStatus::Allowed,
        "disabled gate rejected a request"
    );

    yolo::api::RequestGate authenticated("secret-token", 0.0, 1);
    expect(authenticated.authenticationEnabled(), "authentication was not enabled");
    expect(
        authenticated.check("Bearer wrong", "client-a", start).status
            == yolo::api::RequestGateStatus::Unauthorized,
        "wrong bearer token was accepted"
    );
    expect(
        authenticated.check("Bearer secret-token", "client-a", start).status
            == yolo::api::RequestGateStatus::Allowed,
        "correct bearer token was rejected"
    );
    const auto authenticated_stats = authenticated.stats();
    expect(authenticated_stats.unauthorized_count == 1, "unauthorized count mismatch");
    expect(authenticated_stats.allowed_count == 1, "authenticated allowed count mismatch");

    yolo::api::RequestGate protected_limited("secret-token", 1.0, 1);
    expect(
        protected_limited.check("Bearer wrong", "client-c", start).status
            == yolo::api::RequestGateStatus::Unauthorized,
        "first unauthorized request did not reach authentication"
    );
    expect(
        protected_limited.check("Bearer secret-token", "client-c", start).status
            == yolo::api::RequestGateStatus::RateLimited,
        "unauthorized request did not consume the client rate limit"
    );
    const auto protected_stats = protected_limited.stats();
    expect(protected_stats.unauthorized_count == 1, "protected unauthorized count mismatch");
    expect(protected_stats.rate_limited_count == 1, "protected rate limit count mismatch");

    yolo::api::RequestGate limited("", 2.0, 2);
    expect(limited.rateLimitEnabled(), "rate limit was not enabled");
    expect(
        limited.check("", "client-a", start).status
            == yolo::api::RequestGateStatus::Allowed,
        "first token was rejected"
    );
    expect(
        limited.check("", "client-a", start).status
            == yolo::api::RequestGateStatus::Allowed,
        "second token was rejected"
    );
    const auto exhausted = limited.check("", "client-a", start);
    expect(
        exhausted.status == yolo::api::RequestGateStatus::RateLimited,
        "exhausted client was not rate limited"
    );
    expect(exhausted.retry_after_seconds == 1, "retry-after mismatch");
    expect(
        limited.check("", "client-b", start).status
            == yolo::api::RequestGateStatus::Allowed,
        "rate limit was not isolated by client"
    );
    expect(
        limited.check("", "client-a", start + std::chrono::milliseconds(500)).status
            == yolo::api::RequestGateStatus::Allowed,
        "token bucket did not refill"
    );

    const auto limited_stats = limited.stats();
    expect(limited_stats.allowed_count == 4, "limited allowed count mismatch");
    expect(limited_stats.rate_limited_count == 1, "rate-limited count mismatch");
    expect(limited_stats.tracked_client_count == 2, "tracked client count mismatch");

    std::cout << "request_gate_test passed\n";
    return 0;
}
