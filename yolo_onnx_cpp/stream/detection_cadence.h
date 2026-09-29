#pragma once

#include <cmath>

namespace yolo {

enum class DetectionTier { None, Low, High };

// One decision per input frame; high refreshes do not consume the low-model budget.
class DetectionCadence final {
public:
    DetectionCadence(double low_fps, double high_fps, bool has_low_model)
        : low_interval_ms_(low_fps > 0.0 ? 1000.0 / low_fps : 0.0),
          high_interval_ms_(high_fps > 0.0 ? 1000.0 / high_fps : 0.0),
          has_low_model_(has_low_model) {}

    DetectionTier select(double timestamp_ms, bool urgent_high = false) {
        return select(timestamp_ms, urgent_high, true, true);
    }

    // Unavailable tiers retain their due slot; advance only admitted work.
    DetectionTier select(
        double timestamp_ms, bool urgent_high, bool high_available, bool low_available
    ) {
        const bool restart = !started_ || timestamp_ms + kToleranceMs < last_timestamp_ms_;
        if (restart) {
            next_low_ms_ = timestamp_ms;
            next_high_ms_ = timestamp_ms;
            started_ = true;
            initial_high_pending_ = true;
        }
        last_timestamp_ms_ = timestamp_ms;
        const bool low_due = timestamp_ms + kToleranceMs >= next_low_ms_;
        const bool high_due = high_interval_ms_ > 0.0
            && timestamp_ms + kToleranceMs >= next_high_ms_;

        if (high_available && (initial_high_pending_ || urgent_high
            || (!has_low_model_ && low_due) || (has_low_model_ && high_due))) {
            initial_high_pending_ = false;
            if (!has_low_model_) {
                advance(next_low_ms_, low_interval_ms_, timestamp_ms);
            }
            if (high_due) {
                advance(next_high_ms_, high_interval_ms_, timestamp_ms);
            }
            return DetectionTier::High;
        }
        if (has_low_model_ && low_available && low_due) {
            advance(next_low_ms_, low_interval_ms_, timestamp_ms);
            return DetectionTier::Low;
        }
        return DetectionTier::None;
    }

private:
    static constexpr double kToleranceMs = 1e-6;

    static void advance(double& next_ms, double interval_ms, double now_ms) {
        if (interval_ms <= 0.0) {
            next_ms = now_ms;
            return;
        }
        // Keep the original phase across frame quantization; skip missed slots.
        const double slots = std::floor((now_ms + kToleranceMs - next_ms) / interval_ms) + 1.0;
        if (slots > 0.0) {
            next_ms += slots * interval_ms;
        }
    }

    double low_interval_ms_;
    double high_interval_ms_;
    bool has_low_model_;
    bool started_ = false;
    bool initial_high_pending_ = false;
    double last_timestamp_ms_ = 0.0;
    double next_low_ms_ = 0.0;
    double next_high_ms_ = 0.0;
};

}  // namespace yolo
