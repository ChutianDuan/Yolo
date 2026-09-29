"""Fixed node-0 thread comparison, preserving the cross-session baseline gap."""
from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "docs/test-results/multistream_local_20260927"))
from verify_affinity_20260927 import verify_case, CASES as AFFINITY
from summarize_multistream_soak_windows import summarize

CASES = {
    "16_16_before": ("20260927", "diag69_node0_threads16_16", 16, 16),
    "16_8": ("20260928", "diag69_node0_threads16_8", 16, 8),
    "8_8": ("20260928", "diag69_node0_threads8_8", 8, 8),
    "16_16_after": ("20260928", "diag69_node0_threads16_16_after", 16, 16),
}


def verify() -> dict:
    rows, reference = {}, None
    for tag, (date, name, high, low) in CASES.items():
        case = REPO / "yolo_onnx_cpp/test_outputs" / ("multistream_local_" + date) / name
        row = verify_case(case, AFFINITY["node0"])
        manifest = json.loads((case / "diagnostic.json").read_text())
        state = json.loads((case / "run_state.json").read_text())
        report = json.loads((case / "acceptance/acceptance.json").read_text())
        yaml = (case / "diagnostic-config.yaml").read_text()
        for key, value in (("high_model_threads", high), ("low_model_threads", low)):
            line = f"{key}: {value}\n"
            assert yaml.count(line) == 1, (tag, key)
            yaml = yaml.replace(line, f"{key}: THREAD_VARIABLE\n")
        assert manifest["configured_high_threads"] == high
        assert manifest["configured_low_threads"] == low
        source_fps = [source["delivered_fps"] for source in manifest["source_connections"]]
        assert len(source_fps) == 4 and all(29.5 <= fps <= 30.5 for fps in source_fps)
        invariant = {
            "normalized_yaml": yaml, "code": state["code_sha256"],
            "binary": state["service_binary_sha256"], "inputs": manifest["inputs"],
            "models": {a["name"]: a["sha256"] for a in report["provenance"]["artifacts"]
                       if a["kind"] == "model"},
            "thresholds": report["thresholds"],
        }
        assert len(invariant["models"]) == 2
        assert manifest["distinct_source_contents"] == 4 and not manifest["source_content_reused"]
        if reference is None:
            reference = invariant
        assert invariant == reference, tag
        row.update({"configured_high_low_threads": [high, low],
                    "collection_started_at": state["collection_started_at"],
                    "stopped_at": state["stopped_at"],
                    "runtime_records": manifest["runtime_records"],
                    "source_delivered_fps": source_fps,
                    "approximate_post_warmup_diagnostic": summarize(
                        case, row["durable_samples"])["observed_total"]})
        rows[tag] = row
    assert rows["16_16_before"]["runtime_records"] == rows["16_16_after"]["runtime_records"]
    return {"scope": "node0 CPU affinity only; 60s warmup and 60s formal windows; older baseline separated by session gap",
            "only_configured_threads_differ": True, "cases": rows}


if __name__ == "__main__":
    print(json.dumps(verify(), indent=2))
