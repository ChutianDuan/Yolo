import type {
  DrogonRequestState,
  MediaKind,
  RuntimeDetails,
  VisionTaskStatus,
} from "../types/vision";

interface SystemBaselineProps {
  mediaKind: MediaKind;
  status: VisionTaskStatus;
  requestState: DrogonRequestState;
  runtime: RuntimeDetails;
}

export function SystemBaseline({
  mediaKind,
  status,
  requestState,
  runtime,
}: SystemBaselineProps) {
  const complete = status === "completed" || status === "demo";
  const demo = status === "demo";
  const failed = status === "failed";
  const active = status === "detecting" || status === "tracking";
  const stages = [
    ["Browser", "active"],
    ["Vite Proxy / API Base", demo ? "not checked" : requestState === "not_checked" ? "idle" : "active"],
    [
      "Drogon",
      demo ? "not checked, Demo" : failed ? "failed" : requestState === "responded" ? "completed" : active ? "active" : "not checked",
    ],
    ["OpenCV", demo ? "Demo replay" : complete ? "completed" : failed ? "failed" : active ? "active" : "not checked"],
    ["ONNX Runtime / OpenVINO", demo ? "Demo replay" : complete ? "completed, backend not reported" : active ? "active" : "not reported"],
    ["ByteTrack", demo ? "Demo replay" : mediaKind === "image" ? "not used" : complete ? "completed" : active ? "active" : "not checked"],
    [
      "LK Optical Flow",
      demo
        ? "Demo replay"
        : mediaKind === "image"
        ? "not used"
        : complete
          ? runtime.weakTrackedFrameCount && runtime.weakTrackedFrameCount > 0
            ? "completed"
            : "not used"
          : active
            ? "active"
            : "not checked",
    ],
    ["response_json", demo ? "Demo replay" : complete ? "completed" : failed ? "failed" : active ? "active" : "not checked"],
    ["Canvas", demo ? "Demo replay" : complete ? "completed" : status === "uploading" ? "active" : failed ? "failed" : "idle"],
  ];

  return (
    <footer className="vision-scrollbar flex h-8 shrink-0 items-center overflow-x-auto border-t border-[#d9dde5] bg-[#f5f6f8] px-3">
      {stages.map(([label, stageStatus], index) => (
        <div key={label} className="flex shrink-0 items-center">
          {index > 0 && <span className="mx-2 text-[9px] text-[#a1a8b3]">→</span>}
          <span className="text-[8px] font-medium text-[#586478]">{label}</span>
          <span
            className={
              "ml-1 font-mono text-[8px] " +
              (stageStatus === "failed"
                ? "text-[#b43c45]"
                : stageStatus === "active"
                  ? "text-[#3559a8]"
                  : "text-[#8992a2]")
            }
          >
            {stageStatus}
          </span>
        </div>
      ))}
    </footer>
  );
}
