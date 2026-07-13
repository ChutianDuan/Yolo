import type {
  DetectionResult,
  EventLogEntry,
  FrameResult,
  HighLowDiagnostics,
  RuntimeDetails,
  TrackResult,
  VisionTaskStatus,
} from "../types/vision";
import { EventLog } from "./EventLog";

interface ResultPanelProps {
  frame: FrameResult;
  tracks: TrackResult[];
  events: EventLogEntry[];
  runtime: RuntimeDetails;
  selectedTrackId: number;
  confidenceThreshold: number;
  status: VisionTaskStatus;
  onSelectTrack: (trackId: number) => void;
}

const formatTrackId = (trackId?: number) =>
  trackId === undefined ? "#--" : "#" + trackId.toString().padStart(2, "0");

const notReported = (value: string | number | undefined) =>
  value === undefined || value === "" ? "not reported" : String(value);

function TraceField({ label, value }: { label: string; value: string }) {
  return (
    <div className="grid grid-cols-[95px_minmax(0,1fr)] gap-2 py-1.5 text-[9px]">
      <dt className="text-[#8a93a3]">{label}</dt>
      <dd className="min-w-0 break-words font-mono text-[#3d485b]">{value}</dd>
    </div>
  );
}

const diagnosticLabels: Partial<Record<keyof HighLowDiagnostics, string>> = {
  stableTrackCount: "stable",
  provisionalTrackCount: "provisional",
  maxStableTrackCount: "peak stable",
  maxProvisionalTrackCount: "peak provisional",
  provisionalCreatedCount: "provisional created",
  provisionalPromotedCount: "provisional promoted",
  provisionalExpiredCount: "provisional expired",
  provisionalDeduplicatedCount: "provisional deduplicated",
  stableStableDuplicateCount: "stable/stable merged",
  stableProvisionalDuplicateCount: "stable/provisional merged",
  provisionalProvisionalDuplicateCount: "provisional/provisional merged",
  lowResGeometryRejectionCount: "geometry conflict",
  lowResClassConflictCount: "class conflict",
  flowRejectedTrackCount: "flow rejected",
  flowLowPointRejectionCount: "flow low points",
  flowForwardBackwardRejectionCount: "flow FB rejection",
  flowMotionDispersionRejectionCount: "flow dispersion",
  flowMotionJumpRejectionCount: "flow jump",
  flowBoundaryRejectionCount: "flow boundary",
  directFlowUpdateCount: "direct flow updates",
  globalFlowUpdateCount: "global flow updates",
  urgentLowResDetectionCount: "urgent low detection",
  urgentHighResDetectionCount: "urgent high detection",
};

