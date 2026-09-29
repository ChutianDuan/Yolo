#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "stream/frame_deadline.h"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace std::chrono;
    using yolo::isFrameExpired;
    const steady_clock::time_point captured_at{seconds(100)};
    expect(!isFrameExpired(true, 300, captured_at, captured_at), "new frame was expired");
    expect(!isFrameExpired(true, 300, captured_at, captured_at + milliseconds(299)),
           "frame before the deadline was expired");
    expect(!isFrameExpired(true, 300, captured_at, captured_at + milliseconds(300)),
           "frame exactly at the deadline was expired");
    expect(isFrameExpired(true, 300, captured_at, captured_at + milliseconds(300) + nanoseconds(1)),
           "sub-millisecond overrun was rounded down");
    expect(isFrameExpired(true, 300, captured_at, captured_at + hours(8)),
           "old live frame was accepted");
    expect(!isFrameExpired(false, 1, captured_at, captured_at + hours(8)),
           "offline backpressure was converted to frame loss");
    expect(!isFrameExpired(true, 0, captured_at, captured_at + hours(8)),
           "disabled result deadline still discarded live frames");

    // The same captured timestamp spans decoding, queueing, inference and publication.
    const auto after_preprocess = captured_at + milliseconds(20);
    const auto after_queue = after_preprocess + milliseconds(200);
    const auto after_inference = after_queue + milliseconds(90);
    expect(!isFrameExpired(true, 300, captured_at, after_preprocess), "preprocess should fit");
    expect(!isFrameExpired(true, 300, captured_at, after_queue), "queue should still fit");
    expect(isFrameExpired(true, 300, captured_at, after_inference),
           "model completion incorrectly restarted the frame deadline");

    const auto fresh_model_result = captured_at + milliseconds(290);
    const auto after_tracking = fresh_model_result + milliseconds(20);
    expect(!isFrameExpired(true, 300, captured_at, fresh_model_result), "fresh result was expired");
    expect(isFrameExpired(true, 300, captured_at, after_tracking),
           "publication after tracking must recheck the deadline");
    std::cout << "frame_deadline_test passed\n";
}
