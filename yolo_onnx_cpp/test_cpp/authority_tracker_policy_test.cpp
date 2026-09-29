#include <cmath>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

#include "video/video_inference_high_low.cpp"

namespace {

using yolo::AuthorityTracker;
using yolo::Detection;
using yolo::HighResRegion;
using yolo::ProjectedTrack;
using yolo::ReplayFrame;
using yolo::TrackMotion;
using yolo::TrackedDetection;
using yolo::applyReplayFrame;
using yolo::trimReplayBuffer;

void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void expectNear(float actual, float expected, const std::string& message) {
    if (std::fabs(actual - expected) > 0.001F) {
        fail(message);
    }
}

Detection makeDetection(
    float x1 = 10.0F,
    float y1 = 20.0F,
    float x2 = 80.0F,
    float y2 = 120.0F,
    int class_id = 2,
    float score = 0.80F
) {
    Detection detection;
    detection.class_id = class_id;
    detection.score = score;
    detection.x1 = x1;
    detection.y1 = y1;
    detection.x2 = x2;
    detection.y2 = y2;
    return detection;
}

ProjectedTrack directProjection(int track_id, const Detection& detection) {
    return ProjectedTrack{TrackedDetection{track_id, detection}, true};
}

void testProvisionalKeepsIdWhenHighResConfirmsAndCorrectsClass() {
    AuthorityTracker tracker;
    const Detection low = makeDetection();

    auto output = tracker.updateLowRes({low}, {}, 0);
    expect(output.empty(), "single low-res hit exposed a provisional");

    output = tracker.updateLowRes({low}, {}, 1);
    expect(output.size() == 1, "repeated low-res hits did not expose provisional");
    const int provisional_id = output.front().track_id;
    expect(provisional_id > 0, "provisional did not reserve its final positive id");

    const Detection high = makeDetection(11.0F, 21.0F, 81.0F, 121.0F, 4, 0.94F);
    output = tracker.updateHighRes({high}, 2);

    expect(output.size() == 1, "high-res confirmation created duplicate tracks");
    expect(output.front().track_id == provisional_id, "promotion changed the track id");
    expect(output.front().detection.class_id == 4, "high-res did not correct provisional class");
    expectNear(output.front().detection.score, 0.94F, "high-res score was not authoritative");
}

void testRejectedProvisionalIsRemoved() {
    AuthorityTracker tracker;
    tracker.updateLowRes({makeDetection()}, {}, 0);
    auto output = tracker.updateLowRes({makeDetection()}, {}, 1);
    expect(output.size() == 1, "test setup did not expose provisional");

    output = tracker.updateHighRes({}, 2);
    expect(output.empty(), "high-res rejection did not remove provisional");
}

void testFlowOnlyDoesNotCreateTrack() {
    AuthorityTracker tracker;
    const std::vector<ProjectedTrack> projected = {
        directProjection(7, makeDetection())
    };

    const auto output = tracker.updateFlow(projected, 1);
    expect(output.empty(), "flow-only update created a track");
}

void testLowResCannotClearHighResMisses() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    expect(output.size() == 1, "high-res did not create stable track");

    for (int miss = 1; miss <= 3; ++miss) {
        output = tracker.updateHighRes({}, miss * 12);
        if (miss < 3) {
            expect(output.size() == 1, "track was deleted before high-res miss tolerance");
            for (int low_hit = 1; low_hit <= 3; ++low_hit) {
                output = tracker.updateLowRes(
                    {makeDetection()},
                    {},
                    miss * 12 + low_hit
                );
            }
            expect(output.size() == 1, "low-res observation unexpectedly removed track");
        }
    }

    expect(output.empty(), "low-res observations cleared authoritative high-res misses");
}

void testFlowCannotKeepTrackPastAuthorityTtl() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    expect(output.size() == 1, "test setup did not create stable track");
    const int track_id = output.front().track_id;

    for (int frame = 1; frame <= 91; ++frame) {
        output = tracker.updateFlow(
            {directProjection(track_id, makeDetection())},
            frame
        );
    }

    expect(output.empty(), "flow refreshed the detector authority lifetime");
}

void testLowResOnlyUpdatesGeometry() {
    AuthorityTracker tracker;
    const Detection high = makeDetection(10.0F, 20.0F, 80.0F, 120.0F, 2, 0.91F);
    auto output = tracker.updateHighRes({high}, 0);
    const int track_id = output.front().track_id;

    const Detection low = makeDetection(20.0F, 30.0F, 90.0F, 130.0F, 2, 0.99F);
    output = tracker.updateLowRes({low}, {}, 1);

    expect(output.size() == 1 && output.front().track_id == track_id,
           "low-res geometry update changed identity");
    expect(output.front().detection.class_id == 2, "low-res changed authoritative class");
    expectNear(output.front().detection.score, 0.91F, "low-res changed authoritative score");
    expect(output.front().detection.x1 > high.x1, "low-res geometry was not fused");
    expect(output.front().detection.x1 < low.x1, "low-res geometry fully replaced authority");
}

