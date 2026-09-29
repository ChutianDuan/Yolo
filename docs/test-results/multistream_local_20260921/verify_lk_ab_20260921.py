"""Read-only audit of four bounded baseline/candidate six-stream windows."""
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PRIOR = ROOT.parent / "multistream_local_20260914/diag57_six_current_binary"
assert len(sys.argv) == 2
CASE_NAME = sys.argv[1]
assert CASE_NAME in {
    "diag61_A1_baseline_60s", "diag61_B1_dedup_60s",
    "diag61_A2_baseline_60s", "diag61_B2_dedup_60s",
}
CASE = ROOT / CASE_NAME
IS_CANDIDATE = "_dedup_" in CASE_NAME
EXPECTED_BINARY = ("ddb8dc1195a4c5f438a64afc2e7984ac0d7e53221f9c75961687d8c5ea80c891"
                   if IS_CANDIDATE else
                   "6a1863dd52f2a3e427246c39f7a8642c6a802f3f450a62df6a0129cdd10298c3")
STAGES = ("frame_work", "prepare", "poll", "publish", "gray", "weak_flow", "motion",
          "projection", "frame_diff", "roi_features", "pyramid_build", "lk_forward",
          "lk_backward", "flow_quality")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text())


prior = read_json(PRIOR / "diagnostic.json")
manifest = read_json(CASE / "diagnostic.json")
report = read_json(CASE / "acceptance/acceptance.json")
timing = read_json(CASE / "timing_metrics.json")
assert (CASE / "diagnostic-config.yaml").read_bytes() == (PRIOR / "diagnostic-config.yaml").read_bytes()
assert manifest["runtime_records"] == prior["runtime_records"]
assert manifest["configured_high_threads"] == manifest["configured_low_threads"] == 16
assert manifest["configured_high_requests"] == 1 and manifest["configured_low_requests"] == 2
assert manifest["performance_mode"] == "throughput" and manifest["configured_cpu_pinning"] == "false"
assert manifest["configured_opencv_threads"] == 4 and manifest["configured_low_detect_fps"] == 5
assert manifest["requested_seconds"] == 60 and manifest["requested_warmup_seconds"] == 10
assert manifest["actual_executable_verified"] is True
assert manifest["service_binary"] == ("yolo_api" if IS_CANDIDATE else "yolo_api_baseline")
assert manifest["service_binary_sha256"] == manifest["service_binary_sha256_after"] == EXPECTED_BINARY
assert manifest["server_returncode"] == 0
assert manifest["inputs"] == prior["inputs"]
assert manifest["distinct_source_contents"] == 4 and manifest["source_content_reused"] is True
assert len(manifest["source_connections"]) == 6
assert {item["path"] for item in manifest["source_connections"]} == {
    f"/camera/{i}.mjpg" for i in range(1, 7)
}
assert report["requested_stream_count"] == 6 and len(report["streams"]) == 6
assert report["sampling_warmup"]["completed"] and not report["sampling_warmup"]["errors"]
assert report["sample_count"] == 61 and report["elapsed_seconds"] >= 60
assert not report["run_errors"] and not timing["errors"]
assert manifest["collector_returncode"] == (0 if report["overall_status"] == "PASS" else 2)
assert all(item["unchanged"] and item["sha256"] == item["sha256_after"]
           for item in report["provenance"]["artifacts"])
prior_report = read_json(PRIOR / "acceptance/acceptance.json")
models = [item["sha256"] for item in report["provenance"]["artifacts"] if item["kind"] == "model"]
prior_models = [item["sha256"] for item in prior_report["provenance"]["artifacts"]
                if item["kind"] == "model"]
assert models == prior_models
thresholds = report["thresholds"]
assert thresholds == prior_report["thresholds"]
assert (thresholds["min_processed_fps"], thresholds["min_low_detection_fps"],
        thresholds["min_high_detection_fps"], thresholds["max_result_age_ms"]) == (25, 4, .5, 300)
assert (thresholds["max_cpu_percent"], thresholds["max_memory_growth_percent"],
        thresholds["max_fairness_spread_percent"], thresholds["max_queue_depth"]) == (85, 5, 10, 2)
host_fields = ("cpu_models", "logical_cpus", "physical_cores", "sockets",
               "collector_cpu_affinity", "monitored_pid_cpu_affinity")
host = report["provenance"]["collector_host"]
prior_host = prior_report["provenance"]["collector_host"]
assert all(host.get(key) == prior_host.get(key) for key in host_fields)

