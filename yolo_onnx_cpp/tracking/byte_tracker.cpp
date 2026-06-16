#include "byte_tracker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace yolo {
namespace {

constexpr float kTrackScoreThreshold = 0.45F;
constexpr float kLowScoreThreshold = 0.1F;
constexpr float kNewTrackThreshold = 0.45F;
constexpr float kFirstMatchThreshold = 0.8F;
constexpr float kSecondMatchThreshold = 0.5F;
constexpr float kUnconfirmedMatchThreshold = 0.7F;
constexpr float kDuplicateIouThreshold = 0.85F;
constexpr float kStdWeightPosition = 1.0F / 20.0F;
constexpr float kStdWeightVelocity = 1.0F / 160.0F;
constexpr float kEpsilon = 1.0e-6F;
constexpr float kLargeCost = 1.0e6F;
constexpr int kDefaultTrackBuffer = 30;

using Vector8 = std::array<float, 8>;
using Vector4 = std::array<float, 4>;
using Matrix8 = std::array<std::array<float, 8>, 8>;
using Matrix4 = std::array<std::array<float, 4>, 4>;
using Matrix8x4 = std::array<std::array<float, 4>, 8>;

float boxWidth(const Detection& detection) {
    return std::max(0.0F, detection.x2 - detection.x1);
}

float boxHeight(const Detection& detection) {
    return std::max(0.0F, detection.y2 - detection.y1);
}

float boxArea(const Detection& detection) {
    return boxWidth(detection) * boxHeight(detection);
}

float square(float value) {
    return value * value;
}

Matrix8 identity8() {
    Matrix8 result{};
    for (size_t i = 0; i < result.size(); ++i) {
        result[i][i] = 1.0F;
    }
    return result;
}

Matrix8 transpose(const Matrix8& matrix) {
    Matrix8 result{};
    for (size_t row = 0; row < 8; ++row) {
        for (size_t col = 0; col < 8; ++col) {
            result[col][row] = matrix[row][col];
        }
    }
    return result;
}

Vector8 multiply(const Matrix8& matrix, const Vector8& values) {
    Vector8 result{};
    for (size_t row = 0; row < 8; ++row) {
        for (size_t col = 0; col < 8; ++col) {
            result[row] += matrix[row][col] * values[col];
        }
    }
    return result;
}

Matrix8 multiply(const Matrix8& lhs, const Matrix8& rhs) {
    Matrix8 result{};
    for (size_t row = 0; row < 8; ++row) {
        for (size_t mid = 0; mid < 8; ++mid) {
            if (std::abs(lhs[row][mid]) <= kEpsilon) {
                continue;
            }
            for (size_t col = 0; col < 8; ++col) {
                result[row][col] += lhs[row][mid] * rhs[mid][col];
            }
        }
    }
    return result;
}

Matrix4 invert4(Matrix4 matrix) {
    std::array<std::array<float, 8>, 4> augmented{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            augmented[row][col] = matrix[row][col];
        }
        augmented[row][row + 4] = 1.0F;
    }

    for (size_t col = 0; col < 4; ++col) {
        size_t pivot = col;
        float best = std::abs(augmented[col][col]);
        for (size_t row = col + 1; row < 4; ++row) {
            const float value = std::abs(augmented[row][col]);
            if (value > best) {
                best = value;
                pivot = row;
            }
        }

        if (pivot != col) {
            std::swap(augmented[pivot], augmented[col]);
        }

        float divisor = augmented[col][col];
        if (std::abs(divisor) < kEpsilon) {
            divisor = divisor < 0.0F ? -kEpsilon : kEpsilon;
            augmented[col][col] = divisor;
        }

        for (size_t item = 0; item < 8; ++item) {
            augmented[col][item] /= divisor;
        }

        for (size_t row = 0; row < 4; ++row) {
            if (row == col) {
                continue;
            }
            const float factor = augmented[row][col];
            if (std::abs(factor) <= kEpsilon) {
                continue;
            }
            for (size_t item = 0; item < 8; ++item) {
                augmented[row][item] -= factor * augmented[col][item];
            }
        }
    }

    Matrix4 inverse{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            inverse[row][col] = augmented[row][col + 4];
        }
    }
    return inverse;
}

Matrix8 motionMatrix() {
    Matrix8 matrix = identity8();
    for (size_t dim = 0; dim < 4; ++dim) {
        matrix[dim][dim + 4] = 1.0F;
    }
    return matrix;
}