void testLowResClassConflictDoesNotCreateDuplicate() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    const int track_id = output.front().track_id;
    const Detection wrong_class = makeDetection(10.0F, 20.0F, 80.0F, 120.0F, 6, 0.99F);

    output = tracker.updateLowRes({wrong_class}, {}, 1);
    output = tracker.updateLowRes({wrong_class}, {}, 2);

    expect(output.size() == 1, "low-res class conflict created a duplicate provisional");
    expect(output.front().track_id == track_id, "low-res class conflict changed identity");
    expect(output.front().detection.class_id == 2,
           "low-res class conflict changed authoritative class");
}

void testHighResCorrectsStableClassWithoutChangingId() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    const int track_id = output.front().track_id;

    output = tracker.updateHighRes(
        {makeDetection(10.0F, 20.0F, 80.0F, 120.0F, 5, 0.93F)},
        24
    );

    expect(output.size() == 1, "high-res class correction created duplicate stable track");
    expect(output.front().track_id == track_id, "high-res class correction changed id");
    expect(output.front().detection.class_id == 5, "high-res did not correct stable class");
}

void testSeparatedLowResHitsAreNotConsecutive() {
    AuthorityTracker tracker;
    auto output = tracker.updateLowRes({makeDetection()}, {}, 0);
    expect(output.empty(), "single low-res hit exposed provisional");

    output = tracker.updateLowRes({}, {}, 6);
    expect(output.empty(), "missing low-res observation exposed provisional");

    output = tracker.updateLowRes({makeDetection()}, {}, 12);
    expect(output.empty(), "non-consecutive low-res hits exposed provisional");
}

void testLowConfidenceProvisionalNeedsThreeHitsAndStopsAfterTwoMisses() {
    AuthorityTracker tracker;
    const Detection low_score = makeDetection(10.0F, 20.0F, 80.0F, 120.0F, 2, 0.50F);

    auto output = tracker.updateLowRes({low_score}, {}, 0);
    expect(output.empty(), "first low-confidence hit exposed provisional");
    output = tracker.updateLowRes({low_score}, {}, 1);
    expect(output.empty(), "second low-confidence hit exposed provisional");
    output = tracker.updateLowRes({low_score}, {}, 2);
    expect(output.size() == 1, "third low-confidence hit did not expose provisional");

    output = tracker.updateLowRes({}, {}, 3);
    expect(output.size() == 1, "one low-res miss stopped provisional too early");
    output = tracker.updateLowRes({}, {}, 4);
    expect(output.empty(), "two low-res misses did not stop provisional output");
    tracker.updateFlow({}, 17);
    expect(tracker.diagnostics().provisional_expired_count > 0,
           "stale provisional was not expired after twelve frames");
}

void testStableTracksConsolidateAfterOneHighResConfirmation() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes(
        {
            makeDetection(10.0F, 20.0F, 80.0F, 120.0F),
            makeDetection(70.0F, 20.0F, 140.0F, 120.0F)
        },
        0
    );
    expect(output.size() == 2, "test setup did not create two stable tracks");

    output = tracker.updateHighRes(
        {makeDetection(40.0F, 20.0F, 110.0F, 120.0F, 2, 0.95F)},
        1
    );
    expect(output.size() == 1, "overlapping stable states were not consolidated");
    expect(tracker.diagnostics().stable_stable_duplicate_count > 0,
           "stable consolidation was not diagnosed");
}

void testLowResAreaJumpIsRejected() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    const float original_x1 = output.front().detection.x1;
    const Detection inflated = makeDetection(
        -5.0F,
        7.0F,
        95.0F,
        133.0F,
        2,
        0.90F
    );

    output = tracker.updateLowRes({inflated}, {}, 1);
    expect(output.size() == 1, "area jump removed the stable track");
    expectNear(output.front().detection.x1, original_x1,
               "area jump changed stable geometry");
    expect(tracker.diagnostics().low_res_geometry_rejection_count == 1,
           "area jump rejection was not diagnosed");
}

