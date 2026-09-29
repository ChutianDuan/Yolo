#include <cstdlib>
#include <iostream>
#include <string>

#include "stream/detection_cadence.h"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}


yolo::DetectionTier selectAvailable(
    yolo::DetectionCadence& cadence, double timestamp_ms,
    bool high_available, bool low_available, bool urgent_high = false
) {
    return cadence.select(timestamp_ms, urgent_high, high_available, low_available);
}

void testBusyAdmission() {
    using yolo::DetectionCadence;
    using yolo::DetectionTier;
    DetectionCadence low(4.0, 1.0, true);
    (void)selectAvailable(low, 0.0, true, true);
    (void)selectAvailable(low, 33.0, true, true);
    expect(selectAvailable(low, 250.0, true, false) == DetectionTier::None, "busy low admitted");
    expect(selectAvailable(low, 260.0, true, true) == DetectionTier::Low,
           "busy low consumed its due budget before admission");
    expect(selectAvailable(low, 261.0, true, true) == DetectionTier::None, "low catch-up burst");
    expect(selectAvailable(low, 500.0, true, true) == DetectionTier::Low, "low phase drifted");

    DetectionCadence high(4.0, 1.0, true);
    (void)selectAvailable(high, 0.0, true, true);
    (void)selectAvailable(high, 33.0, true, true);
    expect(selectAvailable(high, 1000.0, false, true) == DetectionTier::Low, "busy high starved due low");
    expect(selectAvailable(high, 1001.0, true, true) == DetectionTier::High, "busy high lost periodic budget");
    expect(selectAvailable(high, 1002.0, true, true) == DetectionTier::None, "high catch-up burst");

    DetectionCadence initial(4.0, 0.0, true);
    expect(selectAvailable(initial, 0.0, false, true) == DetectionTier::Low, "initial busy high starved low");
    expect(selectAvailable(initial, 33.0, false, false) == DetectionTier::None, "both busy admitted");
    expect(selectAvailable(initial, 40.0, true, true) == DetectionTier::High, "delayed initial high lost");
    expect(selectAvailable(initial, 41.0, true, true) == DetectionTier::None, "disabled high repeated");

    DetectionCadence stalled(4.0, 1.0, true);
    expect(selectAvailable(stalled, 0.0, false, false) == DetectionTier::None, "initial busy admitted");
    expect(selectAvailable(stalled, 10000.0, false, false) == DetectionTier::None, "stall busy admitted");
    expect(selectAvailable(stalled, 10001.0, true, true) == DetectionTier::High, "stall high lost");
    expect(selectAvailable(stalled, 10002.0, true, true) == DetectionTier::Low, "stall low lost");
    expect(selectAvailable(stalled, 10003.0, true, true) == DetectionTier::None, "stall catch-up burst");
    expect(selectAvailable(stalled, 0.0, false, false) == DetectionTier::None, "reset busy admitted");
    expect(selectAvailable(stalled, 33.0, true, true) == DetectionTier::High, "reset initial high lost");
    expect(selectAvailable(stalled, 34.0, true, true) == DetectionTier::Low, "reset low lost");

    DetectionCadence urgent(4.0, 1.0, true);
    (void)selectAvailable(urgent, 0.0, true, true);
    (void)selectAvailable(urgent, 33.0, true, true);
    expect(selectAvailable(urgent, 250.0, false, true, true) == DetectionTier::Low, "busy urgent starved low");
    expect(selectAvailable(urgent, 251.0, true, false, true) == DetectionTier::High, "busy low blocked urgent");

    DetectionCadence single(4.0, 1.0, false);
    expect(selectAvailable(single, 0.0, false, true) == DetectionTier::None, "single busy admitted");
    expect(selectAvailable(single, 33.0, true, false) == DetectionTier::High, "single initial lost");
    expect(selectAvailable(single, 250.0, false, true) == DetectionTier::None, "single busy admitted");
    expect(selectAvailable(single, 260.0, true, false) == DetectionTier::High, "single due lost");
    expect(selectAvailable(single, 261.0, true, true) == DetectionTier::None, "single catch-up burst");

    for (const int fps : {25, 30, 60}) {
        DetectionCadence cadence(4.0, 0.5, true);
        int low_count = 0, high_count = 0;
        for (int frame = 0; frame < fps * 8; ++frame) {
            const double now_ms = frame * 1000.0 / fps;
            const bool available = std::fmod(now_ms, 250.0) >= 50.0;
            const auto tier = selectAvailable(cadence, now_ms, available, available);
            low_count += tier == DetectionTier::Low;
            high_count += tier == DetectionTier::High;
        }
        expect(low_count == 32, "short busy windows reduced admitted low cadence");
        expect(high_count == 4, "short busy windows reduced admitted high cadence");
    }
}

}  // namespace

int main() {
    using yolo::DetectionCadence;
    using yolo::DetectionTier;

    for (const int fps : {25, 30, 60}) {
        for (const double high_fps : {0.5, 1.0}) {
            DetectionCadence cadence(4.0, high_fps, true);
            int low_count = 0, high_count = 0;
            for (int frame = 0; frame < fps * 8; ++frame) {
                const auto tier = cadence.select(frame * 1000.0 / fps);
                low_count += tier == DetectionTier::Low;
                high_count += tier == DetectionTier::High;
            }
            expect(low_count == 32, "high refresh or frame rounding reduced low FPS");
            expect(high_count == static_cast<int>(high_fps * 8), "high cadence drifted");
        }
    }

    DetectionCadence urgent(4.0, 1.0, true);
    expect(urgent.select(0.0) == DetectionTier::High, "initial authority refresh missing");
    expect(urgent.select(33.0) == DetectionTier::Low, "low refresh consumed by high");
    expect(urgent.select(100.0, true) == DetectionTier::High, "urgent refresh delayed");
    expect(urgent.select(250.0) == DetectionTier::Low, "urgent refresh reset low phase");

    DetectionCadence jump(4.0, 1.0, true);
    (void)jump.select(0.0);
    expect(jump.select(10000.0) == DetectionTier::High, "large gap did not refresh");
    expect(jump.select(10001.0) == DetectionTier::Low, "low refresh missing after gap");
    expect(jump.select(10002.0) == DetectionTier::None, "missed slots caused catch-up burst");
    expect(jump.select(0.0) == DetectionTier::High, "timestamp reset did not reanchor");
    expect(jump.select(33.0) == DetectionTier::Low, "low cadence froze after timestamp reset");

    DetectionCadence single(4.0, 1.0, false);
    int high_count = 0;
    for (int frame = 0; frame < 120; ++frame) {
        const auto tier = single.select(frame * 1000.0 / 30.0);
        expect(tier != DetectionTier::Low, "single-model mode selected absent model");
        high_count += tier == DetectionTier::High;
    }
    expect(high_count == 16, "single-model detection rate changed");

    DetectionCadence no_periodic_high(4.0, 0.0, true);
    expect(no_periodic_high.select(0.0) == DetectionTier::High, "initial high missing");
    expect(no_periodic_high.select(33.0) == DetectionTier::Low, "initial low missing");
    expect(no_periodic_high.select(1000.0) == DetectionTier::Low, "disabled high ran periodically");

    DetectionCadence every_frame(0.0, 0.0, false);
    for (int frame = 0; frame < 30; ++frame) {
        expect(every_frame.select(frame * 33.0) == DetectionTier::High, "zero FPS did not detect every frame");
    }
    testBusyAdmission();
    std::cout << "detection_cadence_test passed\n";
}