Matrix8 motionCovariance(const Vector8& mean) {
    const float height = std::max(mean[3], 1.0F);
    const std::array<float, 8> stddev = {
        kStdWeightPosition * height,
        kStdWeightPosition * height,
        1.0e-2F,
        kStdWeightPosition * height,
        kStdWeightVelocity * height,
        kStdWeightVelocity * height,
        1.0e-5F,
        kStdWeightVelocity * height,
    };

    Matrix8 covariance{};
    for (size_t i = 0; i < stddev.size(); ++i) {
        covariance[i][i] = square(stddev[i]);
    }
    return covariance;
}

Matrix4 measurementCovariance(const Vector8& mean) {
    const float height = std::max(mean[3], 1.0F);
    const std::array<float, 4> stddev = {
        kStdWeightPosition * height,
        kStdWeightPosition * height,
        1.0e-1F,
        kStdWeightPosition * height,
    };

    Matrix4 covariance{};
    for (size_t i = 0; i < stddev.size(); ++i) {
        covariance[i][i] = square(stddev[i]);
    }
    return covariance;
}

Matrix8 initiateCovariance(const Vector4& measurement) {
    const float height = std::max(measurement[3], 1.0F);
    const std::array<float, 8> stddev = {
        2.0F * kStdWeightPosition * height,
        2.0F * kStdWeightPosition * height,
        1.0e-2F,
        2.0F * kStdWeightPosition * height,
        10.0F * kStdWeightVelocity * height,
        10.0F * kStdWeightVelocity * height,
        1.0e-5F,
        10.0F * kStdWeightVelocity * height,
    };

    Matrix8 covariance{};
    for (size_t i = 0; i < stddev.size(); ++i) {
        covariance[i][i] = square(stddev[i]);
    }
    return covariance;
}

Vector8 initiateMean(const Vector4& measurement) {
    Vector8 mean{};
    for (size_t i = 0; i < measurement.size(); ++i) {
        mean[i] = measurement[i];
    }
    return mean;
}

void kalmanPredict(Vector8& mean, Matrix8& covariance, bool tracked) {
    if (!tracked) {
        mean[7] = 0.0F;
    }

    const Matrix8 motion = motionMatrix();
    const Matrix8 motion_covariance = motionCovariance(mean);
    mean = multiply(motion, mean);
    covariance = multiply(multiply(motion, covariance), transpose(motion));
    for (size_t row = 0; row < 8; ++row) {
        for (size_t col = 0; col < 8; ++col) {
            covariance[row][col] += motion_covariance[row][col];
        }
    }
}

void kalmanUpdate(Vector8& mean, Matrix8& covariance, const Vector4& measurement) {
    Matrix4 projected_covariance = measurementCovariance(mean);
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            projected_covariance[row][col] += covariance[row][col];
        }
    }

    const Matrix4 inverse_projected = invert4(projected_covariance);
    Matrix8x4 kalman_gain{};
    for (size_t row = 0; row < 8; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            for (size_t mid = 0; mid < 4; ++mid) {
                kalman_gain[row][col] += covariance[row][mid] * inverse_projected[mid][col];
            }
        }
    }

    Vector4 innovation{};
    for (size_t i = 0; i < 4; ++i) {
        innovation[i] = measurement[i] - mean[i];
    }

    for (size_t row = 0; row < 8; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            mean[row] += kalman_gain[row][col] * innovation[col];
        }
    }

    Matrix8 updated_covariance = covariance;
    for (size_t row = 0; row < 8; ++row) {
        for (size_t col = 0; col < 8; ++col) {
            float correction = 0.0F;
            for (size_t a = 0; a < 4; ++a) {
                for (size_t b = 0; b < 4; ++b) {
                    correction += kalman_gain[row][a]
                        * projected_covariance[a][b]
                        * kalman_gain[col][b];
                }
            }
            updated_covariance[row][col] -= correction;
        }
    }

    for (size_t row = 0; row < 8; ++row) {
        for (size_t col = row + 1; col < 8; ++col) {
            const float symmetric = (updated_covariance[row][col]
                + updated_covariance[col][row]) * 0.5F;
            updated_covariance[row][col] = symmetric;
            updated_covariance[col][row] = symmetric;
        }
    }
    covariance = updated_covariance;
}

struct AssignmentMatch {
    size_t row = 0;
    size_t col = 0;
    float cost = 0.0F;
};