void testFlowOnlyOutputAndRetentionTtl() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    const int track_id = output.front().track_id;

    for (int frame = 1; frame <= 16; ++frame) {
        output = tracker.updateFlow(
            {directProjection(track_id, makeDetection())},
            frame
        );
    }
    expect(output.size() == 1, "flow-only track stopped before sixteen frames");
    output = tracker.updateFlow({directProjection(track_id, makeDetection())}, 17);
    expect(output.empty(), "flow-only track remained visible after sixteen frames");
    tracker.updateFlow({directProjection(track_id, makeDetection())}, 25);
    expect(tracker.diagnostics().flow_age_expired_count > 0,
           "flow-only track was retained after twenty-four frames");
}

void testBoundaryClippingStopsTrackAfterTwoFrames() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    const int track_id = output.front().track_id;
    ProjectedTrack exiting = directProjection(track_id, makeDetection());
    exiting.boundary_clipped = true;
    exiting.visible_area_ratio = 0.75;

    output = tracker.updateFlow({exiting}, 1);
    expect(output.size() == 1, "first clipped frame stopped track too early");
    output = tracker.updateFlow({exiting}, 2);
    expect(output.empty(), "second clipped frame did not stop exiting track");
}

void testCenterOutsideDeletesTrackImmediately() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    ProjectedTrack outside = directProjection(output.front().track_id, makeDetection());
    outside.center_outside = true;
    outside.boundary_clipped = true;
    outside.visible_area_ratio = 0.0;

    output = tracker.updateFlow({outside}, 1);
    expect(output.empty(), "center-outside track was not deleted immediately");
    expect(tracker.diagnostics().flow_age_expired_count == 1,
           "center-outside deletion was not diagnosed");
}

void testReplayAppliesMotionToCorrectedGeometry() {
    AuthorityTracker tracker;
    const Detection corrected = makeDetection(100.0F, 20.0F, 170.0F, 120.0F);
    auto output = tracker.updateHighRes({corrected}, 0);
    const int track_id = output.front().track_id;

    ReplayFrame frame;
    frame.frame_index = 1;
    frame.image_width = 640;
    frame.image_height = 384;
    frame.direct_motions.push_back(TrackMotion{
        track_id,
        5.0F,
        0.0F,
        135.0F,
        70.0F,
        70.0F,
        100.0F
    });

    output = applyReplayFrame(frame, tracker);
    expect(output.size() == 1, "replay lost corrected track");
    expectNear(output.front().detection.x1, 105.0F,
               "replay reused stale projected geometry instead of motion delta");
}

void testReplayRejectsMotionFromReusedId() {
    AuthorityTracker tracker;
    const Detection authority = makeDetection(300.0F, 20.0F, 370.0F, 120.0F);
    auto output = tracker.updateHighRes({authority}, 0);
    const int track_id = output.front().track_id;

    ReplayFrame frame;
    frame.frame_index = 1;
    frame.image_width = 640;
    frame.image_height = 384;
    frame.direct_motions.push_back(TrackMotion{
        track_id,
        20.0F,
        0.0F,
        45.0F,
        70.0F,
        70.0F,
        100.0F
    });

    output = applyReplayFrame(frame, tracker);
    expect(output.size() == 1, "motion identity gate removed authority track");
    expectNear(output.front().detection.x1, 300.0F,
               "motion from a reused id moved the wrong track");
}

void testReplayBaseAdvancesWhenWindowEvictsFrame() {
    AuthorityTracker base_tracker;
    std::deque<ReplayFrame> replay_buffer;

    ReplayFrame authority_frame;
    authority_frame.frame_index = 0;
    authority_frame.has_high_res = true;
    authority_frame.high_res_detections.push_back(makeDetection());
    replay_buffer.push_back(std::move(authority_frame));

    ReplayFrame motion_frame;
    motion_frame.frame_index = 1;
    motion_frame.image_width = 640;
    motion_frame.image_height = 384;
    motion_frame.direct_motions.push_back(TrackMotion{
        1,
        5.0F,
        0.0F,
        45.0F,
        70.0F,
        70.0F,
        100.0F
    });
    replay_buffer.push_back(std::move(motion_frame));

    trimReplayBuffer(replay_buffer, base_tracker, 1);
    expect(replay_buffer.size() == 1, "replay window did not evict oldest frame");

    const auto output = applyReplayFrame(replay_buffer.front(), base_tracker);
    expect(output.size() == 1, "eviction lost replay base tracker state");
    expectNear(output.front().detection.x1, 15.0F,
               "evicted frame was not folded into replay base tracker");
}

