"""Read-only fixed four-stream CPU-affinity comparison; no memory binding claim."""
from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "yolo_onnx_cpp/tools"))
from verify_local_multistream_soak import audit
from summarize_multistream_soak_windows import summarize

CASES = {
    "all_a1": set(range(64)),
    "node0": set(range(16)) | set(range(32, 48)),
    "node1": set(range(16, 32)) | set(range(48, 64)),
    "all_a2": set(range(64)),
}


def cpu_set(value: str) -> set[int]:
    cpus: set[int] = set()
    for item in value.split(","):
        bounds = [int(number) for number in item.split("-")]
        assert len(bounds) in (1, 2) and bounds[-1] >= bounds[0] >= 0
        cpus.update(range(bounds[0], bounds[-1] + 1))
    return cpus


def verify_case(case: Path, allowed: set[int]) -> dict:
    result = audit(case)
    report = json.loads((case / "acceptance/acceptance.json").read_text())
    assert report["requested_stream_count"] == 4
    assert report["requested_duration_seconds"] == 60
    assert report["sampling_warmup"]["requested_seconds"] == 60
    host = report["provenance"]["collector_host"]
    assert set(host["monitored_pid_cpu_affinity"]) == allowed
    thread_rows = 0
    masks: set[str] = set()
    observed_cpus: set[int] = set()
    with (case / "live_samples.jsonl").open() as source:
        for line in source:
            resources = json.loads(line)["resources"]
            assert set(resources["service_cpu_affinity"]) == allowed
            assert resources["threads"]
            for thread in resources["threads"].values():
                mask = thread["allowed_cpus"]
                assert cpu_set(mask) and cpu_set(mask) <= allowed
                masks.add(mask)
                thread_rows += 1
                if thread["cpu_ticks"] > 0:
                    assert thread["last_cpu"] in allowed
                    observed_cpus.add(thread["last_cpu"])
    result["affinity_evidence"] = {
        "scope": "sampled thread masks and endpoint CPUs; not memory binding or migration counts",
        "expected_cpus": sorted(allowed), "thread_observations": thread_rows,
        "observed_masks": sorted(masks), "observed_endpoint_cpus": sorted(observed_cpus),
    }
    return result


def main() -> None:
    root = REPO / "yolo_onnx_cpp/test_outputs/multistream_local_20260927"
    rows, reference = {}, None
    for tag, allowed in CASES.items():
        case = root / ("diag68_four_affinity_" + tag)
        result = verify_case(case, allowed)
        manifest = json.loads((case / "diagnostic.json").read_text())
        state = json.loads((case / "run_state.json").read_text())
        report = json.loads((case / "acceptance/acceptance.json").read_text())
        invariant = {
            "yaml": (case / "diagnostic-config.yaml").read_text(),
            "code": state["code_sha256"], "binary": state["service_binary_sha256"],
            "models": {a["name"]: a["sha256"] for a in report["provenance"]["artifacts"]
                       if a["kind"] == "model"},
            "inputs": manifest["inputs"],
        }
        assert len(invariant["models"]) == 2 and manifest["distinct_source_contents"] == 4
        assert not manifest["source_content_reused"]
        if reference is None:
            reference = invariant
        assert invariant == reference, tag
        result["runtime_records"] = manifest["runtime_records"]
        result["approximate_post_warmup_diagnostic"] = summarize(
            case, result["durable_samples"])["observed_total"]
        rows[tag] = result
    print(json.dumps({"scope": "four streams; serial all/node0/node1/all; CPU affinity only",
                      "same_yaml_models_inputs_code_binary": True,
                      "runtime_records_identical": all(
                          row["runtime_records"] == rows["all_a1"]["runtime_records"]
                          for row in rows.values()), "cases": rows}, indent=2))


if __name__ == "__main__":
    main()
