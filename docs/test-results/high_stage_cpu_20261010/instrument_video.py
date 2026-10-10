"""Generate an experiment-only instrumented copy of the production video loop."""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
from pathlib import Path


def instrument(source: str) -> str:
    def replace(old: str, new: str) -> None:
        nonlocal source
        if source.count(old) != 1:
            raise ValueError(f"Instrumentation anchor must occur once: {old!r}")
        source = source.replace(old, new, 1)

    replace('#include "video_inference.h"', '#include "profiled_video.h"')
    replace("VideoInferResult inferVideoFile(", "ProfiledVideoResult profiledInferVideoFile(")
    replace("    const auto total_start =", "    StageProfile profile;\n"
            '    if (config.video_model_async || config.video_stride_mode != "fixed" || scheduler) {\n'
            '        throw std::runtime_error("Stage experiment requires sync fixed inference without scheduler");\n'
            "    }\n    const auto total_start =")
    replace("    auto runSyncDetection = [\n", "    auto runSyncDetection = [\n        &profile,\n")
    replace("        const auto& detected_tracks = processor.applyDetections(\n"
            "            infer_result.detections, source_frame_index, true\n        );",
            "        const auto byte_start = std::chrono::steady_clock::now();\n"
            "        const auto& detected_tracks = processor.applyDetections(\n"
            "            infer_result.detections, source_frame_index, true\n        );\n"
            "        profile.byte_track_ms.push_back(elapsedMs(byte_start));")
    replace("    while (capture.read(frame)) {", "    while (true) {\n"
            "        const auto decode_start = std::chrono::steady_clock::now();\n"
            "        const bool decoded = capture.read(frame);\n"
            "        const double decode_ms = elapsedMs(decode_start);\n"
            "        if (!decoded) break;\n"
            "        profile.video_decode_ms.push_back(decode_ms);")
    replace("        auto prepared = processor.prepareFrame(frame, frame_index, true);",
            "        const auto flow_start = std::chrono::steady_clock::now();\n"
            "        auto prepared = processor.prepareFrame(frame, frame_index, true);\n"
            "        profile.lk_flow_ms.push_back(elapsedMs(flow_start));")
    replace("            frame_result.tracks = processor.applyFlow(prepared, false);",
            "            const auto byte_start = std::chrono::steady_clock::now();\n"
            "            const auto& flow_tracks = processor.applyFlow(prepared, false);\n"
            "            profile.byte_track_ms.push_back(elapsedMs(byte_start));\n"
            "            frame_result.tracks = flow_tracks;")
    replace("    return result;", "    profile.cpu_seconds = usage_end.cpu_seconds - usage_start.cpu_seconds;\n"
            "    profile.cpu_wall_ms = std::chrono::duration<double, std::milli>(\n"
            "        usage_end.wall_time - usage_start.wall_time).count();\n"
            "    return {std::move(result), std::move(profile)};")
    return source


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    path = args.repo / "yolo_onnx_cpp/video/video_inference.cpp"
    original = path.read_text()
    modified = instrument(original)
    generated = args.output_dir / "profiled_video.cpp"
    generated.write_text(modified)
    (args.output_dir / "instrumentation.diff").write_text("".join(difflib.unified_diff(
        original.splitlines(keepends=True), modified.splitlines(keepends=True),
        fromfile=str(path), tofile=str(generated))))
    metadata = {"source": str(path), "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "generated_sha256": hashlib.sha256(generated.read_bytes()).hexdigest()}
    (args.output_dir / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
