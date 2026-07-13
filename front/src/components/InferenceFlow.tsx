import { useEffect, useMemo, useState } from "react";
import {
  ArrowsClockwise,
  BracketsCurly,
  FileArrowUp,
  FlowArrow,
  GitBranch,
  ImageSquare,
  Path,
  Pulse,
  Scan,
  Stack,
} from "@phosphor-icons/react";
import type {
  FrameResult,
  InferenceMetrics,
  MediaKind,
  RuntimeDetails,
  VisionTaskStatus,
} from "../types/vision";

interface InferenceFlowProps {
  mediaKind: MediaKind;
  status: VisionTaskStatus;
  endpoint: string;
  metrics: InferenceMetrics;
  runtime: RuntimeDetails;
  currentFrame: FrameResult;
  errorMessage?: string;
}

interface FlowNode {
  id: string;
  label: string;
  summary: string;
  icon: typeof Scan;
}

const imageNodes: FlowNode[] = [
  { id: "upload", label: "Upload", summary: "image field", icon: FileArrowUp },
  { id: "drogon", label: "Drogon /infer", summary: "multipart", icon: FlowArrow },
  { id: "decode", label: "OpenCV decode", summary: "image bytes", icon: ImageSquare },
  { id: "letterbox", label: "Letterbox", summary: "normalize", icon: Stack },
  { id: "model", label: "Model inference", summary: "CPU backend", icon: Pulse },
  { id: "output", label: "output0", summary: "[1,300,6]", icon: BracketsCurly },
  { id: "box", label: "Box decode", summary: "score / class", icon: Scan },
  { id: "response", label: "response_json", summary: "detections", icon: BracketsCurly },
  { id: "overlay", label: "Detection Overlay", summary: "canvas", icon: ImageSquare },
];

const videoPrimary: FlowNode[] = [
  { id: "upload", label: "Upload", summary: "video field", icon: FileArrowUp },
  { id: "drogon", label: "Drogon /infer_video", summary: "multipart", icon: FlowArrow },
  { id: "capture", label: "OpenCV VideoCapture", summary: "source frames", icon: ImageSquare },
  { id: "scheduler", label: "Dynamic stride", summary: "scheduler", icon: GitBranch },
];

const strongNodes: FlowNode[] = [
  { id: "detection", label: "Detection frame", summary: "async request", icon: Scan },
  { id: "model", label: "Model inference", summary: "CPU backend", icon: Pulse },
  { id: "motion", label: "Motion compensation", summary: "frame correction", icon: ArrowsClockwise },
  { id: "bytetrack", label: "ByteTrack update", summary: "strong path", icon: Path },
];

const weakNodes: FlowNode[] = [
  { id: "skipped", label: "Skipped frame", summary: "no full model", icon: ImageSquare },
  { id: "flow", label: "LK Optical Flow", summary: "weak tracking", icon: FlowArrow },
  { id: "quality", label: "Flow quality gate", summary: "track policy", icon: Pulse },
  { id: "update-tracked", label: "ByteTrack updateTracked", summary: "ID continuity", icon: Path },
];

const videoOutput: FlowNode[] = [
  { id: "interpolation", label: "bbox interpolation", summary: "fallback", icon: ArrowsClockwise },
  { id: "video-result", label: "VideoInferResult", summary: "frames", icon: Stack },
  { id: "response", label: "response_json", summary: "mapped fields", icon: BracketsCurly },
  { id: "timeline", label: "Frame Timeline", summary: "frame source", icon: FlowArrow },
  { id: "overlay", label: "Detection Overlay", summary: "canvas", icon: ImageSquare },
];

const displayValue = (value: string | number | boolean | undefined, suffix = "") => {
  if (value === undefined || value === "") return "not reported";
  if (typeof value === "boolean") return value ? "true" : "false";
  return String(value) + suffix;
};

