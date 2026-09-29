"""Inspect a fixed prefix of the ongoing soak; this is not final acceptance."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STREAM = re.compile(r'yolo_stream_(processed_frames|low_detections|high_detections)_by_stream_total\{stream_id="([^"]+)"\}')


def parse_sample(sample: dict) -> dict:
    streams, values = {}, {}
    for line in sample["metrics"]["prometheus_text"].splitlines():
        if not line or line.startswith("#"):
            continue
        key, raw = line.rsplit(" ", 1)
        match = STREAM.fullmatch(key)
        if match:
            streams.setdefault(match[2], {})[match[1]] = int(raw)
        elif "stream_id=" not in key and key.startswith(
                ("yolo_stream_async_", "yolo_stream_processing_stage_")):
            values[key] = float(raw)
    return {"time": sample["metrics"]["monotonic_seconds"], "streams": streams,
            "global": values, "rss_kib": sample["memory"]["Rss_kib"]}


def window(start: dict, end: dict, origin: float) -> dict:
    duration = end["time"] - start["time"]
    assert duration > 0 and start["streams"].keys() == end["streams"].keys()
    rates = {}
    for sid, initial in start["streams"].items():
        rates[sid] = {}
        for kind, before in initial.items():
            difference = end["streams"][sid][kind] - before
            assert difference >= 0
            rates[sid][kind] = difference / duration
    ranges = {key: [min(r[key] for r in rates.values()), max(r[key] for r in rates.values())]
              for key in ("processed_frames", "low_detections", "high_detections")}
    slow, fast = ranges["processed_frames"]
    fairness = 100 * (fast - slow) / fast if fast else None

    def delta(key: str) -> float:
        value = end["global"][key] - start["global"][key]
        assert math.isfinite(value) and value >= 0
        return value

    def mean(prefix: str, labels: str) -> float | None:
        count = delta(prefix + "_count" + labels)
        return 1000 * delta(prefix + "_sum" + labels) / count if count else None

    return {
        "from_sample_seconds": start["time"] - origin,
        "to_sample_seconds": end["time"] - origin, "duration_seconds": duration,
        "fps_ranges": ranges, "fairness_percent": fairness,
        "diagnostic_rate_checks": {
            "output_ge_25": slow >= 25, "low_ge_4": ranges["low_detections"][0] >= 4,
            "high_ge_0_5": ranges["high_detections"][0] >= 0.5,
            "fairness_le_10": fairness is not None and fairness <= 10,
        },
        "rss_start_kib": start["rss_kib"], "rss_end_kib": end["rss_kib"],
        "weak_flow_mean_ms": mean("yolo_stream_processing_stage_seconds", '{stage="weak_flow"}'),
        "prepare_mean_ms": mean("yolo_stream_processing_stage_seconds", '{stage="prepare"}'),
        "inference": {
            tier: {"mean_ms": mean("yolo_stream_async_stage_seconds", f'{{tier="{tier}",stage="infer"}}'),
                   "applied": int(delta(f'yolo_stream_async_results_total{{tier="{tier}",outcome="applied"}}')),
                   "expired": int(delta(f'yolo_stream_async_results_total{{tier="{tier}",outcome="expired"}}'))}
            for tier in ("high", "low")
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", type=int, required=True)
    args = parser.parse_args()
    assert args.samples > 1
    path = ROOT / "diag63_six_soak_8h/live_samples.jsonl"
    digest = hashlib.sha256()
    windows = []
    first_full = first_steady = start = previous_time = last = None
    peak_rss = count = 0
    with path.open("rb") as journal:
        for raw in journal:
            if count == args.samples:
                break
            assert raw.endswith(b"\n"), "requested prefix includes an incomplete record"
            sample = json.loads(raw)
            count += 1
            assert sample["index"] == count
            digest.update(raw)
            current = parse_sample(sample)
            assert previous_time is None or current["time"] > previous_time
            previous_time = current["time"]
            peak_rss = max(peak_rss, current["rss_kib"])
            if len(current["streams"]) != 6:
                assert first_full is None, "stream disappeared during inspected prefix"
                continue
            assert all(set(row) == {"processed_frames", "low_detections", "high_detections"}
                       for row in current["streams"].values())
            if first_full is None:
                first_full = current["time"]
            # Approximate steady start, distinct from the acceptance collector's boundary.
            if current["time"] - first_full < 60:
                continue
            if start is None:
                start = first_steady = current
            elif current["time"] - start["time"] >= 300:
                windows.append(window(start, current, first_full))
                start = current
            last = current
    assert count == args.samples and first_steady is not None and last is not None
    print(json.dumps({
        "scope": "fixed prefix; five-minute rate diagnostics, not final acceptance or CPU/P95 gates",
        "samples": count, "journal_prefix_sha256": digest.hexdigest(),
        "last_monotonic_seconds": last["time"],
        "elapsed_since_first_full_stream_sample": last["time"] - first_full,
        "steady_start": "first six-stream metric sample plus at least 60 seconds; approximate",
        "peak_rss_kib_including_startup": peak_rss, "complete_windows": len(windows),
        "failed_window_counts": {key: sum(not row["diagnostic_rate_checks"][key] for row in windows)
                                 for key in ("output_ge_25", "low_ge_4", "high_ge_0_5", "fairness_le_10")},
        "observed_total": window(first_steady, last, first_full), "windows": windows,
        "unfinished_tail": window(start, last, first_full) if last["time"] > start["time"] else None,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
