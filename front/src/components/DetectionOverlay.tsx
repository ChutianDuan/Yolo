import type { DetectionResult, TrackResult } from "../types/vision";

interface DetectionOverlayProps {
  detections: DetectionResult[];
  selectedTrackId: number;
  onSelectTrack: (trackId: number) => void;
  tracks: TrackResult[];
}

const formatTrackId = (trackId?: number) =>
  trackId === undefined ? "#--" : "#" + trackId.toString().padStart(2, "0");

export function DetectionOverlay({
  detections,
  selectedTrackId,
  onSelectTrack,
  tracks,
}: DetectionOverlayProps) {
  const hasSelectedTrack = detections.some(
    (detection) => detection.trackId === selectedTrackId,
  );

  return (
    <div className="absolute inset-0">
      {detections.map((detection) => {
        const track = tracks.find((item) => item.trackId === detection.trackId);
        const color = track?.color ?? "#2dd4bf";
        const trackLabel = formatTrackId(detection.trackId);
        const selected = detection.trackId === selectedTrackId;
        const muted = hasSelectedTrack && !selected;
        const label =
          detection.className + " " + trackLabel + " " + detection.confidence.toFixed(2);

        return (
          <button
            key={detection.id}
            type="button"
            aria-label={"Select " + label}
            onClick={() => detection.trackId !== undefined && onSelectTrack(detection.trackId)}
            className={
              "absolute block text-left outline-none transition duration-200 focus-visible:ring-2 focus-visible:ring-teal-600 focus-visible:ring-offset-2 " +
              (muted ? "opacity-45 hover:opacity-90" : "opacity-100")
            }
            style={{
              left: detection.bbox.x + "%",
              top: detection.bbox.y + "%",
              width: detection.bbox.width + "%",
              height: detection.bbox.height + "%",
            }}
          >
            {selected && (
              <span
                className="pointer-events-none absolute left-1/2 top-1/2 h-[160%] w-[160%] -translate-x-1/2 -translate-y-1/2 rounded-full border border-dashed opacity-45"
                style={{ borderColor: color }}
              />
            )}
            <span
              className={
                "absolute inset-0 transition " +
                (selected ? "border-[3px] bg-white/20" : "border-2 bg-white/5")
              }
              style={{
                borderColor: color,
                boxShadow: selected
                  ? "0 0 0 2px rgba(255,255,255,0.9), 0 10px 24px rgba(15,23,42,0.18)"
                  : "0 4px 12px rgba(15,23,42,0.14)",
              }}
            />
            <span className="absolute left-0 top-0 flex max-w-[15rem] -translate-y-full items-center overflow-hidden rounded-t-md border border-slate-200 bg-white/95 font-mono text-[11px] font-semibold shadow-panel">
              <span className="px-2 py-1 text-slate-800">{detection.className}</span>
              <span className="px-2 py-1 text-slate-950" style={{ backgroundColor: color }}>
                {trackLabel}
              </span>
              <span className="px-2 py-1 text-teal-700">{detection.confidence.toFixed(2)}</span>
            </span>
            <span
              className="absolute left-1/2 top-1/2 h-1.5 w-1.5 -translate-x-1/2 -translate-y-1/2 rounded-full border border-white"
              style={{ backgroundColor: color }}
            />
            {selected && (
              <>
                <span className="absolute -left-1 -top-1 h-4 w-4 border-l-2 border-t-2 border-slate-950" />
                <span className="absolute -right-1 -top-1 h-4 w-4 border-r-2 border-t-2 border-slate-950" />
                <span className="absolute -bottom-1 -left-1 h-4 w-4 border-b-2 border-l-2 border-slate-950" />
                <span className="absolute -bottom-1 -right-1 h-4 w-4 border-b-2 border-r-2 border-slate-950" />
                <span
                  className="absolute bottom-0 right-0 translate-x-1/2 translate-y-1/2 rounded-sm px-1.5 py-0.5 font-mono text-[10px] font-bold text-slate-950"
                  style={{ backgroundColor: color }}
                >
                  locked
                </span>
              </>
            )}
          </button>
        );
      })}
    </div>
  );
}