std::vector<AssignmentMatch> hungarianAssign(
    const std::vector<std::vector<float>>& costs,
    float threshold
) {
    const size_t row_count = costs.size();
    const size_t col_count = row_count == 0 ? 0 : costs[0].size();
    if (row_count == 0 || col_count == 0) {
        return {};
    }

    const bool transposed = row_count > col_count;
    const size_t rows = transposed ? col_count : row_count;
    const size_t cols = transposed ? row_count : col_count;

    std::vector<std::vector<float>> matrix(rows, std::vector<float>(cols, 0.0F));
    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            matrix[row][col] = transposed ? costs[col][row] : costs[row][col];
        }
    }

    std::vector<float> u(rows + 1, 0.0F);
    std::vector<float> v(cols + 1, 0.0F);
    std::vector<size_t> p(cols + 1, 0);
    std::vector<size_t> way(cols + 1, 0);

    for (size_t i = 1; i <= rows; ++i) {
        p[0] = i;
        size_t j0 = 0;
        std::vector<float> minv(cols + 1, std::numeric_limits<float>::max());
        std::vector<char> used(cols + 1, false);
        do {
            used[j0] = true;
            const size_t i0 = p[j0];
            float delta = std::numeric_limits<float>::max();
            size_t j1 = 0;
            for (size_t j = 1; j <= cols; ++j) {
                if (used[j]) {
                    continue;
                }
                const float current = matrix[i0 - 1][j - 1] - u[i0] - v[j];
                if (current < minv[j]) {
                    minv[j] = current;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }

            for (size_t j = 0; j <= cols; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);

        do {
            const size_t j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    std::vector<AssignmentMatch> matches;
    for (size_t col = 1; col <= cols; ++col) {
        if (p[col] == 0) {
            continue;
        }
        const size_t assigned_row = p[col] - 1;
        const size_t assigned_col = col - 1;
        const size_t track_index = transposed ? assigned_col : assigned_row;
        const size_t detection_index = transposed ? assigned_row : assigned_col;
        const float cost = costs[track_index][detection_index];
        if (track_index < row_count && detection_index < col_count && cost <= threshold) {
            matches.push_back(AssignmentMatch{track_index, detection_index, cost});
        }
    }

    std::sort(
        matches.begin(),
        matches.end(),
        [](const AssignmentMatch& a, const AssignmentMatch& b) {
            if (a.row != b.row) {
                return a.row < b.row;
            }
            return a.col < b.col;
        }
    );
    return matches;
}

}  // namespace

ByteTracker::ByteTracker()
    : track_buffer_(kDefaultTrackBuffer) {}

float ByteTracker::boxIou(const Detection& a, const Detection& b) {
    const float inter_x1 = std::max(a.x1, b.x1);
    const float inter_y1 = std::max(a.y1, b.y1);
    const float inter_x2 = std::min(a.x2, b.x2);
    const float inter_y2 = std::min(a.y2, b.y2);

    const float inter_w = std::max(0.0F, inter_x2 - inter_x1);
    const float inter_h = std::max(0.0F, inter_y2 - inter_y1);
    const float inter_area = inter_w * inter_h;

    const float union_area = boxArea(a) + boxArea(b) - inter_area;
    if (union_area <= 0.0F) {
        return 0.0F;
    }
    return inter_area / union_area;
}

Detection ByteTracker::stateToDetection(
    const std::array<float, 8>& mean,
    int class_id,
    float score
) {
    const float aspect = std::max(mean[2], 1.0e-3F);
    const float height = std::max(mean[3], 1.0e-3F);
    const float width = aspect * height;
    Detection detection;
    detection.class_id = class_id;
    detection.score = score;
    detection.x1 = mean[0] - width * 0.5F;
    detection.y1 = mean[1] - height * 0.5F;
    detection.x2 = mean[0] + width * 0.5F;
    detection.y2 = mean[1] + height * 0.5F;
    return detection;
}

std::array<float, 4> ByteTracker::detectionToMeasurement(const Detection& detection) {
    const float width = std::max(boxWidth(detection), 1.0e-3F);
    const float height = std::max(boxHeight(detection), 1.0e-3F);
    return {
        detection.x1 + width * 0.5F,
        detection.y1 + height * 0.5F,
        width / height,
        height,
    };
}

ByteTracker::Track ByteTracker::createTrack(const Detection& detection) {
    const Vector4 measurement = detectionToMeasurement(detection);

    Track track;
    track.id = next_track_id_++;
    track.detection = detection;
    track.mean = initiateMean(measurement);
    track.covariance = initiateCovariance(measurement);
    track.state = TrackState::Tracked;
    track.activated = frame_id_ == 1;
    track.matched = true;
    track.frame_id = frame_id_;
    track.start_frame = frame_id_;
    track.tracklet_len = 0;
    return track;
}

void ByteTracker::predictTrack(Track& track) const {
    kalmanPredict(track.mean, track.covariance, track.state == TrackState::Tracked);
    track.detection = stateToDetection(
        track.mean,
        track.detection.class_id,
        track.detection.score
    );
}

void ByteTracker::updateTrack(Track& track, const Detection& detection) {
    const bool was_lost = track.state == TrackState::Lost;
    const Vector4 measurement = detectionToMeasurement(detection);
    kalmanUpdate(track.mean, track.covariance, measurement);
    track.detection = stateToDetection(track.mean, detection.class_id, detection.score);
    track.state = TrackState::Tracked;
    track.activated = true;
    track.matched = true;
    track.frame_id = frame_id_;
    track.tracklet_len = was_lost ? 0 : track.tracklet_len + 1;
}

void ByteTracker::markLost(Track& track) {
    if (track.state != TrackState::Removed) {
        track.state = TrackState::Lost;
        track.matched = false;
    }
}

void ByteTracker::markRemoved(Track& track) {
    track.state = TrackState::Removed;
    track.matched = false;
}

std::vector<ByteTracker::Match> ByteTracker::assignDetections(
    const std::vector<size_t>& track_indices,
    const std::vector<Detection>& detections,
    float match_threshold,
    bool fuse_score
) const {
    if (track_indices.empty() || detections.empty()) {
        return {};
    }

    std::vector<std::vector<float>> costs(
        track_indices.size(),
        std::vector<float>(detections.size(), kLargeCost)
    );

    for (size_t track_pos = 0; track_pos < track_indices.size(); ++track_pos) {
        const auto& track = tracks_[track_indices[track_pos]];
        for (size_t detection_index = 0; detection_index < detections.size(); ++detection_index) {
            const auto& detection = detections[detection_index];
            if (track.detection.class_id != detection.class_id) {
                continue;
            }

            const float iou = boxIou(track.detection, detection);
            const float similarity = fuse_score ? iou * detection.score : iou;
            costs[track_pos][detection_index] = 1.0F - similarity;
        }
    }

    const auto local_matches = hungarianAssign(costs, match_threshold);
    std::vector<Match> matches;
    matches.reserve(local_matches.size());
    for (const auto& match : local_matches) {
        matches.push_back(Match{
            track_indices[match.row],
            match.col,
            match.cost
        });
    }
    return matches;
}

std::vector<TrackedDetection> ByteTracker::currentTrackedDetections() const {
    std::vector<TrackedDetection> output_tracks;
    for (const auto& track : tracks_) {
        if (track.state == TrackState::Tracked && track.frame_id == frame_id_) {
            output_tracks.push_back(TrackedDetection{track.id, track.detection});
        }
    }

    std::sort(
        output_tracks.begin(),
        output_tracks.end(),
        [](const TrackedDetection& a, const TrackedDetection& b) {
            return a.track_id < b.track_id;
        }
    );
    return output_tracks;
}

void ByteTracker::removeDuplicateTracks() {
    for (size_t tracked_index = 0; tracked_index < tracks_.size(); ++tracked_index) {
        auto& tracked = tracks_[tracked_index];
        if (tracked.state != TrackState::Tracked) {
            continue;
        }

        for (size_t lost_index = 0; lost_index < tracks_.size(); ++lost_index) {
            auto& lost = tracks_[lost_index];
            if (lost.state != TrackState::Lost) {
                continue;
            }
            if (boxIou(tracked.detection, lost.detection) < kDuplicateIouThreshold) {
                continue;
            }

            const int tracked_life = tracked.frame_id - tracked.start_frame;
            const int lost_life = lost.frame_id - lost.start_frame;
            if (tracked_life >= lost_life) {
                markRemoved(lost);
            } else {
                markRemoved(tracked);
                break;
            }
        }
    }
}

void ByteTracker::pruneTracks() {
    for (auto& track : tracks_) {
        if (track.state == TrackState::Lost
            && frame_id_ - track.frame_id > track_buffer_) {
            markRemoved(track);
        }
    }

    tracks_.erase(
        std::remove_if(
            tracks_.begin(),
            tracks_.end(),
            [](const Track& track) {
                return track.state == TrackState::Removed;
            }
        ),
        tracks_.end()
    );
}

std::vector<TrackedDetection> ByteTracker::updateTracked(
    const std::vector<TrackedDetection>& tracked_detections
) {
    ++frame_id_;
    for (auto& track : tracks_) {
        if (track.state != TrackState::Removed) {
            predictTrack(track);
        }
        track.matched = false;
    }

    for (const auto& tracked_detection : tracked_detections) {
        auto matched_track = std::find_if(
            tracks_.begin(),
            tracks_.end(),
            [&tracked_detection](const Track& track) {
                return track.id == tracked_detection.track_id
                    && track.state != TrackState::Removed;
            }
        );
        if (matched_track == tracks_.end()) {
            continue;
        }

        updateTrack(*matched_track, tracked_detection.detection);
    }

    for (auto& track : tracks_) {
        if (track.matched || track.state != TrackState::Tracked) {
            continue;
        }
        if (track.activated) {
            markLost(track);
        } else {
            markRemoved(track);
        }
    }

    removeDuplicateTracks();
    pruneTracks();
    return currentTrackedDetections();
}

std::vector<TrackedDetection> ByteTracker::update(
    const std::vector<Detection>& detections
) {
    ++frame_id_;
    for (auto& track : tracks_) {
        if (track.state != TrackState::Removed) {
            predictTrack(track);
        }
        track.matched = false;
    }

    std::vector<Detection> high_detections;
    std::vector<Detection> low_detections;
    high_detections.reserve(detections.size());
    low_detections.reserve(detections.size());
    for (const auto& detection : detections) {
        if (detection.score >= kTrackScoreThreshold) {
            high_detections.push_back(detection);
        } else if (detection.score >= kLowScoreThreshold) {
            low_detections.push_back(detection);
        }
    }

    std::vector<size_t> active_tracks;
    std::vector<size_t> lost_tracks;
    std::vector<size_t> unconfirmed_tracks;
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i].state == TrackState::Tracked && tracks_[i].activated) {
            active_tracks.push_back(i);
        } else if (tracks_[i].state == TrackState::Tracked) {
            unconfirmed_tracks.push_back(i);
        } else if (tracks_[i].state == TrackState::Lost) {
            lost_tracks.push_back(i);
        }
    }

    std::vector<size_t> track_pool = active_tracks;
    track_pool.insert(track_pool.end(), lost_tracks.begin(), lost_tracks.end());

    std::vector<bool> high_matched(high_detections.size(), false);

    const auto first_matches = assignDetections(
        track_pool,
        high_detections,
        kFirstMatchThreshold,
        true
    );
    for (const auto& match : first_matches) {
        updateTrack(tracks_[match.track_index], high_detections[match.detection_index]);
        high_matched[match.detection_index] = true;
    }

    std::vector<size_t> unmatched_tracked_tracks;
    for (size_t track_index : active_tracks) {
        if (!tracks_[track_index].matched && tracks_[track_index].state == TrackState::Tracked) {
            unmatched_tracked_tracks.push_back(track_index);
        }
    }

    const auto second_matches = assignDetections(
        unmatched_tracked_tracks,
        low_detections,
        kSecondMatchThreshold,
        false
    );
    for (const auto& match : second_matches) {
        updateTrack(tracks_[match.track_index], low_detections[match.detection_index]);
    }

    for (size_t track_index : unmatched_tracked_tracks) {
        if (!tracks_[track_index].matched) {
            markLost(tracks_[track_index]);
        }
    }

    std::vector<Detection> remaining_high_detections;
    std::vector<size_t> remaining_high_indices;
    for (size_t i = 0; i < high_detections.size(); ++i) {
        if (!high_matched[i]) {
            remaining_high_indices.push_back(i);
            remaining_high_detections.push_back(high_detections[i]);
        }
    }

    std::vector<bool> unconfirmed_matched(unconfirmed_tracks.size(), false);
    const auto unconfirmed_matches = assignDetections(
        unconfirmed_tracks,
        remaining_high_detections,
        kUnconfirmedMatchThreshold,
        true
    );
    for (const auto& match : unconfirmed_matches) {
        updateTrack(tracks_[match.track_index], remaining_high_detections[match.detection_index]);
        const auto matched_unconfirmed = std::find(
            unconfirmed_tracks.begin(),
            unconfirmed_tracks.end(),
            match.track_index
        );
        if (matched_unconfirmed != unconfirmed_tracks.end()) {
            unconfirmed_matched[static_cast<size_t>(
                matched_unconfirmed - unconfirmed_tracks.begin()
            )] = true;
        }
        high_matched[remaining_high_indices[match.detection_index]] = true;
    }

    for (size_t i = 0; i < unconfirmed_tracks.size(); ++i) {
        if (!unconfirmed_matched[i] && !tracks_[unconfirmed_tracks[i]].matched) {
            markRemoved(tracks_[unconfirmed_tracks[i]]);
        }
    }

    for (size_t i = 0; i < high_detections.size(); ++i) {
        if (high_matched[i] || high_detections[i].score < kNewTrackThreshold) {
            continue;
        }
        tracks_.push_back(createTrack(high_detections[i]));
    }

    removeDuplicateTracks();
    pruneTracks();
    return currentTrackedDetections();
}

}  // namespace yolo
