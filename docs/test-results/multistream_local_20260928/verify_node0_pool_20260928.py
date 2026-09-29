"""Read-only pool-2/pool-4 comparison on node0 with fixed 8/8 model threads."""
from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "docs/test-results/multistream_local_20260927"))
from verify_affinity_20260927 import verify_case, CASES as AFFINITY
from summarize_multistream_soak_windows import summarize

CASES = {
    "a1_pool2": ("diag69_node0_threads8_8", 2),
    "b1_pool4": ("diag70_node0_pool4_b1", 4),
    "a2_pool2": ("diag70_node0_pool2_a2", 2),
    "b2_pool4": ("diag70_node0_pool4_b2", 4),
}


def verify() -> dict:
    rows, reference = {}, None
    for tag, (name, pool) in CASES.items():
        case = REPO / "yolo_onnx_cpp/test_outputs/multistream_local_20260928" / name
        row = verify_case(case, AFFINITY["node0"])
        manifest = json.loads((case / "diagnostic.json").read_text())
        state = json.loads((case / "run_state.json").read_text())
        report = json.loads((case / "acceptance/acceptance.json").read_text())
        yaml = (case / "diagnostic-config.yaml").read_text()
        line = f"low_res_infer_request_count: {pool}\n"
        assert yaml.count(line) == 1
        assert manifest["configured_high_threads"] == manifest["configured_low_threads"] == 8
        assert manifest["configured_high_requests"] == 1 and manifest["configured_low_requests"] == pool
        source_fps = [source["delivered_fps"] for source in manifest["source_connections"]]
        assert len(source_fps) == 4 and all(29.5 <= fps <= 30.5 for fps in source_fps)
        invariant = {
            "normalized_yaml": yaml.replace(line, "low_res_infer_request_count: POOL_VARIABLE\n"),
            "code": state["code_sha256"], "binary": state["service_binary_sha256"],
            "models": {a["name"]: a["sha256"] for a in report["provenance"]["artifacts"]
                       if a["kind"] == "model"},
            "inputs": manifest["inputs"], "thresholds": report["thresholds"],
        }
        assert len(invariant["models"]) == 2 and manifest["distinct_source_contents"] == 4
        assert not manifest["source_content_reused"]
        if reference is None:
            reference = invariant
        assert invariant == reference, tag
        row.update({"configured_low_requests": pool,
                    "collection_started_at": state["collection_started_at"],
                    "stopped_at": state["stopped_at"],
                    "runtime_records": manifest["runtime_records"],
                    "source_delivered_fps": source_fps,
                    "approximate_post_warmup_diagnostic": summarize(
                        case, row["durable_samples"])["observed_total"]})
        rows[tag] = row
    return {"scope": "node0 CPU affinity; 8/8 threads; only configured low request pool 2/4 differs; A1 reused from section69",
            "only_configured_low_pool_differs": True, "cases": rows}


if __name__ == "__main__":
    print(json.dumps(verify(), indent=2))