export function ResultPanel({
  frame,
  tracks,
  events,
  runtime,
  selectedTrackId,
  confidenceThreshold,
  status,
  onSelectTrack,
}: ResultPanelProps) {
  const detections = [...frame.detections].sort((a, b) => b.confidence - a.confidence);
  const selectedDetection: DetectionResult | undefined = frame.detections.find(
    (item) => item.trackId === selectedTrackId,
  );
  const selectedTrack = tracks.find((item) => item.trackId === selectedTrackId);
  const visible = selectedDetection !== undefined;
  const diagnostics = runtime.highLowDiagnostics;
  const diagnosticEntries = diagnostics
    ? (Object.entries(diagnostics) as [keyof HighLowDiagnostics, number][])
        .filter(([key]) => diagnosticLabels[key] !== undefined)
    : [];

  const emptyMessage =
    status === "idle"
      ? "No response mapped."
      : status === "uploading"
        ? "Media staged. Run inference to map objects."
        : status === "failed"
          ? "Request failed. Inspect the run event."
          : status === "detecting" || status === "tracking"
            ? "Awaiting the HTTP response."
            : "No detections above the UI score filter.";

  return (
    <aside className="vision-scrollbar min-h-0 overflow-y-auto border-l border-[#d9dde5] bg-[#fbfcfd]">
      <section className="border-b border-[#d9dde5]">
        <div className="flex h-10 items-center justify-between px-3">
          <h2 className="text-[11px] font-semibold text-[#263247]">Objects</h2>
          <span className="font-mono text-[8px] text-[#8a93a3]">
            frame {frame.frameIndex} / conf {confidenceThreshold.toFixed(2)}
          </span>
        </div>
        {detections.length === 0 ? (
          <p className="border-t border-[#e4e7ec] px-3 py-4 text-[9px] leading-4 text-[#8490a2]">
            {emptyMessage}
          </p>
        ) : (
          <div className="border-t border-[#e4e7ec]">
            {detections.map((detection) => {
              const selected = detection.trackId === selectedTrackId;
              return (
                <button
                  key={detection.id}
                  type="button"
                  onClick={() => detection.trackId !== undefined && onSelectTrack(detection.trackId)}
                  className={
                    "grid w-full grid-cols-[4px_42px_minmax(0,1fr)_42px] items-center gap-2 border-b border-[#eceef2] px-3 py-2 text-left transition-colors hover:bg-[#f5f7fa] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-[#3559a8] active:translate-y-px " +
                    (selected ? "border-l-[#3559a8]" : "border-l-transparent")
                  }
                >
                  <span className={selected ? "h-full bg-[#3559a8]" : "h-full bg-transparent"} />
                  <span className="font-mono text-[9px] font-semibold text-[#3d485b]">
                    {formatTrackId(detection.trackId)}
                  </span>
                  <span className="min-w-0">
                    <span className="block truncate text-[9px] font-medium text-[#3b4659]">
                      {detection.className}
                    </span>
                    <span className="block truncate font-mono text-[8px] text-[#8a93a3]">
                      {frame.tracksSource} / visible
                    </span>
                  </span>
                  <span className="text-right font-mono text-[9px] text-[#3559a8]">
                    {detection.confidence.toFixed(2)}
                  </span>
                </button>
              );
            })}
          </div>
        )}
      </section>

      <section className="border-b border-[#d9dde5] px-3 py-3">
        <div className="flex items-center justify-between">
          <h3 className="text-[11px] font-semibold text-[#263247]">Track Trace</h3>
          {selectedTrack && (
            <span className="font-mono text-[9px] font-semibold text-[#3559a8]">
              {formatTrackId(selectedTrack.trackId)}
            </span>
          )}
        </div>
        {selectedTrack ? (
          <dl className="mt-2">
            <TraceField label="class" value={selectedTrack.className} />
            <TraceField label="first_frame" value={String(selectedTrack.firstFrame)} />
            <TraceField label="last_frame" value={String(selectedTrack.lastFrame)} />
            <TraceField label="current frame" value={String(frame.frameIndex)} />
            <TraceField label="average conf" value={selectedTrack.averageConfidence.toFixed(3)} />
            <TraceField label="status" value={visible ? "active" : selectedTrack.status} />
            <TraceField label="visibility" value={visible ? "visible" : "not visible"} />
            <TraceField label="tracks_source" value={notReported(frame.tracksSource)} />
            <TraceField
              label="provided by"
              value={
                frame.tracksSource === "weak_tracked"
                  ? "LK Optical Flow + ByteTrack"
                  : frame.tracksSource === "interpolated"
                    ? "bbox interpolation"
                    : frame.tracksSource === "detected" || frame.tracksSource === "async_corrected"
                      ? "model detection + ByteTrack"
                      : "not reported"
              }
            />
            <TraceField
              label="bbox"
              value={
                selectedDetection
                  ? "[" +
                    selectedDetection.bbox.x.toFixed(1) + ", " +
                    selectedDetection.bbox.y.toFixed(1) + ", " +
                    selectedDetection.bbox.width.toFixed(1) + ", " +
                    selectedDetection.bbox.height.toFixed(1) + "] %"
                  : "not visible on current frame"
              }
            />
          </dl>
        ) : (
          <p className="mt-3 text-[9px] leading-4 text-[#8992a2]">
            Select a detection box or object row to inspect its lifecycle.
          </p>
        )}
      </section>

      <section id="diagnostics" className="border-b border-[#d9dde5] px-3 py-3">
        <div className="flex items-center justify-between">
          <h3 className="text-[11px] font-semibold text-[#263247]">High/Low diagnostics</h3>
          <span className="font-mono text-[8px] text-[#8a93a3]">
            {diagnostics ? "response" : "optional"}
          </span>
        </div>
        {diagnosticEntries.length > 0 ? (
          <dl className="mt-2 grid grid-cols-2 gap-x-3">
            {diagnosticEntries.map(([key, value]) => (
              <div key={key} className="border-b border-[#eceef2] py-1.5">
                <dt className="truncate text-[8px] text-[#8a93a3]">{diagnosticLabels[key]}</dt>
                <dd className="mt-0.5 font-mono text-[9px] text-[#3d485b]">{value}</dd>
              </div>
            ))}
          </dl>
        ) : (
          <p className="mt-3 text-[9px] leading-4 text-[#8992a2]">
            high_low_diagnostics not reported in the current response.
          </p>
        )}
      </section>

      <EventLog events={events} />
    </aside>
  );
}