samples = timing["samples"]
memory = timing["memory_samples"]
assert 65 <= len(samples) <= 85
assert len(memory) == len(samples) == manifest["memory_sample_count"]
assert all(b["monotonic_seconds"] > a["monotonic_seconds"]
           for a, b in zip(samples, samples[1:]))
allowed_memory = {f"{key}_kib" for key in ("Rss", "Pss", "Private_Clean", "Private_Dirty",
                   "Shared_Clean", "Shared_Dirty", "Anonymous", "AnonHugePages")}
for sample, mem in zip(samples, memory):
    assert set(mem) == allowed_memory | {"threads", "monotonic_seconds"}
    assert all(type(mem[key]) is int and mem[key] >= 0 for key in allowed_memory | {"threads"})
    assert math.isfinite(mem["monotonic_seconds"])
    assert 0 <= mem["monotonic_seconds"] - sample["monotonic_seconds"] < 2
assert all(b["monotonic_seconds"] > a["monotonic_seconds"]
           for a, b in zip(memory, memory[1:]))

parsed = []
for sample in samples:
    values = {}
    for line in sample["prometheus_text"].splitlines():
        if line and not line.startswith("#") and "stream_id=" not in line:
            key, value = line.rsplit(" ", 1)
            values[key] = float(value)
    parsed.append(values)
selected = [key for key in parsed[0]
            if key.startswith(("yolo_stream_async_", "yolo_scheduler_updated_total",
                               "yolo_scheduler_cancelled_total", "yolo_stream_processing_stage_",
                               "yolo_stream_weak_flow_"))]
assert len(selected) == (112 if IS_CANDIDATE else 111)
assert all(math.isfinite(row[key]) and row[key] >= 0 for row in parsed for key in selected)
assert all(b[key] >= a[key] for a, b in zip(parsed, parsed[1:]) for key in selected)
final = parsed[-1]
assert final["yolo_streams_active"] == final["yolo_streams_registered"] == 0
for tier in ("high", "low"):
    assert final[f'yolo_scheduler_queue_depth{{tier="{tier}"}}'] == 0
    assert final[f'yolo_scheduler_in_flight{{tier="{tier}"}}'] == 0

processing = {}
for name in STAGES:
    label = f'{{stage="{name}"}}'
    count = final["yolo_stream_processing_stage_seconds_count" + label]
    total = final["yolo_stream_processing_stage_seconds_sum" + label]
    maximum = final["yolo_stream_processing_stage_seconds_max" + label]
    assert count > 0 and total >= maximum >= 0
    processing[name] = {"count": int(count), "sum_ms": 1000 * total,
                        "mean_ms": 1000 * total / count, "max_ms": 1000 * maximum}
assert processing["gray"]["count"] == processing["prepare"]["count"]
assert processing["prepare"]["count"] <= processing["frame_work"]["count"]
assert processing["frame_work"]["count"] == processing["poll"]["count"]
assert processing["publish"]["count"] <= processing["prepare"]["count"]
assert all(processing[name]["count"] <= processing["gray"]["count"]
           for name in ("weak_flow", "motion", "projection"))
assert processing["motion"]["count"] <= processing["weak_flow"]["count"]
assert processing["projection"]["count"] <= processing["weak_flow"]["count"]
assert processing["prepare"]["sum_ms"] >= sum(processing[name]["sum_ms"]
    for name in ("gray", "weak_flow", "motion", "projection"))
assert processing["frame_diff"]["count"] == processing["weak_flow"]["count"]
assert processing["roi_features"]["count"] == processing["weak_flow"]["count"]
assert processing["pyramid_build"]["count"] <= processing["weak_flow"]["count"]
assert all(processing[name]["count"] == processing["pyramid_build"]["count"]
           for name in ("lk_forward", "lk_backward", "flow_quality"))
assert processing["weak_flow"]["sum_ms"] >= sum(processing[name]["sum_ms"] for name in
    ("frame_diff", "roi_features", "pyramid_build", "lk_forward", "lk_backward", "flow_quality"))
assert processing["frame_work"]["sum_ms"] >= sum(processing[name]["sum_ms"]
    for name in ("prepare", "poll", "publish"))

weak_names = ("rois", "roi_pixels", "sampled_points", "unique_points") if IS_CANDIDATE else (
    "rois", "roi_pixels", "sampled_points")
global_weak_load = {
    name: int(final[f"yolo_stream_weak_flow_{name}_total"]) for name in weak_names
}
assert global_weak_load["rois"] > 0
assert global_weak_load["roi_pixels"] >= 16 * global_weak_load["rois"]
assert global_weak_load["sampled_points"] <= 20 * global_weak_load["rois"]
if IS_CANDIDATE:
    assert global_weak_load["unique_points"] <= global_weak_load["sampled_points"]
