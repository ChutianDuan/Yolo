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
  return (
    <div className="absolute inset-0">
      {detections.map((detection) => {
        const track = tracks.find((item) => item.trackId === detection.trackId);
        const selected = detection.trackId === selectedTrackId;
        const color = selected ? "#3559a8" : track?.color ?? "#66758a";
        const trackLabel = formatTrackId(detection.trackId);

        return (
          <button
            key={detection.id}
            type="button"
            aria-label={
              "Select " + trackLabel + " " + detection.className + " " +
              detection.confidence.toFixed(2)
            }
            onClick={() => detection.trackId !== undefined && onSelectTrack(detection.trackId)}
            className="group absolute block text-left outline-none transition-opacity duration-200 focus-visible:z-20 focus-visible:ring-2 focus-visible:ring-[#3559a8] focus-visible:ring-offset-1 active:opacity-80"
            style={{
              left: detection.bbox.x + "%",
              top: detection.bbox.y + "%",
              width: detection.bbox.width + "%",
              height: detection.bbox.height + "%",
            }}
          >
            <span
              className={
                "absolute inset-0 transition-colors duration-200 " +
                (selected ? "border-[2.5px] bg-[#3559a8]/[0.04]" : "border-[1.5px] group-hover:border-2")
              }
              style={{ borderColor: color }}
            />
            <span className="absolute left-0 top-0 flex max-w-[14rem] -translate-y-full items-center border border-[#d5dae3] bg-[#fbfcfd]/95 px-1.5 py-0.5 font-mono text-[9px] leading-3 text-[#263247]">
              <span className={selected ? "font-semibold text-[#3559a8]" : "font-semibold"}>
                {trackLabel}
              </span>
              <span className="ml-1 truncate">{detection.className}</span>
              <span className="ml-1 text-[#657188]">{detection.confidence.toFixed(2)}</span>
            </span>
            {selected && (
              <>
                <span className="absolute -left-1 -top-1 h-3 w-3 border-l-2 border-t-2 border-[#3559a8]" />
                <span className="absolute -right-1 -top-1 h-3 w-3 border-r-2 border-t-2 border-[#3559a8]" />
                <span className="absolute -bottom-1 -left-1 h-3 w-3 border-b-2 border-l-2 border-[#3559a8]" />
                <span className="absolute -bottom-1 -right-1 h-3 w-3 border-b-2 border-r-2 border-[#3559a8]" />
              </>
            )}
          </button>
        );
      })}
    </div>
  );
}
