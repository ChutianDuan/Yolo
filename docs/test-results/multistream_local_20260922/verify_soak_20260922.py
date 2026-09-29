"""Read-only live-process checks and completed local-soak evidence audit."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PRIOR = ROOT.parent / "multistream_local_20260921/diag62_B2_cache_60s"
BINARY_SHA = "44286a8410d69b909c6ec4b6bedf12e50e59f65f65345325b631868c3bad1012"
PREFIXES = ("yolo_stream_async_", "yolo_scheduler_updated_total",
            "yolo_scheduler_cancelled_total", "yolo_stream_processing_stage_",
            "yolo_stream_weak_flow_")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text())


def owned_alive(pid: int, start_ticks: int) -> bool:
    try:
        fields = (Path("/proc") / str(pid) / "stat").read_text().rsplit(") ", 1)[1].split()
    except FileNotFoundError:
        return False
    return fields[0] != "Z" and int(fields[19]) == start_ticks


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("case", choices=("diag63_journal_smoke_60s", "diag63_six_soak_8h"))
    parser.add_argument("--live", action="store_true")
    args = parser.parse_args()
    case = ROOT / args.case
    state = read_json(case / "run_state.json")
    progress = read_json(case / "progress.json")
    duration = 60 if args.case == "diag63_journal_smoke_60s" else 28800
    warmup = 10 if duration == 60 else 60
    assert state["requested_seconds"] == duration and state["warmup_seconds"] == warmup
    assert state["sample_interval_seconds"] == 5
    assert state["service_binary_sha256"] == BINARY_SHA
    binary = Path(state["service_binary"])
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == BINARY_SHA
    alive = {name: owned_alive(state[name + "_pid"], state[name + "_start_ticks"])
             for name in ("driver", "service", "collector")}
    if args.live:
        assert state["state"] == "collecting" and all(alive.values()), (state["state"], alive)
        assert (Path("/proc") / str(state["service_pid"]) / "exe").resolve() == binary.resolve()
        listeners = subprocess.check_output(["ss", "-ltnp", "sport = :8080"], text=True)
        assert {int(pid) for pid in re.findall(r"pid=(\d+)", listeners)} == {state["service_pid"]}
        age = time.monotonic() - progress["metrics_monotonic_seconds"]
        assert 0 <= age < 30, "durable sampler is not advancing recently"
        print(json.dumps({"case": args.case, "state": "RUNNING", "owned_processes_alive": alive,
                          "service_pid": state["service_pid"],
                          "collection_started_at": state["collection_started_at"],
                          "requested_seconds": duration, "sample_count": progress["sample_count"],
                          "latest_sample_age_seconds": age, "memory": progress["memory"]}))
        return

    assert state["state"] == "finished" and not any(alive.values())
    assert state["service_returncode"] == 0 and state["run_error"] is None
    manifest = read_json(case / "diagnostic.json")
    timing = read_json(case / "timing_metrics.json")
    report = read_json(case / "acceptance/acceptance.json")
    prior_manifest = read_json(PRIOR / "diagnostic.json")
    prior_report = read_json(PRIOR / "acceptance/acceptance.json")
    assert (case / "diagnostic-config.yaml").read_bytes() == (PRIOR / "diagnostic-config.yaml").read_bytes()
    assert manifest["runtime_records"] == prior_manifest["runtime_records"]
    assert manifest["inputs"] == prior_manifest["inputs"]
    assert manifest["distinct_source_contents"] == 4 and manifest["source_content_reused"]
    assert len(manifest["source_connections"]) == manifest["stream_count"] == 6
    assert manifest["service_binary_sha256"] == manifest["service_binary_sha256_after"] == BINARY_SHA
    assert manifest["actual_executable_verified"] and manifest["server_returncode"] == 0
    assert manifest["requested_seconds"] == duration and manifest["requested_warmup_seconds"] == warmup
    assert manifest["sample_interval_seconds"] == 5
    assert report["requested_stream_count"] == len(report["streams"]) == 6
    assert report["requested_duration_seconds"] == duration and report["elapsed_seconds"] >= duration
    assert report["sampling_warmup"]["completed"] and not report["sampling_warmup"]["errors"]
    assert report["thresholds"] == prior_report["thresholds"] and not report["run_errors"]
    assert all(item["unchanged"] and item["sha256"] == item["sha256_after"]
               for item in report["provenance"]["artifacts"])
    assert [a["sha256"] for a in report["provenance"]["artifacts"] if a["kind"] == "model"] == [
        a["sha256"] for a in prior_report["provenance"]["artifacts"] if a["kind"] == "model"]
    host = report["provenance"]["collector_host"]
    prior_host = prior_report["provenance"]["collector_host"]
    assert all(host.get(k) == prior_host.get(k) for k in (
        "cpu_models", "logical_cpus", "physical_cores", "sockets",
        "collector_cpu_affinity", "monitored_pid_cpu_affinity"))
    assert timing["samples_file"] == state["samples_file"] == "live_samples.jsonl"
    assert not timing["errors"] and not manifest["timing_metrics_errors"]

    previous = {}
    previous_stream = {}
    previous_time = None
    first_time = None
    maximum_gap = 0.0
    peak_rss = 0
    peak_anonymous = 0
    count = 0
    selected = None
    with (case / "live_samples.jsonl").open() as journal:
        for line in journal:
            sample = json.loads(line)
            count += 1
            assert sample["index"] == count
            metric, memory = sample["metrics"], sample["memory"]
            now = metric["monotonic_seconds"]
            assert math.isfinite(now) and 0 <= memory["monotonic_seconds"] - now < 3
            if first_time is None:
                first_time = now
            if previous_time is not None:
                assert now > previous_time
                maximum_gap = max(maximum_gap, now - previous_time)
            previous_time = now
            assert all(type(v) is int and v >= 0 for k, v in memory.items()
                       if k != "monotonic_seconds")
            peak_rss = max(peak_rss, memory["Rss_kib"])
            peak_anonymous = max(peak_anonymous, memory["Anonymous_kib"])
            global_values = {}
            for row in metric["prometheus_text"].splitlines():
                if not row or row.startswith("#"):
                    continue
                key, raw = row.rsplit(" ", 1)
                value = float(raw)
                if "stream_id=" not in key:
                    global_values[key] = value
                elif key.startswith(PREFIXES):
                    assert math.isfinite(value) and value >= previous_stream.get(key, 0)
                    previous_stream[key] = value
            if selected is None:
                selected = [key for key in global_values if key.startswith(PREFIXES)]
                assert len(selected) == 111
            assert all(math.isfinite(global_values[key])
                       and global_values[key] >= previous.get(key, 0) for key in selected)
            previous = global_values
    assert count == timing["sample_count"] == manifest["memory_sample_count"] == state["samples"]
    assert count == manifest["timing_metrics_sample_count"] == progress["sample_count"]
    assert count >= duration / 10 and previous_time - first_time >= duration
    assert previous["yolo_streams_active"] == previous["yolo_streams_registered"] == 0
    for tier in ("high", "low"):
        for kind in ("queue_depth", "in_flight"):
            assert previous[f'yolo_scheduler_{kind}{{tier="{tier}"}}'] == 0
    assert not any(row["inference_errors_max"] or row["reconnects_delta"]
                   for row in report["streams"])
    all_passed = all(gate["passed"] is True for gate in report["gates"])
    assert report["overall_status"] == ("PASS" if all_passed else "FAIL")
    assert state["collector_returncode"] == manifest["collector_returncode"] == (0 if all_passed else 2)
    print(json.dumps({
        "case": args.case, "purpose": "local MJPEG soak; not real-camera RTSP or quality acceptance",
        "status": report["overall_status"], "duration_seconds": report["elapsed_seconds"],
        "acceptance_samples": report["sample_count"], "durable_samples": count,
        "selected_global_monotonic_series": len(selected),
        "metrics_maximum_gap_seconds": maximum_gap,
        "failed_gates": [g["name"] for g in report["gates"] if g["passed"] is not True],
        "output_fps": [min(s["processed_fps"] for s in report["streams"]),
                       max(s["processed_fps"] for s in report["streams"])],
        "low_fps": [min(s["low_detection_fps"] for s in report["streams"]),
                    max(s["low_detection_fps"] for s in report["streams"])],
        "high_fps": [min(s["high_detection_fps"] for s in report["streams"]),
                     max(s["high_detection_fps"] for s in report["streams"])],
        "maximum_p95_ms": max(s["result_age_ms_p95"] for s in report["streams"]),
        "cpu_host_percent": report["process"]["cpu_average_percent_of_host"],
        "rss_growth_percent": report["process"]["rss_growth_percent"],
        "fairness_percent": report["fairness_spread_percent"],
        "peak_rss_kib": peak_rss, "peak_anonymous_kib": peak_anonymous,
        "service_binary_sha256": BINARY_SHA, "owned_processes_alive": alive,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
