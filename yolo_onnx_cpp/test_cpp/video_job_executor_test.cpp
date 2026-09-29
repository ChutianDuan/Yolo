#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "drogon/video_job_executor.h"

namespace {

void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

}  // namespace

int main() {
    yolo::api::VideoJobExecutor executor(1, 1);
    std::mutex mutex;
    std::condition_variable condition;
    bool first_started = false;
    bool release_first = false;
    std::atomic<int> completed{0};

    expect(executor.submit([&]() {
        {
            std::unique_lock<std::mutex> lock(mutex);
            first_started = true;
            condition.notify_all();
            condition.wait(lock, [&]() { return release_first; });
        }
        ++completed;
    }), "first video job was rejected");

    {
        std::unique_lock<std::mutex> lock(mutex);
        expect(
            condition.wait_for(
                lock,
                std::chrono::seconds(2),
                [&]() { return first_started; }
            ),
            "first video job did not start"
        );
    }

    expect(executor.submit([&]() { ++completed; }), "queued video job was rejected");
    expect(!executor.submit([&]() { ++completed; }), "queue overflow was accepted");

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first = true;
    }
    condition.notify_all();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (completed.load() != 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }

    expect(completed.load() == 2, "accepted video jobs did not complete");
    const auto stats = executor.stats();
    expect(stats.submitted_count == 2, "video job submitted count mismatch");
    expect(stats.completed_count == 2, "video job completed count mismatch");
    expect(stats.rejected_count == 1, "video job rejected count mismatch");
    expect(stats.queued_count == 0, "video job queue did not drain");
    expect(stats.in_flight_count == 0, "video job worker did not drain");

    std::cout << "video_job_executor_test passed\n";
    return 0;
}
