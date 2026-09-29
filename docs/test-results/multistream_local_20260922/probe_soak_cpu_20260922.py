"""Ten-second read-only CPU/NUMA observation of the owned soak service."""
from __future__ import annotations

import json
import os
import re
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STATE = ROOT / "diag63_six_soak_8h/run_state.json"


def optional_text(path: Path) -> str | None:
    try:
        return path.read_text().strip()
    except (FileNotFoundError, PermissionError):
        return None


def task_stat(path: Path) -> dict:
    fields = path.read_text().rsplit(") ", 1)[1].split()
    return {"cpu_ticks": int(fields[11]) + int(fields[12]),
            "start_ticks": int(fields[19]), "last_cpu": int(fields[36]),
            "state": fields[0]}


def snapshot(pid: int, start_ticks: int, binary: Path) -> dict:
    root = Path("/proc") / str(pid)
    process = task_stat(root / "stat")
    assert process["start_ticks"] == start_ticks and process["state"] != "Z"
    assert (root / "exe").resolve() == binary.resolve()
    threads = {}
    for directory in (root / "task").iterdir():
        try:
            row = task_stat(directory / "stat")
            row["allowed_cpus"] = next(line.split(":", 1)[1].strip()
                                      for line in (directory / "status").read_text().splitlines()
                                      if line.startswith("Cpus_allowed_list:"))
            threads[directory.name] = row
        except FileNotFoundError:
            continue
    pages = {}
    for line in (root / "numa_maps").read_text().splitlines():
        for node, count in re.findall(r"(?:^| )N(\d+)=(\d+)", line):
            pages[node] = pages.get(node, 0) + int(count)
    cpu_line = Path("/proc/stat").read_text().splitlines()[0].split()
    cpu = [int(value) for value in cpu_line[1:9]]
    cgroups = {}
    for line in (root / "cgroup").read_text().splitlines():
        _, controllers, group = line.split(":", 2)
        if "cpu" not in controllers.split(","):
            continue
        mount = Path("/sys/fs/cgroup/cpu")
        for relative in ("", group.lstrip("/")):
            path = mount / relative
            cgroups[str(path)] = {name: optional_text(path / name) for name in (
                "cpu.cfs_quota_us", "cpu.cfs_period_us", "cpu.stat")}
    frequencies = [int(value) for path in Path("/sys/devices/system/cpu").glob(
        "cpu[0-9]*/cpufreq/scaling_cur_freq") if (value := optional_text(path)) is not None]
    return {
        "utc": datetime.now(timezone.utc).isoformat(), "monotonic_seconds": time.monotonic(),
        "process": process, "threads": threads, "numa_mapping_pages_by_node": pages,
        "host_cpu_ticks_first_eight": cpu, "cpu_cgroups": cgroups,
        "scaling_frequency_khz": {"count": len(frequencies),
                                 "min": min(frequencies) if frequencies else None,
                                 "max": max(frequencies) if frequencies else None,
                                 "mean": sum(frequencies) / len(frequencies) if frequencies else None},
        "host_cpu_pressure": optional_text(Path("/proc/pressure/cpu")),
    }


def main() -> None:
    state = json.loads(STATE.read_text())
    assert state["state"] == "collecting"
    pid, start_ticks = state["service_pid"], state["service_start_ticks"]
    binary = Path(state["service_binary"])
    nodes = {}
    for node_path in Path("/sys/devices/system/node").glob("node[0-9]*"):
        for cpu_path in node_path.glob("cpu[0-9]*"):
            nodes[int(cpu_path.name[3:])] = int(node_path.name[4:])
    first = snapshot(pid, start_ticks, binary)
    time.sleep(10)
    last = snapshot(pid, start_ticks, binary)
    duration = last["monotonic_seconds"] - first["monotonic_seconds"]
    hz = os.sysconf("SC_CLK_TCK")
    thread_changes = []
    for tid in first["threads"].keys() & last["threads"].keys():
        a, b = first["threads"][tid], last["threads"][tid]
        if a["start_ticks"] != b["start_ticks"]:
            continue
        ticks = b["cpu_ticks"] - a["cpu_ticks"]
        assert ticks >= 0
        if ticks:
            thread_changes.append({"tid": int(tid), "cpu_percent_one_core": 100 * ticks / hz / duration,
                                   "node_start": nodes.get(a["last_cpu"]),
                                   "node_end": nodes.get(b["last_cpu"]),
                                   "allowed_cpus": b["allowed_cpus"]})
    ticks = last["process"]["cpu_ticks"] - first["process"]["cpu_ticks"]
    deltas = [b - a for a, b in zip(first["host_cpu_ticks_first_eight"],
                                   last["host_cpu_ticks_first_eight"])]
    assert ticks >= 0 and all(value >= 0 for value in deltas)
    thread_changes.sort(key=lambda row: row["cpu_percent_one_core"], reverse=True)
    print(json.dumps({
        "scope": "ten-second observation; endpoints do not prove migration cost or the cause of slowdown",
        "service_pid": pid, "service_start_ticks": start_ticks,
        "service_binary_sha256_recorded": state["service_binary_sha256"],
        "duration_seconds": duration, "cpu_to_numa_node": nodes,
        "service_cpu_percent_one_core": 100 * ticks / hz / duration,
        "service_cpu_percent_of_host": 100 * ticks / hz / duration / os.cpu_count(),
        "host_busy_percent": 100 * (sum(deltas) - deltas[3] - deltas[4]) / sum(deltas),
        "active_threads_observed": len(thread_changes),
        "active_threads_with_different_endpoint_nodes": sum(
            row["node_start"] != row["node_end"] for row in thread_changes
            if row["node_start"] is not None and row["node_end"] is not None),
        "thread_interval": thread_changes, "first": first, "last": last,
    }))


if __name__ == "__main__":
    main()