function NodeButton({
  node,
  selected,
  status,
  onSelect,
}: {
  node: FlowNode;
  selected: boolean;
  status: VisionTaskStatus;
  onSelect: () => void;
}) {
  const Icon = node.icon;
  const requesting = status === "detecting" || status === "tracking";
  const failed = status === "failed";
  const completed = status === "completed";
  const demo = status === "demo";

  return (
    <button
      type="button"
      onClick={onSelect}
      className={
        "flow-node relative z-10 w-[108px] shrink-0 border-b-2 bg-[#fbfcfd] px-2 py-2 text-left transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[#3559a8] " +
        (selected
          ? "border-[#3559a8] bg-[#f0f4fb]"
          : failed
            ? "border-[#b43c45]"
            : demo
              ? "border-[#9dadce]"
              : requesting
              ? "border-[#3559a8]"
              : completed
                ? "border-[#334155]"
                : "border-[#d6dbe4]")
      }
    >
      <span className="flex items-center gap-1.5">
        <Icon size={13} className={requesting ? "text-[#3559a8]" : "text-[#657188]"} />
        <span className="truncate text-[9px] font-semibold text-[#303b4e]">{node.label}</span>
      </span>
      <span className="mt-1 block truncate font-mono text-[8px] text-[#8992a2]">
        {failed ? "failed" : demo ? "demo replay" : requesting ? "awaiting response" : node.summary}
      </span>
    </button>
  );
}

function FlowRow({
  nodes,
  selectedId,
  status,
  onSelect,
}: {
  nodes: FlowNode[];
  selectedId: string;
  status: VisionTaskStatus;
  onSelect: (id: string) => void;
}) {
  return (
    <div className="flow-row relative flex min-w-max items-center gap-4 py-1">
      {nodes.map((node) => (
        <NodeButton
          key={node.id}
          node={node}
          selected={selectedId === node.id}
          status={status}
          onSelect={() => onSelect(node.id)}
        />
      ))}
    </div>
  );
}

