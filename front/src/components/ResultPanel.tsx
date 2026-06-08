import type {
  DetectionResult,
  FrameResult,
  TrackResult,
  VisionTaskStatus,
} from "../types/vision";

interface ResultPanelProps {
  frame: FrameResult;
  tracks: TrackResult[];
  selectedTrackId: number;
  confidenceThreshold: number;
  status: VisionTaskStatus;
  onSelectTrack: (trackId: number) => void;
}

function formatConfidence(value: number) {
  return value.toFixed(2);
}

function formatTrackId(trackId?: number) {
  return trackId === undefined ? "#--" : "#" + trackId.toString().padStart(2, "0");
}

function getTrackColor(tracks: TrackResult[], trackId?: number) {
  return tracks.find((track) => track.trackId === trackId)?.color ?? "#2dd4bf";
}

function findSelectedDetection(
  detections: DetectionResult[],
  selectedTrackId: number,
) {
  return detections.find((detection) => detection.trackId === selectedTrackId);
}

export function ResultPanel({
  frame,
  tracks,
  selectedTrackId,
  confidenceThreshold,
  status,
  onSelectTrack,
}: ResultPanelProps) {
  const sortedDetections = [...frame.detections].sort((a, b) => b.confidence - a.confidence);
  const selectedDetection = findSelectedDetection(frame.detections, selectedTrackId);
  const selectedTrack = tracks.find((track) => track.trackId === selectedTrackId);
  const visibleTrackIds = new Set(
    frame.detections.flatMap((detection) =>
      detection.trackId === undefined ? [] : [detection.trackId],
    ),
  );
  const isProcessing = status === "detecting" || status === "tracking";

  return (
    <aside className="flex min-h-0 w-full flex-col border-l border-slate-800 bg-[#0b0f14] lg:w-[280px]">
      <div className="flex h-10 items-center justify-between border-b border-slate-800 px-3">
        <div>
          <h2 className="text-xs font-semibold text-slate-100">Objects</h2>
          <p className="text-[10px] text-slate-500">
            conf {confidenceThreshold.toFixed(2)} · frame {frame.frameIndex}
          </p>
        </div>
        <div className="rounded border border-slate-700 bg-slate-950 px-2 py-1 font-mono text-[10px] text-slate-300">
          {frame.objectCount} / {frame.tracks.length}
        </div>
      </div>

      <div className="vision-scrollbar min-h-0 flex-1 overflow-y-auto">
        {sortedDetections.length === 0 ? (
          <div className="m-3 rounded border border-dashed border-slate-700 bg-slate-950/70 p-3 text-xs text-slate-400">
            {isProcessing ? "Waiting for inference output." : "No detections above threshold."}
          </div>
        ) : (
          <div className="divide-y divide-slate-800">
            {sortedDetections.map((detection) => {
              const selected = detection.trackId === selectedTrackId;
              const trackColor = getTrackColor(tracks, detection.trackId);

              return (
                <button
                  key={detection.id}
                  type="button"
                  onClick={() =>
                    detection.trackId !== undefined && onSelectTrack(detection.trackId)
                  }
                  className={
                    "flex w-full items-center gap-2 px-3 py-2 text-left transition active:translate-y-px " +
                    (selected ? "bg-teal-300/10" : "hover:bg-slate-900")
                  }
                >
                  <span
                    className="h-2 w-2 shrink-0 rounded-full"
                    style={{ backgroundColor: trackColor }}
                  />
                  <span className="w-9 shrink-0 font-mono text-[11px] font-semibold text-slate-200">
                    {formatTrackId(detection.trackId)}
                  </span>
                  <span className="min-w-0 flex-1 truncate text-xs text-slate-200">
                    {detection.className}
                  </span>
                  <span className="font-mono text-[11px] text-teal-200">
                    {formatConfidence(detection.confidence)}
                  </span>
                </button>
              );
            })}
          </div>
        )}
      </div>

      <div className="border-t border-slate-800 p-3">
        {selectedTrack ? (
          <div>
            <div className="mb-2 flex items-center justify-between gap-2">
              <div className="flex min-w-0 items-center gap-2">
                <span
                  className="h-2.5 w-2.5 rounded-full"
                  style={{ backgroundColor: selectedTrack.color }}
                />
                <span className="truncate text-xs font-semibold text-slate-100">
                  {formatTrackId(selectedTrack.trackId)} {selectedTrack.className}
                </span>
              </div>
              <span
                className={
                  "rounded border px-1.5 py-0.5 font-mono text-[10px] " +
                  (visibleTrackIds.has(selectedTrack.trackId)
                    ? "border-emerald-300/40 bg-emerald-300/10 text-emerald-200"
                    : "border-slate-700 bg-slate-950 text-slate-500")
                }
              >
                {visibleTrackIds.has(selectedTrack.trackId) ? "live" : "off"}
              </span>
            </div>

            <dl className="grid grid-cols-2 gap-x-3 gap-y-2 text-[11px]">
              <div>
                <dt className="text-slate-500">frames</dt>
                <dd className="font-mono text-slate-200">
                  {selectedTrack.firstFrame}-{selectedTrack.lastFrame}
                </dd>
              </div>
              <div>
                <dt className="text-slate-500">speed</dt>
                <dd className="font-mono text-slate-200">{selectedTrack.speedKmh} km/h</dd>
              </div>
              <div>
                <dt className="text-slate-500">region</dt>
                <dd className="truncate text-slate-200">{selectedTrack.region}</dd>
              </div>
              <div>
                <dt className="text-slate-500">status</dt>
                <dd className="font-mono text-slate-200">{selectedTrack.status}</dd>
              </div>
              <div className="col-span-2">
                <dt className="text-slate-500">bbox</dt>
                <dd className="truncate font-mono text-slate-200">
                  {selectedDetection
                    ? "[" +
                      selectedDetection.bbox.x.toFixed(0) +
                      ", " +
                      selectedDetection.bbox.y.toFixed(0) +
                      ", " +
                      selectedDetection.bbox.width.toFixed(0) +
                      ", " +
                      selectedDetection.bbox.height.toFixed(0) +
                      "]"
                    : "not visible on frame"}
                </dd>
              </div>
            </dl>
          </div>
        ) : (
          <p className="text-xs text-slate-500">Select a detection to inspect its track.</p>
        )}
      </div>
    </aside>
  );
}