else:
    assert "yolo_stream_weak_flow_unique_points_total" not in final

by_stream = {}
weak_by_stream = {}
for stream in report["streams"]:
    sid = stream["stream_id"]
    labels = {name: f'yolo_stream_processing_stage_by_stream_seconds_count{{stage="{name}",stream_id="{sid}"}}'
              for name in STAGES}
    registered = [sample for sample in samples
                  if all(f"{label} " in sample["prometheus_text"] for label in labels.values())]
    assert registered
    values = {line.rsplit(" ", 1)[0]: float(line.rsplit(" ", 1)[1])
              for line in registered[-1]["prometheus_text"].splitlines()
              if line and not line.startswith("#")}
    details = {}
    for name, key in labels.items():
        count = values[key]
        assert count > 0
        details[name] = {"count": int(count),
                         "mean_ms": 1000 * values[key.replace("_count{", "_sum{")] / count}
    by_stream[sid] = details
    load = {}
    for name in weak_names:
        key = f'yolo_stream_weak_flow_{name}_by_stream_total{{stream_id="{sid}"}}'
        series = []
        for sample in registered:
            row = {line.rsplit(" ", 1)[0]: float(line.rsplit(" ", 1)[1])
                   for line in sample["prometheus_text"].splitlines()
                   if line and not line.startswith("#")}
            series.append(row[key])
        assert all(math.isfinite(value) and value >= 0 and value.is_integer()
                   for value in series)
        assert all(b >= a for a, b in zip(series, series[1:]))
        load[name] = int(series[-1])
    assert load["roi_pixels"] >= 16 * load["rois"]
    assert load["sampled_points"] <= 20 * load["rois"]
    if IS_CANDIDATE:
        assert load["unique_points"] <= load["sampled_points"]
    weak_by_stream[sid] = {
        **load,
        "rois_per_weak_frame": load["rois"] / details["roi_features"]["count"],
        "roi_pixels_per_roi": load["roi_pixels"] / load["rois"] if load["rois"] else None,
        "points_per_roi": load["sampled_points"] / load["rois"] if load["rois"] else None,
        "output_fps": stream["processed_fps"],
        "duplicate_fraction": (1 - load["unique_points"] / load["sampled_points"]
                               if IS_CANDIDATE and load["sampled_points"] else None),
    }
assert all(sum(item[name] for item in weak_by_stream.values()) <= global_weak_load[name]
           for name in weak_names)
assert not any(stream["inference_errors_max"] or stream["reconnects_delta"]
               for stream in report["streams"])

def extent(field: str) -> list[float]:
    return [min(row[field] for row in report["streams"]),
            max(row[field] for row in report["streams"])]

summary = {
    "case": CASE_NAME,
    "purpose": "controlled local LK candidate comparison; sequential windows do not prove acceleration, quality, or true RTSP acceptance",
    "is_candidate": IS_CANDIDATE,
    "validated_against_previous_profile": True,
    "service_binary_sha256": EXPECTED_BINARY,
    "status": report["overall_status"],
    "failed_gates": [gate["name"] for gate in report["gates"] if gate["passed"] is not True],
    "sample_count": report["sample_count"],
    "elapsed_seconds": report["elapsed_seconds"],
    "metrics_samples": len(samples),
    "selected_global_monotonic_series": len(selected),
    "output_fps": extent("processed_fps"),
    "low_fps": extent("low_detection_fps"),
    "high_fps": extent("high_detection_fps"),
    "p95_ms": extent("result_age_ms_p95"),
    "cpu_host_percent": report["process"]["cpu_average_percent_of_host"],
    "rss_growth_percent": report["process"]["rss_growth_percent"],
    "fairness_percent": report["fairness_spread_percent"],
    "processing_global": processing,
    "weak_flow_load_global": global_weak_load,
    "duplicate_fraction_global": (1 - global_weak_load["unique_points"] /
                                  global_weak_load["sampled_points"] if IS_CANDIDATE else None),
    "weak_flow_load_by_stream_last_registered_scrape": weak_by_stream,
    "processing_by_stream_last_registered_scrape": by_stream,
    "memory_full_lifecycle": {field: {"first": memory[0][field],
                                     "peak": max(row[field] for row in memory),
                                     "last": memory[-1][field]}
                              for field in ("Rss_kib", "Anonymous_kib", "threads")},
}
print(json.dumps(summary, ensure_ascii=False))
