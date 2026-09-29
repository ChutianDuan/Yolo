#!/usr/bin/env python3
"""Exercise the realtime API and emit a reproducible multistream acceptance report."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


TERMINAL_STATUSES = {"completed", "expired", "failed", "stopped"}
STREAM_STATUSES = TERMINAL_STATUSES | {
    "starting", "connecting", "running", "reconnecting", "stopping",
}
COUNTERS = (
    "decoded_frame_count", "processed_frame_count", "low_res_detection_count",
    "high_res_detection_count", "dropped_frame_count", "decoder_queue_drop_count",
    "processor_coalesced_frame_count", "inference_error_count", "reconnect_count",
)


@dataclass(frozen=True)
class ProcessSample:
    timestamp: float
    cpu_ticks: int
    rss_bytes: int
    start_ticks: int = 0


class _RequestFailure(RuntimeError):
    """Only locally constructed messages, never remote body/reason/URL text."""


def _safe_error(error: BaseException) -> str:
    return str(error) if isinstance(error, _RequestFailure) else type(error).__name__


def request_json(
    base_url: str,
    method: str,
    path: str,
    timeout: float,
    payload: dict[str, Any] | None = None,
    bearer_token: str | None = None,
) -> Any:
    body = None
    headers: dict[str, str] = {}
    if bearer_token:
        headers["Authorization"] = f"Bearer {bearer_token}"
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(
        base_url.rstrip("/") + path,
        data=body,
        headers=headers,
        method=method,
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            content = response.read()
            return json.loads(content) if content else None
    except urllib.error.HTTPError as error:
        error.close()
        raise _RequestFailure(f"HTTP request returned HTTP {int(error.code)}") from None
    except urllib.error.URLError:
        raise _RequestFailure("HTTP request failed: URLError") from None
    except json.JSONDecodeError:
        raise _RequestFailure("HTTP response contained invalid JSON") from None


def load_sources(arguments: argparse.Namespace) -> list[str]:
    sources = list(arguments.source)
    if arguments.sources_file is not None:
        for raw_line in arguments.sources_file.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if line and not line.startswith("#"):
                sources.append(line)
    if not sources:
        raise ValueError("at least one --source or --sources-file entry is required")
    return sources


def load_bearer_token(arguments: argparse.Namespace) -> str | None:
    env_name = arguments.bearer_token_env
    if not env_name:
        return None
    token = os.environ.get(env_name)
    if not token:
        raise ValueError(
            f"bearer token environment variable is unset or empty: {env_name}"
        )
    return token


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(fraction * len(ordered)) - 1)
    return ordered[index]


def artifact_inputs(arguments: argparse.Namespace) -> list[tuple[str, Path]]:
    config = getattr(arguments, "server_config", None)
    return ([("config", config)] if config is not None else []) + [
        ("model", path) for path in getattr(arguments, "model_file", [])
    ]


def file_fingerprint(path: Path) -> dict[str, Any]:
    before = path.stat()
    if not path.is_file():
        raise ValueError("artifact must be a regular file")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    after = path.stat()
    if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
        after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns
    ):
        raise ValueError("artifact changed while hashing")
    return {"name": path.name, "size_bytes": after.st_size, "sha256": digest.hexdigest()}


def collect_provenance(arguments: argparse.Namespace) -> dict[str, Any]:
    host: dict[str, Any] = {
        "system": platform.system(), "kernel": platform.release(),
        "architecture": platform.machine(), "logical_cpus": os.cpu_count(),
        "cpu_models": [], "physical_cores": None, "sockets": None,
        "collector_cpu_affinity": None, "monitored_pid_cpu_affinity": None,
    }
    try:
        text = Path("/proc/cpuinfo").read_text(encoding="utf-8")
        processors = [
            dict(line.split(":", 1) for line in block.splitlines() if ":" in line)
            for block in text.strip().split("\n\n")
        ]
        processors = [
            {key.strip(): value.strip() for key, value in item.items()}
            for item in processors
        ]
        host["cpu_models"] = sorted({
            item["model name"] for item in processors if item.get("model name")
        })
        cores = {
            (item["physical id"], item["core id"]) for item in processors
            if "physical id" in item and "core id" in item
        }
        host["physical_cores"] = len(cores) or None
        host["sockets"] = len({socket for socket, _core in cores}) or None
    except OSError:
        pass
    for key, pid in (
        ("collector_cpu_affinity", 0), ("monitored_pid_cpu_affinity", arguments.pid)
    ):
        if pid is not None:
            try:
                host[key] = sorted(os.sched_getaffinity(pid))
            except (AttributeError, OSError):
                pass

    artifacts = []
    for index, (kind, path) in enumerate(artifact_inputs(arguments), 1):
        try:
            artifacts.append({"kind": kind, **file_fingerprint(path)})
        except (OSError, ValueError) as error:
            # Do not leak absolute paths or artifact contents in errors/reports.
            raise ValueError(
                f"cannot fingerprint {kind} artifact {index}: {type(error).__name__}"
            ) from error
    return {
        "scope": "Collector host and operator-supplied endpoint fingerprints; service loading is not attested.",
        "collector_host": host, "artifacts": artifacts,
        "artifacts_stable": None, "errors": [],
    }


def verify_artifacts(arguments: argparse.Namespace, provenance: dict[str, Any]) -> None:
    artifacts = provenance["artifacts"]
    for index, ((kind, path), artifact) in enumerate(
        zip(artifact_inputs(arguments), artifacts), 1
    ):
        try:
            current = file_fingerprint(path)
            artifact["sha256_after"] = current["sha256"]
            artifact["size_bytes_after"] = current["size_bytes"]
            artifact["unchanged"] = (
                artifact["sha256"] == current["sha256"]
                and artifact["size_bytes"] == current["size_bytes"]
            )
            if not artifact["unchanged"]:
                provenance["errors"].append(f"{kind} artifact {index} changed during run")
        except (OSError, ValueError) as error:
            artifact["unchanged"] = False
            provenance["errors"].append(
                f"cannot recheck {kind} artifact {index}: {type(error).__name__}"
            )
    provenance["artifacts_stable"] = (
        all(item.get("unchanged") is True for item in artifacts) if artifacts else None
    )


def read_process_sample(pid: int) -> ProcessSample:
    stat_text = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
    command_end = stat_text.rfind(")")
    if command_end < 0:
        raise RuntimeError(f"cannot parse /proc/{pid}/stat")
    fields_after_command = stat_text[command_end + 2 :].split()
    cpu_ticks = int(fields_after_command[11]) + int(fields_after_command[12])

    rss_bytes = 0
    for line in Path(f"/proc/{pid}/status").read_text(encoding="utf-8").splitlines():
        if line.startswith("VmRSS:"):
            rss_bytes = int(line.split()[1]) * 1024
            break
    if rss_bytes <= 0:
        raise RuntimeError(f"no resident memory measurement for process {pid}")
    return ProcessSample(time.monotonic(), cpu_ticks, rss_bytes, int(fields_after_command[19]))


def process_interval(
    previous: ProcessSample,
    current: ProcessSample,
) -> float:
    elapsed = current.timestamp - previous.timestamp
    if elapsed <= 0.0:
        raise ValueError("process samples must have increasing timestamps")
    if current.start_ticks != previous.start_ticks or current.cpu_ticks < previous.cpu_ticks:
        raise ValueError("monitored process restarted or its CPU counter reset")
    clock_ticks = float(os.sysconf("SC_CLK_TCK"))
    logical_cpus = max(os.cpu_count() or 1, 1)
    used_seconds = (current.cpu_ticks - previous.cpu_ticks) / clock_ticks
    return max(0.0, used_seconds / elapsed / logical_cpus * 100.0)


def gate(name: str, passed: bool | None, actual: Any, target: str) -> dict[str, Any]:
    return {
        "name": name,
        "passed": passed,
        "actual": actual,
        "target": target,
    }


def stream_snapshots(response: Any, stream_ids: list[str]) -> dict[str, Any]:
    """Reject malformed telemetry instead of silently skipping a sample."""
    if not isinstance(response, dict) or not isinstance(response.get("streams"), list):
        raise ValueError("/streams response must contain a streams array")
    selected: dict[str, Any] = {}
    for item in response["streams"]:
        if not isinstance(item, dict):
            raise ValueError("/streams contains a non-object entry")
        stream_id = item.get("stream_id")
        if stream_id not in stream_ids:
            continue
        if stream_id in selected:
            raise ValueError(f"duplicate telemetry for {stream_id}")
        if not isinstance(item.get("status"), str) or item["status"] not in STREAM_STATUSES:
            raise ValueError(f"invalid stream status for {stream_id}")
        for key in COUNTERS + ("max_queue_length",):
            value = item.get(key)
            if type(value) is not int or value < 0:
                raise ValueError(f"invalid {key} for {stream_id}")
        age = item.get("latest_result_age_ms")
        if type(age) not in (float, int) or not math.isfinite(age) or age < 0:
            raise ValueError(f"invalid result age for {stream_id}")
        # Export only acceptance telemetry; URL paths/queries and arbitrary errors can be secrets.
        selected[stream_id] = {
            "stream_id": stream_id, "source": "[redacted]", "status": item["status"],
            **{key: item[key] for key in COUNTERS + ("max_queue_length", "latest_result_age_ms")},
        }
    return selected



def run_sampling_warmup(
    arguments: argparse.Namespace, stream_ids: list[str], bearer_token: str | None,
) -> dict[str, Any]:
    """Observe the same owned streams before sampling, without retaining frame histories."""
    requested = getattr(arguments, "warmup_seconds", 0.0)
    result: dict[str, Any] = {
        "requested_seconds": requested, "elapsed_seconds": 0.0,
        "observation_count": 0, "completed": requested == 0.0,
        "final_counters": {}, "errors": [],
    }
    if requested == 0.0:
        return result
    started = time.monotonic()
    first: dict[str, Any] | None = None
    previous: dict[str, Any] | None = None
    try:
        while True:
            snapshots = stream_snapshots(request_json(
                arguments.base_url, "GET", "/streams", arguments.http_timeout_seconds,
                bearer_token=bearer_token,
            ), stream_ids)
            result["elapsed_seconds"] = time.monotonic() - started
            result["observation_count"] += 1
            if set(snapshots) != set(stream_ids):
                result["errors"].append("warmup streams missing")
                break
            counters = {
                stream_id: {key: item[key] for key in COUNTERS}
                for stream_id, item in snapshots.items()
            }
            result["final_counters"] = counters
            if any(item["status"] not in {"starting", "connecting", "running", "reconnecting"}
                   for item in snapshots.values()):
                result["errors"].append("warmup stream terminal or invalid status")
                break
            if any(item["inference_error_count"] for item in snapshots.values()):
                result["errors"].append("warmup inference errors")
                break
            if previous is not None and any(
                counters[stream_id][key] < previous[stream_id][key]
                for stream_id in stream_ids for key in COUNTERS
            ):
                result["errors"].append("warmup counters regressed")
                break
            if first is None:
                first = counters
            previous = counters
            if result["elapsed_seconds"] >= requested:
                result["completed"] = result["observation_count"] >= 2 and all(
                    snapshots[stream_id]["status"] == "running"
                    and counters[stream_id]["processed_frame_count"]
                        > first[stream_id]["processed_frame_count"]
                    for stream_id in stream_ids
                )
                if not result["completed"]:
                    result["errors"].append("warmup ended without running advancing streams")
                break
            time.sleep(min(arguments.sample_interval_seconds,
                           max(0.0, requested - result["elapsed_seconds"])))
    except KeyboardInterrupt:
        result["errors"].append("warmup interrupted")
    except (OSError, RuntimeError, ValueError) as error:
        # HTTP failures can include credentials/URLs; retain only the error category.
        result["errors"].append("warmup " + type(error).__name__)
    return result


def run_lifecycle_churn(
    arguments: argparse.Namespace,
    stream_ids: list[str],
    sources: list[str],
    bearer_token: str | None,
) -> dict[str, Any]:
    """Repeatedly create and remove the requested streams before the soak run."""
    requested_cycles = arguments.lifecycle_cycles
    result: dict[str, Any] = {
        "requested_cycles": requested_cycles,
        "completed_cycles": 0,
        "streams_per_cycle": len(stream_ids),
        "created_streams": 0,
        "delete_attempts": 0,
        "deleted_streams": 0,
        "elapsed_seconds": 0.0,
        "errors": [],
    }
    if requested_cycles == 0:
        return result

    started = time.monotonic()
    for cycle_index in range(requested_cycles):
        created_this_cycle: list[str] = []
        cycle_errors: list[str] = []
        try:
            for stream_id, source in zip(stream_ids, sources):
                request_json(
                    arguments.base_url,
                    "POST",
                    "/streams",
                    arguments.http_timeout_seconds,
                    {"stream_id": stream_id, "source": source},
                    bearer_token=bearer_token,
                )
                created_this_cycle.append(stream_id)
                result["created_streams"] += 1

            visible = stream_snapshots(
                request_json(
                    arguments.base_url,
                    "GET",
                    "/streams",
                    arguments.http_timeout_seconds,
                    bearer_token=bearer_token,
                ),
                stream_ids,
            )
            missing = [stream_id for stream_id in stream_ids if stream_id not in visible]
            if missing:
                raise RuntimeError("created streams not visible: " + ", ".join(missing))
        except KeyboardInterrupt:
            cycle_errors.append("interrupted")
        except (OSError, RuntimeError, ValueError) as error:
            cycle_errors.append(_safe_error(error))
        finally:
            for stream_id in reversed(created_this_cycle):
                result["delete_attempts"] += 1
                try:
                    request_json(
                        arguments.base_url,
                        "DELETE",
                        f"/streams/{stream_id}",
                        arguments.http_timeout_seconds,
                        bearer_token=bearer_token,
                    )
                    result["deleted_streams"] += 1
                except (OSError, RuntimeError, ValueError) as error:
                    cycle_errors.append(f"cleanup failed for {stream_id}: {_safe_error(error)}")

            try:
                remaining = stream_snapshots(
                    request_json(
                        arguments.base_url,
                        "GET",
                        "/streams",
                        arguments.http_timeout_seconds,
                        bearer_token=bearer_token,
                    ),
                    stream_ids,
                )
                if remaining:
                    cycle_errors.append(
                        "streams still registered after cleanup: "
                        + ", ".join(sorted(remaining))
                    )
            except (OSError, RuntimeError, ValueError) as error:
                cycle_errors.append(f"post-cleanup verification failed: {_safe_error(error)}")

        if cycle_errors:
            result["errors"].extend(
                f"cycle {cycle_index + 1}: {error}" for error in cycle_errors
            )
            break
        result["completed_cycles"] += 1
        if cycle_index + 1 < requested_cycles and arguments.lifecycle_interval_seconds > 0:
            time.sleep(arguments.lifecycle_interval_seconds)

    result["elapsed_seconds"] = time.monotonic() - started
    return result


def stream_summary(
    stream_id: str,
    samples: list[dict[str, Any]],
    elapsed: float,
) -> dict[str, Any]:
    observations = [
        sample["streams"][stream_id]
        for sample in samples
        if stream_id in sample["streams"]
    ]
    if not observations:
        return {"stream_id": stream_id, "missing": True, "observed_samples": 0}

    first, last = observations[0], observations[-1]

    def delta(counter: str) -> int:
        return int(last[counter]) - int(first[counter])

    def rate(counter: str) -> float:
        return max(0, delta(counter)) / elapsed if elapsed > 0 else 0.0

    ages = [
        float(item["latest_result_age_ms"])
        for item in observations
        if item["processed_frame_count"] > 0
    ]
    return {
        "stream_id": stream_id,
        "source": last.get("source", ""),
        "status": last["status"],
        "observed_samples": len(observations),
        "terminal_statuses_observed": sorted(
            {item["status"] for item in observations} & TERMINAL_STATUSES
        ),
        "counters_monotonic": all(
            later[key] >= earlier[key]
            for earlier, later in zip(observations, observations[1:])
            for key in COUNTERS
        ),
        "processed_fps": rate("processed_frame_count"),
        "low_detection_fps": rate("low_res_detection_count"),
        "high_detection_fps": rate("high_res_detection_count"),
        "result_age_ms_p50": percentile(ages, 0.50),
        "result_age_ms_p95": percentile(ages, 0.95),
        "result_age_ms_p99": percentile(ages, 0.99),
        "decoded_frames_delta": delta("decoded_frame_count"),
        "processed_frames_delta": delta("processed_frame_count"),
        "dropped_frames_delta": delta("dropped_frame_count"),
        "decoder_queue_drops_delta": delta("decoder_queue_drop_count"),
        "processor_coalesced_frames_delta": delta("processor_coalesced_frame_count"),
        "inference_errors_delta": delta("inference_error_count"),
        "inference_errors_max": max(item["inference_error_count"] for item in observations),
        "max_queue_length": max(item["max_queue_length"] for item in observations),
        "reconnects_delta": delta("reconnect_count"),
    }


def build_report(
    arguments: argparse.Namespace,
    started_at: str,
    samples: list[dict[str, Any]],
    stream_ids: list[str],
    cpu_samples: list[float],
    process_samples: list[ProcessSample],
    run_errors: list[str] | None = None,
    lifecycle_churn: dict[str, Any] | None = None,
    provenance: dict[str, Any] | None = None,
    sampling_warmup: dict[str, Any] | None = None,
) -> dict[str, Any]:
    # Copy and whitelist even when a caller supplies raw snapshots directly.
    samples = [
        {"observed_at_seconds": sample["observed_at_seconds"],
         "streams": stream_snapshots({"streams": list(sample["streams"].values())}, stream_ids)}
        for sample in samples
    ]
    run_errors = list(run_errors or [])
    provenance = provenance or {
        "collector_host": {}, "artifacts": [], "artifacts_stable": None, "errors": [],
    }
    requested_cycles = getattr(arguments, "lifecycle_cycles", 0)
    lifecycle_churn = lifecycle_churn or {
        "requested_cycles": requested_cycles,
        "completed_cycles": 0,
        "streams_per_cycle": len(stream_ids),
        "created_streams": 0,
        "delete_attempts": 0,
        "deleted_streams": 0,
        "elapsed_seconds": 0.0,
        "errors": [],
    }
    times = [sample["observed_at_seconds"] for sample in samples]
    intervals = [later - earlier for earlier, later in zip(times, times[1:])]
    timeline_valid = len(times) >= 2 and all(
        math.isfinite(value) for value in times
    ) and all(interval > 0 for interval in intervals)
    elapsed = times[-1] - times[0] if timeline_valid else 0.0
    streams = [stream_summary(stream_id, samples, elapsed) for stream_id in stream_ids]
    valid_streams = [stream for stream in streams if not stream.get("missing")]
    processed_rates = [stream["processed_fps"] for stream in valid_streams]
    fairness_spread = (
        (max(processed_rates) - min(processed_rates)) / max(processed_rates) * 100.0
        if processed_rates and max(processed_rates) > 0 else None
    )

    # Cumulative CPU time over wall time correctly weights irregular polling intervals.
    cpu_average = None
    if arguments.pid is not None and len(process_samples) >= 2:
        try:
            cpu_average = process_interval(process_samples[0], process_samples[-1])
        except ValueError as error:
            run_errors.append(str(error))
    cpu_peak = max(cpu_samples) if cpu_average is not None and cpu_samples else None
    rss_start = process_samples[0].rss_bytes if process_samples else None
    rss_end = process_samples[-1].rss_bytes if process_samples else None
    rss_growth_percent = (
        (rss_end - rss_start) / rss_start * 100.0
        if cpu_average is not None and rss_start and rss_end else None
    )

    max_gap = max(intervals) if timeline_valid else None
    allowed_gap = arguments.sample_interval_seconds + arguments.http_timeout_seconds
    gates = [
        gate("collection and cleanup completed", not run_errors, run_errors, "no errors"),
        gate(
            "reproducibility evidence recorded",
            True if (
                any(item["kind"] == "config" for item in provenance["artifacts"])
                and any(item["kind"] == "model" for item in provenance["artifacts"])
                and provenance["collector_host"].get("cpu_models")
                and provenance["collector_host"].get("logical_cpus")
            ) else None,
            {
                "config_files": sum(item["kind"] == "config" for item in provenance["artifacts"]),
                "model_files": sum(item["kind"] == "model" for item in provenance["artifacts"]),
                "cpu_identity_recorded": bool(provenance["collector_host"].get("cpu_models")),
            },
            "config and >=1 model fingerprint plus collector CPU identity",
        ),
        gate(
            "artifact fingerprints stable", provenance["artifacts_stable"],
            provenance["errors"], "start/end fingerprints match after cleanup",
        ),
        gate(
            "lifecycle churn completed",
            not lifecycle_churn["errors"]
            and lifecycle_churn["completed_cycles"]
            == lifecycle_churn["requested_cycles"],
            (
                "disabled"
                if lifecycle_churn["requested_cycles"] == 0
                else (
                    str(lifecycle_churn["completed_cycles"])
                    + "/"
                    + str(lifecycle_churn["requested_cycles"])
                    + " cycles"
                )
            ),
            (
                "disabled"
                if lifecycle_churn["requested_cycles"] == 0
                else (
                    str(lifecycle_churn["requested_cycles"])
                    + "/"
                    + str(lifecycle_churn["requested_cycles"])
                    + " cycles; no errors"
                )
            ),
        ),
        gate("valid sampling timeline", timeline_valid, len(samples), ">= 2 ordered samples"),
        gate(
            "requested duration covered",
            elapsed + 1e-6 >= arguments.duration_seconds,
            elapsed,
            f">= {arguments.duration_seconds} s",
        ),
        gate(
            "sampling continuity",
            max_gap is not None and max_gap <= allowed_gap,
            max_gap,
            f"maximum gap <= {allowed_gap} s",
        ),
        gate(
            "all requested streams observed",
            bool(stream_ids) and len(valid_streams) == len(stream_ids),
            f"{len(valid_streams)}/{len(stream_ids)}",
            f"{len(stream_ids)}/{len(stream_ids)}",
        ),
    ]
    for stream in valid_streams:
        stream_id = stream["stream_id"]
        gates.extend([
            gate(
                f"{stream_id}: every sample observed",
                stream["observed_samples"] == len(samples),
                stream["observed_samples"],
                str(len(samples)),
            ),
            gate(
                f"{stream_id}: healthy status",
                stream["status"] == "running" and not stream["terminal_statuses_observed"],
                {"final": stream["status"], "terminal": stream["terminal_statuses_observed"]},
                "running at end; no terminal status during sampling",
            ),
            gate(
                f"{stream_id}: counters never reset",
                stream["counters_monotonic"],
                stream["counters_monotonic"],
                "monotonic",
            ),
            gate(
                f"{stream_id}: processed FPS",
                stream["processed_fps"] >= arguments.min_processed_fps,
                stream["processed_fps"],
                f">= {arguments.min_processed_fps}",
            ),
            gate(
                f"{stream_id}: low detection FPS",
                stream["low_detection_fps"] >= arguments.min_low_detection_fps,
                stream["low_detection_fps"],
                f">= {arguments.min_low_detection_fps}",
            ),
            gate(
                f"{stream_id}: high detection FPS",
                stream["high_detection_fps"] >= arguments.min_high_detection_fps,
                stream["high_detection_fps"],
                f">= {arguments.min_high_detection_fps}",
            ),
            gate(
                f"{stream_id}: result age P95",
                stream["result_age_ms_p95"] is not None
                and stream["result_age_ms_p95"] <= arguments.max_result_age_ms,
                stream["result_age_ms_p95"],
                f"<= {arguments.max_result_age_ms} ms (local decode to query)",
            ),
            gate(
                f"{stream_id}: inference errors",
                stream["inference_errors_max"] == 0,
                stream["inference_errors_max"],
                "0, including before the first sample",
            ),
            gate(
                f"{stream_id}: queue bound",
                stream["max_queue_length"] <= arguments.max_queue_depth,
                stream["max_queue_length"],
                f"<= {arguments.max_queue_depth}",
            ),
        ])

    gates.extend([
        gate(
            "stream fairness spread",
            fairness_spread is not None
            and fairness_spread <= arguments.max_fairness_spread_percent,
            fairness_spread,
            f"<= {arguments.max_fairness_spread_percent}%",
        ),
        gate(
            "process CPU average",
            None if cpu_average is None else cpu_average <= arguments.max_cpu_percent,
            cpu_average,
            f"<= {arguments.max_cpu_percent}% of host",
        ),
        gate(
            "process RSS growth",
            None if rss_growth_percent is None
            else rss_growth_percent <= arguments.max_memory_growth_percent,
            rss_growth_percent,
            f"<= {arguments.max_memory_growth_percent}%",
        ),
    ])
    requested_warmup = getattr(arguments, "warmup_seconds", 0.0)
    if requested_warmup > 0.0:
        warmup_valid = sampling_warmup is not None and (
            sampling_warmup.get("requested_seconds") == requested_warmup
            and sampling_warmup.get("completed") is True
            and not sampling_warmup.get("errors")
            and sampling_warmup.get("observation_count", 0) >= 2
            and math.isfinite(sampling_warmup.get("elapsed_seconds", 0.0))
            and sampling_warmup.get("elapsed_seconds", 0.0) >= requested_warmup
            and set(sampling_warmup.get("final_counters", {})) == set(stream_ids)
            and all(
                type(value) is int and value >= 0
                for stream_id in stream_ids
                for value in (sampling_warmup["final_counters"][stream_id].get(key)
                              for key in COUNTERS)
            )
        )
        gates.append(gate("sampling warmup completed",
                          warmup_valid if sampling_warmup is not None else None,
                          sampling_warmup or "missing",
                          f">= {requested_warmup} s healthy same-stream warmup"))
    if any(entry["passed"] is False for entry in gates):
        overall_status = "FAIL"
    elif any(entry["passed"] is None for entry in gates):
        overall_status = "INCOMPLETE"
    else:
        overall_status = "PASS"
    report = {
        "schema_version": 4,
        "started_at_utc": started_at,
        "finished_at_utc": datetime.now(timezone.utc).isoformat(),
        "base_url": "[redacted]",
        "requested_stream_count": len(stream_ids),
        "sample_count": len(samples),
        "elapsed_seconds": elapsed,
        "requested_duration_seconds": arguments.duration_seconds,
        "run_errors": run_errors,
        "lifecycle_churn": lifecycle_churn,
        "provenance": provenance,
        "thresholds": {
            "min_processed_fps": arguments.min_processed_fps,
            "min_low_detection_fps": arguments.min_low_detection_fps,
            "min_high_detection_fps": arguments.min_high_detection_fps,
            "max_result_age_ms": arguments.max_result_age_ms,
            "max_fairness_spread_percent": arguments.max_fairness_spread_percent,
            "max_cpu_percent": arguments.max_cpu_percent,
            "max_memory_growth_percent": arguments.max_memory_growth_percent,
            "max_queue_depth": arguments.max_queue_depth,
        },
        "process": {
            "pid": arguments.pid,
            "cpu_average_percent_of_host": cpu_average,
            "cpu_peak_percent_of_host": cpu_peak,
            "rss_start_bytes": rss_start,
            "rss_end_bytes": rss_end,
            "rss_growth_percent": rss_growth_percent,
            "host_logical_cpus": os.cpu_count(),
        },
        "fairness_spread_percent": fairness_spread,
        "streams": streams,
        "gates": gates,
        "overall_status": overall_status,
        "overall_passed": overall_status == "PASS",
        "samples": samples,
    }
    if requested_warmup > 0.0:
        report["schema_version"] = 5
        report["sampling_warmup"] = sampling_warmup
    return report


def markdown_report(report: dict[str, Any]) -> str:
    lifecycle = report["lifecycle_churn"]
    lines = [
        "# 多路实时监测验收报告",
        "",
        f"- 开始时间：{report['started_at_utc']}",
        f"- 实际采样时长：{report['elapsed_seconds']:.1f} s",
        f"- 要求采样时长：{report['requested_duration_seconds']:.1f} s",
        f"- 请求流数：{report['requested_stream_count']}",
        f"- 生命周期循环：{lifecycle['completed_cycles']}/"
        f"{lifecycle['requested_cycles']}，创建 {lifecycle['created_streams']} 次，"
        f"删除 {lifecycle['deleted_streams']}/{lifecycle['delete_attempts']} 次",
        f"- 总体结果：{report['overall_status']}",
        "- 范围：本次运行指标；不代替人工真值质量、输入规格或完整生产验收。",
        "",
        "## 每路结果",
        "",
        "| stream_id | status | output FPS | low FPS | high FPS | age P95 ms | drops | infer errors | max queue |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    if "sampling_warmup" in report:
        warmup = report["sampling_warmup"]
        summary = (
            f'预热 {warmup["elapsed_seconds"]:.3f}/{warmup["requested_seconds"]:.3f} s，'
            f'观察 {warmup["observation_count"]} 次，完成 {warmup["completed"]}'
            if warmup else "预热证据缺失"
        )
        lines.insert(8, "- 同一批流采样前预热：" + summary + "；不计入采样时长/资源基线。")
    for stream in report["streams"]:
        if stream.get("missing"):
            lines.append(f"| {stream['stream_id']} | missing | - | - | - | - | - | - | - |")
            continue
        age = stream["result_age_ms_p95"]
        age_text = "-" if age is None else f"{age:.2f}"
        lines.append(
            "| {stream_id} | {status} | {processed_fps:.3f} | "
            "{low_detection_fps:.3f} | {high_detection_fps:.3f} | "
            "{age} | {dropped_frames_delta} | {inference_errors_delta} | "
            "{max_queue_length} |".format(age=age_text, **stream)
        )

    provenance = report["provenance"]
    host = provenance["collector_host"]
    lines.extend([
        "", "## 复现证据", "",
        "- 范围：采集机与文件前后指纹；未证明服务加载配置/模型或期间从未改动。",
        f"- CPU：{', '.join(host.get('cpu_models', [])) or '未记录'}",
        f"- 逻辑核/物理核/插槽：{host.get('logical_cpus')} / "
        f"{host.get('physical_cores')} / {host.get('sockets')}",
        f"- 采集器 CPU affinity：{host.get('collector_cpu_affinity')}",
        f"- 监测 PID CPU affinity：{host.get('monitored_pid_cpu_affinity')}",
        "",
        "| kind | file | bytes | SHA-256 before | SHA-256 after | unchanged |",
        "| --- | --- | ---: | --- | --- | --- |",
    ])
    for artifact in provenance["artifacts"]:
        name = artifact["name"].replace("|", "\\|").replace("\n", " ").replace("\r", " ")
        lines.append(
            f"| {artifact['kind']} | {name} | {artifact['size_bytes']} | "
            f"{artifact['sha256']} | {artifact.get('sha256_after', '-')} | "
            f"{artifact.get('unchanged')} |"
        )

    lines.extend(
        [
            "",
            "## 验收门槛",
            "",
            "| gate | result | actual | target |",
            "| --- | --- | --- | --- |",
        ]
    )
    for entry in report["gates"]:
        result = (
            "NOT_EVALUATED"
            if entry["passed"] is None
            else ("PASS" if entry["passed"] else "FAIL")
        )
        lines.append(
            f"| {entry['name']} | {result} | {entry['actual']} | {entry['target']} |"
        )
    lines.append("")
    return "\n".join(lines)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create streams, sample the realtime API, and emit acceptance reports."
    )
    parser.add_argument("--base-url", default="http://127.0.0.1:8080")
    parser.add_argument("--source", action="append", default=[])
    parser.add_argument("--sources-file", type=Path)
    parser.add_argument("--stream-prefix", default="acceptance")
    parser.add_argument("--duration-seconds", type=float, default=60.0)
    parser.add_argument("--warmup-seconds", type=float, default=0.0,
                        help="observe the same streams before sampling; default keeps startup baseline")
    parser.add_argument("--sample-interval-seconds", type=float, default=1.0)
    parser.add_argument("--http-timeout-seconds", type=float, default=10.0)
    parser.add_argument(
        "--lifecycle-cycles",
        type=int,
        default=0,
        help="before sampling, repeatedly create, verify and delete all streams",
    )
    parser.add_argument(
        "--lifecycle-interval-seconds",
        type=float,
        default=0.0,
        help="delay between successful lifecycle cycles",
    )
    parser.add_argument(
        "--bearer-token-env",
        help="read the API bearer token from this environment variable",
    )
    parser.add_argument("--pid", type=int)
    parser.add_argument("--server-config", type=Path, help="fingerprint the operator-supplied server config")
    parser.add_argument("--model-file", type=Path, action="append", default=[],
                        help="fingerprint an operator-supplied model; repeat for high/low models")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--keep-streams", action="store_true")
    parser.add_argument("--min-processed-fps", type=float, default=25.0)
    parser.add_argument("--min-low-detection-fps", type=float, default=4.0)
    parser.add_argument("--min-high-detection-fps", type=float, default=0.5)
    parser.add_argument("--max-result-age-ms", type=float, default=300.0)
    parser.add_argument("--max-fairness-spread-percent", type=float, default=10.0)
    parser.add_argument("--max-cpu-percent", type=float, default=85.0)
    parser.add_argument("--max-memory-growth-percent", type=float, default=5.0)
    parser.add_argument("--max-queue-depth", type=int, default=2)
    arguments = parser.parse_args()
    for name in ("duration_seconds", "sample_interval_seconds", "http_timeout_seconds"):
        value = getattr(arguments, name)
        if not math.isfinite(value) or value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be finite and positive")
    if (
        not math.isfinite(arguments.lifecycle_interval_seconds)
        or arguments.lifecycle_interval_seconds < 0
    ):
        parser.error("--lifecycle-interval-seconds must be finite and non-negative")
    if not math.isfinite(arguments.warmup_seconds) or arguments.warmup_seconds < 0:
        parser.error("--warmup-seconds must be finite and non-negative")
    if arguments.lifecycle_cycles < 0:
        parser.error("--lifecycle-cycles must be non-negative")
    for name in (
        "min_processed_fps", "min_low_detection_fps", "min_high_detection_fps",
        "max_result_age_ms", "max_fairness_spread_percent", "max_cpu_percent",
        "max_memory_growth_percent",
    ):
        value = getattr(arguments, name)
        if not math.isfinite(value) or value < 0:
            parser.error(f"--{name.replace('_', '-')} must be finite and non-negative")
    if arguments.max_queue_depth <= 0 or (arguments.pid is not None and arguments.pid <= 0):
        parser.error("--max-queue-depth and --pid must be positive")
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,32}", arguments.stream_prefix):
        parser.error("--stream-prefix must be 1-32 letters, digits, '.', '_' or '-'")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    try:
        sources = load_sources(arguments)
        bearer_token = load_bearer_token(arguments)
        provenance = collect_provenance(arguments)
    except (OSError, ValueError) as error:
        print(f"error: {_safe_error(error)}", file=sys.stderr)
        return 2

    started_at = datetime.now(timezone.utc).isoformat()
    run_stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S_%f")
    stream_ids = [
        f"{arguments.stream_prefix}-{index + 1}-{run_stamp}"
        for index in range(len(sources))
    ]
    output_dir = arguments.output_dir or (
        Path(__file__).resolve().parents[1]
        / "test_outputs" / "multistream_acceptance" / run_stamp
    )
    # Refuse an existing output directory before changing server state.
    try:
        output_dir.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        print(f"cannot create report directory: {_safe_error(error)}", file=sys.stderr)
        return 2

    created_stream_ids: list[str] = []
    samples: list[dict[str, Any]] = []
    cpu_samples: list[float] = []
    process_samples: list[ProcessSample] = []
    run_errors: list[str] = []
    lifecycle_churn: dict[str, Any] | None = None
    sampling_warmup: dict[str, Any] | None = None
    try:
        ready = request_json(
            arguments.base_url, "GET", "/ready", arguments.http_timeout_seconds,
            bearer_token=bearer_token,
        )
        if not isinstance(ready, dict) or ready.get("status") != "ready":
            raise RuntimeError("service is not ready")

        lifecycle_churn = run_lifecycle_churn(
            arguments, stream_ids, sources, bearer_token
        )
        if lifecycle_churn["errors"]:
            run_errors.extend(
                f"lifecycle churn failed: {error}"
                for error in lifecycle_churn["errors"]
            )

        streams_to_create = zip(stream_ids, sources) if not run_errors else ()
        for stream_id, source in streams_to_create:
            request_json(
                arguments.base_url, "POST", "/streams", arguments.http_timeout_seconds,
                {"stream_id": stream_id, "source": source},
                bearer_token=bearer_token,
            )
            created_stream_ids.append(stream_id)

        if not run_errors and getattr(arguments, "warmup_seconds", 0.0) > 0.0:
            sampling_warmup = run_sampling_warmup(arguments, stream_ids, bearer_token)
            run_errors.extend(sampling_warmup["errors"])
        previous_process = None
        started_monotonic = None
        while not run_errors:
            response = request_json(
                arguments.base_url, "GET", "/streams", arguments.http_timeout_seconds,
                bearer_token=bearer_token,
            )
            all_streams = stream_snapshots(response, stream_ids)
            if started_monotonic is None and sampling_warmup is not None:
                last = sampling_warmup["final_counters"]
                if set(all_streams) != set(stream_ids) or any(
                    all_streams[stream_id][key] < last[stream_id][key]
                    for stream_id in stream_ids for key in COUNTERS
                ):
                    raise RuntimeError("warmup-to-sampling counters regressed or streams missing")
            now = time.monotonic()
            if started_monotonic is None:
                started_monotonic = now
            elapsed = now - started_monotonic
            samples.append({"observed_at_seconds": elapsed, "streams": all_streams})

            if arguments.pid is not None:
                current_process = read_process_sample(arguments.pid)
                process_samples.append(current_process)
                if previous_process is not None:
                    cpu_samples.append(process_interval(previous_process, current_process))
                previous_process = current_process

            all_terminal = len(all_streams) == len(stream_ids) and all(
                stream["status"] in TERMINAL_STATUSES for stream in all_streams.values()
            )
            if elapsed >= arguments.duration_seconds or all_terminal:
                break
            time.sleep(min(
                arguments.sample_interval_seconds,
                max(0.0, arguments.duration_seconds - elapsed),
            ))
    except KeyboardInterrupt:
        run_errors.append("collection interrupted")
    except (OSError, RuntimeError, ValueError) as error:
        run_errors.append(f"collection failed: {_safe_error(error)}")
    finally:
        if not arguments.keep_streams:
            for stream_id in reversed(created_stream_ids):
                try:
                    request_json(
                        arguments.base_url, "DELETE", f"/streams/{stream_id}",
                        arguments.http_timeout_seconds, bearer_token=bearer_token,
                    )
                except (OSError, RuntimeError, ValueError) as error:
                    run_errors.append(f"cleanup failed for {stream_id}: {_safe_error(error)}")

    verify_artifacts(arguments, provenance)
    run_errors.extend(provenance["errors"])

    # Preserve evidence even for failed creation, interruption, or zero samples.
    report = build_report(
        arguments, started_at, samples, stream_ids, cpu_samples, process_samples, run_errors,
        lifecycle_churn, provenance, sampling_warmup,
    )
    json_path, markdown_path = output_dir / "acceptance.json", output_dir / "acceptance.md"
    try:
        json_path.write_text(
            json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
            encoding="utf-8",
        )
        markdown_path.write_text(markdown_report(report), encoding="utf-8")
    except (OSError, ValueError) as error:
        print(f"cannot write report: {_safe_error(error)}", file=sys.stderr)
        return 2

    for error in report["run_errors"]:
        print(f"error: {error}", file=sys.stderr)
    print(f"JSON report: {json_path}")
    print(f"Markdown report: {markdown_path}")
    print(f"Overall: {report['overall_status']}")
    return 0 if report["overall_passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