export function InferenceFlow({
  mediaKind,
  status,
  endpoint,
  metrics,
  runtime,
  currentFrame,
  errorMessage,
}: InferenceFlowProps) {
  const [selectedId, setSelectedId] = useState(mediaKind === "video" ? "scheduler" : "model");

  useEffect(() => {
    setSelectedId(mediaKind === "video" ? "scheduler" : "model");
  }, [mediaKind]);
  const selectedTitle = useMemo(() => {
    const nodes = mediaKind === "video"
      ? [...videoPrimary, ...strongNodes, ...weakNodes, ...videoOutput]
      : imageNodes;
    return nodes.find((node) => node.id === selectedId)?.label ?? "Runtime detail";
  }, [mediaKind, selectedId]);

  const fields = useMemo(() => {
    if (selectedId === "scheduler") {
      return [
        ["source_fps", displayValue(runtime.sourceFps)],
        ["target_detect_fps", displayValue(runtime.targetDetectFps)],
        ["effective_detect_fps", displayValue(runtime.effectiveDetectFps)],
        ["base_frame_stride", displayValue(runtime.baseFrameStride)],
        ["min_frame_stride_used", displayValue(runtime.minFrameStrideUsed)],
        ["max_frame_stride_used", displayValue(runtime.maxFrameStrideUsed)],
        ["final_frame_stride", displayValue(runtime.finalFrameStride)],
        ["stride_mode", displayValue(runtime.strideMode)],
        ["scheduled_detection_count", displayValue(runtime.scheduledDetectionCount)],
        ["forced_detection_count", displayValue(runtime.forcedDetectionCount)],
        ["weak_tracked_frame_count", displayValue(runtime.weakTrackedFrameCount)],
      ];
    }
    if (selectedId === "flow" || selectedId === "quality") {
      return [
        ["optical_flow_ms", displayValue(metrics.opticalFlowMs, " ms")],
        ["weak_tracked_frame_count", displayValue(runtime.weakTrackedFrameCount)],
        ["interpolated_frame_count", displayValue(runtime.interpolatedFrameCount)],
        ["tracks_source", displayValue(currentFrame.tracksSource)],
        ["is_detection_frame", displayValue(currentFrame.isDetectionFrame)],
        [
          "flow_rejected_track_count",
          displayValue(runtime.highLowDiagnostics?.flowRejectedTrackCount),
        ],
      ];
    }
    if (selectedId === "model" || selectedId === "output") {
      return [
        ["model_backend", "not reported by response"],
        ["input_shape", "1 x 3 x 736 x 1280 (README config)"],
        [
          "output_shape",
          runtime.outputShapes?.[0]?.length
            ? "[" + runtime.outputShapes[0].join(",") + "]"
            : "not reported",
        ],
        ["model_inference_ms", displayValue(metrics.inferenceMs, " ms")],
        ["processed_frame_count", displayValue(runtime.processedFrameCount)],
        ["detected_frame_count", displayValue(runtime.detectedFrameCount)],
        ["async_request_count", displayValue(runtime.asyncInferRequestCount)],
      ];
    }
    return [
      ["endpoint", endpoint],
      ["frame_index", String(currentFrame.frameIndex)],
      ["timestamp_ms", currentFrame.timestampMs.toFixed(2)],
      ["tracks_source", currentFrame.tracksSource],
      ["objects", String(currentFrame.objectCount)],
      ["status", status === "failed" ? errorMessage ?? "failed" : status],
    ];
  }, [currentFrame, endpoint, errorMessage, metrics, runtime, selectedId, status]);

  return (
    <section className="border-b border-[#d9dde5] bg-[#fbfcfd]" aria-labelledby="flow-title">
      <div className="flex h-9 items-center justify-between border-b border-[#e2e5eb] px-4">
        <h2 id="flow-title" className="text-[11px] font-semibold text-[#263247]">
          Inference Flow
        </h2>
        <span className="font-mono text-[9px] text-[#7d8798]">
          {status === "detecting" || status === "tracking"
            ? "requesting, per-node progress not reported"
            : endpoint}
        </span>
      </div>

      <div className="vision-scrollbar overflow-x-auto px-4 py-2">
        {mediaKind === "image" ? (
          <FlowRow
            nodes={imageNodes}
            selectedId={selectedId}
            status={status}
            onSelect={setSelectedId}
          />
        ) : (
          <div className="min-w-[610px]">
            <FlowRow
              nodes={videoPrimary}
              selectedId={selectedId}
              status={status}
              onSelect={setSelectedId}
            />
            <div className="ml-[54px] border-l border-[#cfd5df] pl-5">
              <div className="flex items-center gap-2 py-1 font-mono text-[8px] text-[#7d8798]">
                <span className="w-11">strong</span>
                <FlowRow
                  nodes={strongNodes}
                  selectedId={selectedId}
                  status={status}
                  onSelect={setSelectedId}
                />
              </div>
              <div className="flex items-center gap-2 py-1 font-mono text-[8px] text-[#7d8798]">
                <span className="w-11">weak</span>
                <FlowRow
                  nodes={weakNodes}
                  selectedId={selectedId}
                  status={status}
                  onSelect={setSelectedId}
                />
              </div>
            </div>
            <FlowRow
              nodes={videoOutput}
              selectedId={selectedId}
              status={status}
              onSelect={setSelectedId}
            />
          </div>
        )}
      </div>

      <article className="grid grid-cols-[130px_minmax(0,1fr)] border-t border-[#e2e5eb] px-4 py-2.5">
        <div>
          <p className="text-[10px] font-semibold text-[#2f3a4d]">{selectedTitle}</p>
          <p className="mt-1 text-[8px] leading-3 text-[#8a93a3]">Selected node detail</p>
        </div>
        <dl className="grid grid-cols-2 gap-x-5 2xl:grid-cols-3">
          {fields.map(([label, value]) => (
            <div key={label} className="flex min-w-0 items-baseline justify-between gap-2 border-b border-[#eceef2] py-1">
              <dt className="truncate text-[8px] text-[#8992a2]">{label}</dt>
              <dd className="max-w-[58%] truncate text-right font-mono text-[8px] text-[#3b4659]" title={value}>
                {value}
              </dd>
            </div>
          ))}
        </dl>
      </article>
    </section>
  );
}
