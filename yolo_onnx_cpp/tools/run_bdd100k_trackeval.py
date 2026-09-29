#!/usr/bin/env python3
"""Run an external official TrackEval checkout on a prepared BDD100K export."""

from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trackeval-root", type=Path, required=True)
    parser.add_argument("--export-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_and_verify_manifest(export_dir: Path) -> dict[str, Any]:
    manifest_path = export_dir / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"Invalid export manifest: {manifest_path}") from exc
    if manifest.get("format") != "TrackEval BDD100K JSON":
        raise RuntimeError(f"Unexpected export format in {manifest_path}")
    for item in manifest.get("files", []):
        path = export_dir / item["path"]
        if not path.is_file() or sha256_file(path) != item["sha256"]:
            raise RuntimeError(f"Export file missing or changed: {path}")
    return manifest


def trackeval_revision(root: Path) -> str | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return result.stdout.strip() or None


def load_trackeval(root: Path) -> Any:
    package = root / "trackeval" / "__init__.py"
    if not package.is_file():
        raise RuntimeError(f"TrackEval package not found under {root}")
    # TrackEval commit 12c8791 predates NumPy 1.24 alias removals. These aliases
    # restore built-in scalar names without changing metric or dataset code.
    for name, value in (("bool", bool), ("int", int), ("float", float)):
        if name not in np.__dict__:
            setattr(np, name, value)
    sys.path.insert(0, str(root))
    return importlib.import_module("trackeval")


def metric_summary(metric: Any, values: dict[str, Any]) -> dict[str, float]:
    summary = metric.summary_results({"COMBINED_SEQ": values})
    return {name: float(value) for name, value in summary.items()}


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# BDD100K TrackEval 官方指标",
        "",
        f"- TrackEval commit：`{report['trackeval']['git_commit'] or 'unknown'}`",
        f"- 视频数：{report['input']['video_count']}",
        f"- 模式数：{len(report['runs'])}",
        "- 汇总口径：TrackEval `cls_comb_det_av`，数值单位为百分比。",
        "",
        "| run | HOTA | DetA | AssA | LocA | MOTA | IDF1 |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for run_name, run in report["runs"].items():
        combined = run["cls_comb_det_av"]
        lines.append(
            f"| {run_name} | {combined['HOTA']['HOTA']:.3f} | "
            f"{combined['HOTA']['DetA']:.3f} | {combined['HOTA']['AssA']:.3f} | "
            f"{combined['HOTA']['LocA']:.3f} | {combined['CLEAR']['MOTA']:.3f} | "
            f"{combined['Identity']['IDF1']:.3f} |"
        )
    lines.extend(
        [
            "",
            "## 口径限制",
            "",
            "- 导出保留 BDD100K crowd 和 distractor 真值，由 TrackEval 官方 BDD100K adapter 执行忽略规则。",
            "- C++ 的 person/bike/motor 分别映射回 pedestrian/bicycle/motorcycle；模型不能区分 pedestrian 与 rider。",
            "- 这里只评估既有 C++ 响应保留下来的轨迹，不包含当前阈值以下候选，也不是业务摄像机验收结果。",
            "",
        ]
    )
    return "\n".join(lines)


def run_trackeval(trackeval_root: Path, export_dir: Path, output_dir: Path) -> Path:
    root = trackeval_root.resolve()
    export = export_dir.resolve()
    manifest = load_and_verify_manifest(export)
    output = output_dir.resolve()
    try:
        output.mkdir(parents=True, exist_ok=False)
    except FileExistsError as exc:
        raise RuntimeError(f"Output directory already exists: {output}") from exc

    trackeval = load_trackeval(root)
    eval_config = trackeval.Evaluator.get_default_eval_config()
    eval_config.update(
        {
            "USE_PARALLEL": False,
            "BREAK_ON_ERROR": True,
            "PRINT_RESULTS": False,
            "PRINT_CONFIG": False,
            "TIME_PROGRESS": True,
            "OUTPUT_SUMMARY": True,
            "OUTPUT_DETAILED": True,
            "OUTPUT_EMPTY_CLASSES": False,
            "PLOT_CURVES": False,
        }
    )
    dataset_config = trackeval.datasets.BDD100K.get_default_dataset_config()
    dataset_config.update(
        {
            "GT_FOLDER": str(export / "gt"),
            "TRACKERS_FOLDER": str(export / "trackers"),
            "OUTPUT_FOLDER": str(output / "official-output"),
            "TRACKERS_TO_EVAL": manifest["runs"],
            "CLASSES_TO_EVAL": manifest["evaluated_categories"],
            "TRACKER_SUB_FOLDER": "data",
            "OUTPUT_SUB_FOLDER": "",
            "PRINT_CONFIG": False,
        }
    )
    metrics = [
        trackeval.metrics.HOTA(),
        trackeval.metrics.CLEAR(),
        trackeval.metrics.Identity(),
    ]
    evaluator = trackeval.Evaluator(eval_config)
    results, messages = evaluator.evaluate(
        [trackeval.datasets.BDD100K(dataset_config)], metrics
    )
    dataset_results = results["BDD100K"]
    dataset_messages = messages["BDD100K"]
    run_summaries: dict[str, Any] = {}
    for run_name in manifest["runs"]:
        if dataset_messages.get(run_name) != "Success":
            raise RuntimeError(
                f"TrackEval failed for {run_name}: {dataset_messages.get(run_name)}"
            )
        combined = dataset_results[run_name]["COMBINED_SEQ"]
        run_summaries[run_name] = {}
        for combined_name in (
            "cls_comb_det_av",
            "cls_comb_cls_av",
            "VEHICLE",
            "HUMAN",
            "BIKE",
        ):
            if combined_name not in combined:
                continue
            run_summaries[run_name][combined_name] = {
                metric.get_name(): metric_summary(
                    metric, combined[combined_name][metric.get_name()]
                )
                for metric in metrics
            }

    report = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "trackeval": {
            "source": "https://github.com/JonathonLuiten/TrackEval",
            "git_commit": trackeval_revision(root),
            "hota_sha256": sha256_file(root / "trackeval" / "metrics" / "hota.py"),
            "bdd100k_sha256": sha256_file(root / "trackeval" / "datasets" / "bdd100k.py"),
            "numpy_compatibility_aliases": ["np.bool=bool", "np.int=int", "np.float=float"],
        },
        "input": {
            "export_manifest": str(export / "manifest.json"),
            "export_manifest_sha256": sha256_file(export / "manifest.json"),
            "video_count": len(manifest["videos"]),
            "evaluated_categories": manifest["evaluated_categories"],
        },
        "runs": run_summaries,
    }
    json_path = output / "trackeval_summary.json"
    markdown_path = output / "trackeval_summary.md"
    json_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    markdown_path.write_text(render_markdown(report), encoding="utf-8")
    return markdown_path


def main() -> int:
    args = parse_args()
    print(run_trackeval(args.trackeval_root, args.export_dir, args.output_dir))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        raise SystemExit(f"error: {exc}") from exc