void testRegionalHighResOnlyRejectsTracksInsideRegion() {
    AuthorityTracker tracker;
    const Detection inside = makeDetection(10.0F, 20.0F, 80.0F, 120.0F);
    const Detection outside = makeDetection(300.0F, 20.0F, 370.0F, 120.0F, 4);
    auto output = tracker.updateHighRes({inside, outside}, 0);
    expect(output.size() == 2, "regional rejection setup did not create tracks");
    const int outside_id = output.back().track_id;
    const HighResRegion region{0.0F, 0.0F, 150.0F, 200.0F};

    tracker.updateHighResRegion({}, {}, region, 1);
    tracker.updateHighResRegion({}, {}, region, 2);
    output = tracker.updateHighResRegion({}, {}, region, 3);

    expect(output.size() == 1, "regional misses did not expire only the covered track");
    expect(output.front().track_id == outside_id,
           "regional authority incorrectly rejected an outside track");
}

void testRegionalHighResPreservesOutsideProvisional() {
    AuthorityTracker tracker;
    const Detection inside = makeDetection(10.0F, 20.0F, 80.0F, 120.0F);
    const Detection outside = makeDetection(300.0F, 20.0F, 370.0F, 120.0F, 4);
    tracker.updateLowRes({inside, outside}, {}, 0);
    auto output = tracker.updateLowRes({inside, outside}, {}, 1);
    expect(output.size() == 2, "regional provisional setup did not expose candidates");
    const int outside_id = output.back().track_id;

    output = tracker.updateHighResRegion(
        {}, {}, HighResRegion{0.0F, 0.0F, 150.0F, 200.0F}, 2
    );
    expect(output.size() == 1 && output.front().track_id == outside_id,
           "regional authority removed a provisional outside the crop");
}

void testRegionalHighResMatchesProjectedGeometry() {
    AuthorityTracker tracker;
    auto output = tracker.updateHighRes({makeDetection()}, 0);
    const int track_id = output.front().track_id;
    const Detection projected = makeDetection(110.0F, 20.0F, 180.0F, 120.0F);
    output = tracker.updateHighResRegion(
        {projected},
        {directProjection(track_id, projected)},
        HighResRegion{100.0F, 0.0F, 220.0F, 200.0F},
        1
    );
    expect(output.size() == 1 && output.front().track_id == track_id,
           "regional authority failed to match flow-projected geometry");
    expectNear(output.front().detection.x1, 110.0F,
               "regional authority did not keep full-frame detection coordinates");
}

}  // namespace

int main() {
    // Independent stream instances and replay value copies must not share track state.
    {
        AuthorityTracker first;
        AuthorityTracker second;
        const auto first_tracks = first.updateHighRes({makeDetection()}, 0);
        const auto second_tracks = second.updateHighRes(
            {makeDetection(200.0F, 20.0F, 270.0F, 120.0F, 4, 0.95F)}, 0
        );
        expect(first_tracks.front().track_id == 1 && second_tracks.front().track_id == 1,
               "stream instances shared a global track ID allocator");
        AuthorityTracker replay = first;
        replay.updateHighRes({}, 1);
        replay.updateHighRes({}, 2);
        expect(replay.updateHighRes({}, 3).empty(), "replay copy did not expire independently");
        expect(first.tracks().size() == 1 && first.tracks().front().detection.class_id == 2,
               "mutating replay state changed the original stream");
        expect(second.tracks().size() == 1 && second.tracks().front().detection.class_id == 4,
               "mutating one stream changed a different stream");
        replay = second;
        expect(replay.tracks().size() == 1 && replay.tracks().front().detection.class_id == 4,
               "replay assignment lost copied authority state");
    }
    testProvisionalKeepsIdWhenHighResConfirmsAndCorrectsClass();
    testRejectedProvisionalIsRemoved();
    testFlowOnlyDoesNotCreateTrack();
    testLowResCannotClearHighResMisses();
    testFlowCannotKeepTrackPastAuthorityTtl();
    testLowResOnlyUpdatesGeometry();
    testLowResClassConflictDoesNotCreateDuplicate();
    testHighResCorrectsStableClassWithoutChangingId();
    testSeparatedLowResHitsAreNotConsecutive();
    testLowConfidenceProvisionalNeedsThreeHitsAndStopsAfterTwoMisses();
    testStableTracksConsolidateAfterOneHighResConfirmation();
    testLowResAreaJumpIsRejected();
    testFlowOnlyOutputAndRetentionTtl();
    testBoundaryClippingStopsTrackAfterTwoFrames();
    testCenterOutsideDeletesTrackImmediately();
    testReplayAppliesMotionToCorrectedGeometry();
    testReplayRejectsMotionFromReusedId();
    testReplayBaseAdvancesWhenWindowEvictsFrame();
    testRegionalHighResOnlyRejectsTracksInsideRegion();
    testRegionalHighResPreservesOutsideProvisional();
    testRegionalHighResMatchesProjectedGeometry();
    std::cout << "authority_tracker_policy_test passed\n";
    return 0;
}
