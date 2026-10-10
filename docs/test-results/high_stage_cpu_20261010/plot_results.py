"""Export stage wall-time and average CPU charts from the measured summary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir / "stage_cpu_comparison.png"
    if output.exists():
        raise FileExistsError(output)
    summary = json.loads(args.summary.read_text())["models"]
    names = ("fp32", "int8")
    stages = ("Decode", "Preprocess", "YOLO", "LK Flow", "ByteTrack", "Postprocess", "Other")
    colors = ("#e8b25b", "#86bd91", "#5682ad", "#bc718d", "#8c7fac", "#66aaaa", "#d4d4d4")
    fig, axes = plt.subplots(1, 2, figsize=(12, 5), gridspec_kw={"width_ratios": (2.1, 1)})
    left = [0.0, 0.0]
    for stage, color in zip(stages, colors):
        values = [summary[name]["other_ms_per_frame"] if stage == "Other" else
                  summary[name]["stages"][stage]["amortized_ms_per_frame"] for name in names]
        axes[0].barh(["FP32 + LK", "INT8 + LK"], values, left=left, color=color, label=stage, height=0.5)
        left = [a + b for a, b in zip(left, values)]
    for row, total in enumerate(left):
        axes[0].text(total + 0.4, row, f"{total:.2f} ms", va="center", fontsize=11)
    axes[0].set_xlim(0, max(left) * 1.2)
    axes[0].set_xlabel("Wall time / source frame (ms)")
    axes[0].set_title("Stage breakdown, amortized over all source frames")
    axes[0].invert_yaxis()
    axes[0].legend(loc="upper center", bbox_to_anchor=(0.5, -0.15), ncol=4, frameon=False)
    cpu = [summary[name]["equivalent_busy_cores"] for name in names]
    bars = axes[1].bar(["FP32 + LK", "INT8 + LK"], cpu, color=["#5682ad", "#bc718d"], width=0.55)
    axes[1].set_ylabel("Equivalent continuously busy CPU cores")
    axes[1].set_title("Average process CPU use")
    axes[1].set_ylim(0, max(cpu) * 1.3)
    for bar, name in zip(bars, names):
        axes[1].text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.08,
                     f"{summary[name]['cpu_process_percent']:.1f}%", ha="center")
    for axis in axes:
        axis.spines[["top", "right"]].set_visible(False)
    fig.suptitle("High detector + LK: matched FP32 vs INT8, 3 rounds", fontsize=14)
    fig.tight_layout(rect=(0, 0.04, 1, 0.94))
    fig.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    main()
