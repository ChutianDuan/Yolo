import type {
  DetectionResult,
  EventLogEntry,
  FrameResult,
  TrackResult,
  VisionTaskStatus,
} from "../types/vision";

interface ResultPanelProps {
  frame: FrameResult;
  tracks: TrackResult[];
  events: EventLogEntry[];
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
  return tracks.find((track) => track.trackId === trackId)?.color ?? "#0f766e";
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
  events,
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
  const emptyMessage =
    status === "uploading"
      ? "Run inference to populate detections."
      : status === "failed"
        ? "Inference failed. Check the canvas message."
        : isProcessing
          ? "Waiting for inference output."
          : "No detections above threshold.";

  return (
    <aside className="flex min-h-0 w-full flex-col border-t border-slate-200 bg-white lg:w-[320px] lg:border-l lg:border-t-0">
      <div className="flex h-14 items-center justify-between border-b border-slate-200 px-4">
        <div>
          <h2 className="text-sm font-semibold text-slate-950">Objects</h2>
          <p className="text-[11px] text-slate-500">
            conf {confidenceThreshold.toFixed(2)} · frame {frame.frameIndex}
          </p>
        </div>
        <div className="rounded-md border border-slate-200 bg-slate-50 px-2.5 py-1.5 font-mono text-[11px] font-medium text-slate-700">
          {frame.objectCount} / {frame.tracks.length}
        </div>
      </div>

      <div className="vision-scrollbar min-h-0 flex-1 overflow-y-auto">
        {sortedDetections.length === 0 ? (
          <div className="m-4 rounded-lg border border-dashed border-slate-200 bg-slate-50 p-4 text-xs text-slate-500">
            {emptyMessage}
          </div>
        ) : (
          <div className="divide-y divide-slate-100">
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
                    "flex w-full items-center gap-2 px-4 py-2.5 text-left transition duration-200 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-teal-600 active:translate-y-px " +
                    (selected ? "bg-teal-50" : "hover:bg-slate-50")
                  }
                >
                  <span
                    className="h-2 w-2 shrink-0 rounded-full"
                    style={{ backgroundColor: trackColor }}
                  />
                  <span className="w-9 shrink-0 font-mono text-[11px] font-semibold text-slate-700">
                    {formatTrackId(detection.trackId)}
                  </span>
                  <span className="min-w-0 flex-1 truncate text-xs font-medium text-slate-800">
                    {detection.className}
                  </span>
                  <span className="font-mono text-[11px] font-medium text-teal-700">
                    {formatConfidence(detection.confidence)}
                  </span>
                </button>
              );
            })}
          </div>
        )}
      </div>

      <div className="border-t border-slate-200 bg-slate-50 p-4">
        <div className="mb-4">
          <div className="mb-2 flex items-center justify-between gap-2">
            <h3 className="text-xs font-semibold text-slate-950">Run log</h3>
            <span className="font-mono text-[10px] text-slate-500">{events.length} entries</span>
          </div>
          <div className="space-y-1.5">
            {events.slice(0, 3).map((event) => (
              <article
                key={event.id}
                className="rounded-md border border-slate-200 bg-white px-2.5 py-2"
              >
                <div className="flex items-center gap-2">
                  <span
                    className={
                      "h-1.5 w-1.5 rounded-full " +
                      (event.level === "warning"
                        ? "bg-amber-500"
                        : event.level === "success"
                          ? "bg-emerald-500"
                          : "bg-sky-500")
                    }
                  />
                  <span className="font-mono text-[10px] text-slate-500">{event.time}</span>
                  <span className="min-w-0 truncate text-[11px] font-semibold text-slate-800">
                    {event.message}
                  </span>
                </div>
                <p className="mt-1 line-clamp-2 text-[10px] leading-4 text-slate-500">
                  {event.detail}
                </p>
              </article>
            ))}
          </div>
        </div>

        {selectedTrack ? (
          <div>
            <div className="mb-2 flex items-center justify-between gap-2">
              <div className="flex min-w-0 items-center gap-2">
                <span
                  className="h-2.5 w-2.5 rounded-full"
                  style={{ backgroundColor: selectedTrack.color }}
                />
                <span className="truncate text-xs font-semibold text-slate-950">
                  {formatTrackId(selectedTrack.trackId)} {selectedTrack.className}
                </span>
              </div>
              <span
                className={
                  "rounded-md border px-1.5 py-0.5 font-mono text-[10px] " +
                  (visibleTrackIds.has(selectedTrack.trackId)
                    ? "border-emerald-200 bg-emerald-50 text-emerald-700"
                    : "border-slate-200 bg-white text-slate-500")
                }
              >
                {visibleTrackIds.has(selectedTrack.trackId) ? "live" : "off"}
              </span>
            </div>

            <dl className="grid grid-cols-2 gap-x-3 gap-y-2 text-[11px]">
              <div>
                <dt className="text-slate-500">frames</dt>
                <dd className="font-mono font-medium text-slate-800">
                  {selectedTrack.firstFrame}-{selectedTrack.lastFrame}
                </dd>
              </div>
              <div>
                <dt className="text-slate-500">speed</dt>
                <dd className="font-mono font-medium text-slate-800">{selectedTrack.speedKmh} km/h</dd>
              </div>
              <div>
                <dt className="text-slate-500">region</dt>
                <dd className="truncate font-medium text-slate-800">{selectedTrack.region}</dd>
              </div>
              <div>
                <dt className="text-slate-500">status</dt>
                <dd className="font-mono font-medium text-slate-800">{selectedTrack.status}</dd>
              </div>
              <div className="col-span-2">
                <dt className="text-slate-500">bbox</dt>
                <dd className="truncate font-mono font-medium text-slate-800">
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
