#!/usr/bin/env python3

from __future__ import annotations

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import evaluate_bdd100k_tracking as evaluation  # noqa: E402
import prepare_bdd100k_trackeval as trackeval_export  # noqa: E402


def label_row(
    frame_index: int,
    object_id: str,
    category: str,
    box: tuple[float, float, float, float],
    crowd: bool = False,
) -> dict[str, str]:
    return {
        "name": f"sample-{frame_index + 1:07d}.jpg",
        "videoName": "sample",
        "frameIndex": str(frame_index),
        "id": object_id,
        "category": category,
        "attributes.crowd": str(crowd),
        "attributes.occluded": "False",
        "attributes.truncated": "False",
        "box2d.x1": str(box[0]),
        "box2d.x2": str(box[2]),
        "box2d.y1": str(box[1]),
        "box2d.y2": str(box[3]),
        "haveVideo": "True",
    }


def track(
    track_id: int,
    class_name: str,
    box: tuple[float, float, float, float],
    score: float,
) -> dict[str, object]:
    return {
        "track_id": track_id,
        "class_name": class_name,
        "score": score,
        "box": {"x1": box[0], "y1": box[1], "x2": box[2], "y2": box[3]},
    }


class EvaluationTest(unittest.TestCase):
    def test_hungarian_maximum_assignment(self) -> None:
        self.assertEqual(evaluation.maximum_assignment_weight([[9, 8], [7, 1]]), 15)
        self.assertEqual(evaluation.maximum_assignment_weight([[3], [5]]), 5)
        self.assertEqual(evaluation.maximum_assignment_weight([]), 0)

    def test_frame_stride_alignment_detection_and_identity_metrics(self) -> None:
        fieldnames = [
            "name",
            "videoName",
            "frameIndex",
            "id",
            "category",
            "attributes.crowd",
            "attributes.occluded",
            "attributes.truncated",
            "box2d.x1",
            "box2d.x2",
            "box2d.y1",
            "box2d.y2",
            "haveVideo",
        ]
        rows = [
            label_row(0, "car-a", "car", (0, 0, 10, 10)),
            label_row(1, "car-a", "car", (1, 0, 11, 10)),
            label_row(2, "car-a", "car", (2, 0, 12, 10)),
            label_row(0, "person-a", "pedestrian", (20, 0, 30, 10)),
            label_row(1, "person-a", "pedestrian", (20, 0, 30, 10)),
            label_row(1, "crowd", "car", (50, 0, 60, 10), crowd=True),
            label_row(1, "unsupported", "other vehicle", (70, 0, 80, 10)),
            label_row(3, "crowd-only", "car", (50, 0, 60, 10), crowd=True),
        ]
        payload = {
            "source_fps": 30.0,
            "processed_frame_count": 13,
            "frames": [
                {
                    "frame_index": 0,
                    "timestamp_ms": 0.0,
                    "tracks": [
                        track(10, "car", (0, 0, 10, 10), 0.9),
                        track(20, "person", (20, 0, 30, 10), 0.8),
                        track(99, "traffic sign", (40, 0, 50, 10), 0.95),
                    ],
                },
                {
                    "frame_index": 6,
                    "timestamp_ms": 200.0,
                    "tracks": [
                        track(10, "car", (2, 0, 12, 10), 0.85),
                        track(20, "person", (40, 0, 50, 10), 0.7),
                    ],
                },
                {
                    "frame_index": 12,
                    "timestamp_ms": 400.0,
                    "tracks": [track(11, "car", (2, 0, 12, 10), 0.88)],
                },
                {
                    "frame_index": 18,
                    "timestamp_ms": 600.0,
                    "tracks": [],
                },
            ],
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            labels_path = root / "mot_labels.csv"
            with labels_path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(rows)
            prediction_path = root / "prediction.json"
            prediction_path.write_text(json.dumps(payload), encoding="utf-8")

            labels, label_metadata = evaluation.load_labels(labels_path, "sample")
            loaded_payload, prediction_metadata = evaluation.load_prediction(prediction_path)
            metrics = evaluation.evaluate_payload(loaded_payload, labels, 6, 5.0, 0.5, 1)

        self.assertEqual(label_metadata["selected_rows"], 8)
        self.assertEqual(label_metadata["ignored_crowd"], 2)
        self.assertEqual(label_metadata["ignored_unsupported"], {"other vehicle": 1})
        self.assertEqual(prediction_metadata["response_frame_count"], 4)
        self.assertEqual(metrics["alignment"]["unique_response_frames"], 4)
        self.assertEqual(metrics["alignment"]["frame_stride"], 6)
        self.assertEqual(metrics["alignment"]["skipped_tail_label_frames"], [])

        detection = metrics["detection"]
        self.assertEqual(detection["ground_truth"], 5)
        self.assertEqual(detection["predictions"], 5)
        self.assertEqual(detection["true_positives"], 4)
        self.assertEqual(detection["false_positives"], 1)
        self.assertEqual(detection["false_negatives"], 1)
        self.assertEqual(detection["precision"], 0.8)
        self.assertEqual(detection["recall"], 0.8)
        self.assertAlmostEqual(detection["map50"], 0.752475, places=6)

        tracking = metrics["tracking"]
        self.assertEqual(tracking["id_switches"], 1)
        self.assertEqual(tracking["fragments"], 0)
        self.assertEqual(tracking["idtp"], 3)
        self.assertEqual(tracking["idfp"], 2)
        self.assertEqual(tracking["idfn"], 2)
        self.assertEqual(tracking["idf1"], 0.6)
        self.assertEqual(tracking["mota"], 0.4)
        self.assertEqual(tracking["miss_duration"]["event_count"], 1)
        self.assertEqual(tracking["miss_duration"]["missed_ground_truth_observations"], 1)
        self.assertEqual(tracking["miss_duration"]["p95_ms"], 200.0)
        jitter = tracking["localization_jitter"]
        self.assertEqual(jitter["sample_count"], 1)
        self.assertEqual(jitter["mean_error_delta_px"], 1.0)
        self.assertAlmostEqual(jitter["mean_normalized_error_delta"], 0.070711, places=6)

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

    def test_trackeval_export_preserves_ignore_labels_and_alignment(self) -> None:
        rows = [
            label_row(0, "1", "car", (0, 0, 10, 10)),
            label_row(1, "2", "other vehicle", (20, 0, 30, 10)),
            label_row(1, "3", "car", (40, 0, 50, 10), crowd=True),
            label_row(2, "4", "car", (60, 0, 70, 10)),
        ]
        payload = {
            "source_fps": 30.0,
            "processed_frame_count": 7,
            "frames": [
                {
                    "frame_index": 0,
                    "timestamp_ms": 0.0,
                    "tracks": [track(9, "person", (0, 0, 10, 10), 0.8)],
                },
                {
                    "frame_index": 6,
                    "timestamp_ms": 200.0,
                    "tracks": [track(10, "car", (0, 0, 10, 10), 0.9)],
                },
            ],
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            labels_path = root / "mot_labels.csv"
            with labels_path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            prediction_path = root / "prediction.json"
            prediction_path.write_text(json.dumps(payload), encoding="utf-8")
            report = {
                "schema_version": 2,
                "video_name": "sample",
                "frame_stride": 6,
                "max_missing_tail_label_frames": 1,
                "runs": [
                    {
                        "name": "full_high",
                        "prediction": {
                            "path": str(prediction_path),
                            "sha256": evaluation.sha256_file(prediction_path),
                        },
                        "metrics": {
                            "alignment": {"skipped_tail_label_frames": [2]}
                        },
                    }
                ],
            }
            report_path = root / "evaluation.json"
            report_path.write_text(json.dumps(report), encoding="utf-8")
            output = root / "trackeval-export"

            manifest_path = trackeval_export.build_export(
                labels_path, [report_path], output
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            ground_truth = json.loads(
                (output / "gt" / "sample.json").read_text(encoding="utf-8")
            )
            tracker_frames = json.loads(
                (output / "trackers" / "full_high" / "data" / "sample.json").read_text(
                    encoding="utf-8"
                )
            )

        self.assertEqual(manifest["videos"]["sample"]["evaluated_label_frames"], 2)
        self.assertEqual(manifest["videos"]["sample"]["skipped_tail_label_frames"], [2])
        self.assertEqual(len(ground_truth), 2)
        self.assertEqual(
            [item["category"] for item in ground_truth[1]["labels"]],
            ["other vehicle", "car"],
        )
        self.assertTrue(ground_truth[1]["labels"][1]["attributes"]["Crowd"])
        self.assertEqual(len(tracker_frames), 2)
        self.assertEqual(tracker_frames[0]["labels"][0]["category"], "pedestrian")
        self.assertEqual(tracker_frames[1]["labels"][0]["category"], "car")

    def test_output_directory_must_be_new(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "new-report"
            self.assertEqual(evaluation.create_output_directory(output), output.resolve())
            with self.assertRaises(evaluation.EvaluationError):
                evaluation.create_output_directory(output)

    def test_alignment_rejects_missing_interior_frame(self) -> None:
        labels = {1: [evaluation.ObjectRecord(1, "a", "car", (0, 0, 1, 1))]}
        predictions = [(0.0, 0, []), (400.0, 12, [])]
        with self.assertRaises(evaluation.EvaluationError):
            evaluation.align_frames(labels, predictions, 6, 1)
        duplicate_frames = [(0.0, 0, []), (1.0, 0, [])]
        with self.assertRaises(evaluation.EvaluationError):
            evaluation.align_frames({0: labels[1]}, duplicate_frames, 6, 1)


if __name__ == "__main__":
    unittest.main()
