#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import evaluate_bdd100k_tracking as evaluation  # noqa: E402


class TrackingContinuityTest(unittest.TestCase):
    def test_visibility_gap_splits_miss_runs_without_false_fragment(self) -> None:
        record = evaluation.ObjectRecord
        labels = {
            frame: [record(frame, "car-a", "car", (frame, 0, frame + 10, 10))]
            for frame in (0, 1, 3, 4, 5)
        }
        predictions = {
            0: [record(0, "10", "car", (0, 0, 10, 10), 0.9)],
            3: [record(3, "10", "car", (3, 0, 13, 10), 0.9)],
            5: [record(5, "10", "car", (5, 0, 15, 10), 0.9)],
        }

        tracking = evaluation.tracking_metrics(labels, predictions, 0.5, 5.0)

        self.assertEqual(tracking["false_negatives"], 2)
        self.assertEqual(tracking["miss_duration"]["event_count"], 2)
        self.assertEqual(
            tracking["miss_duration"]["missed_ground_truth_observations"], 2
        )
        self.assertEqual(tracking["miss_duration"]["max_ms"], 200.0)
        self.assertEqual(tracking["fragments"], 1)


if __name__ == "__main__":
    unittest.main()
