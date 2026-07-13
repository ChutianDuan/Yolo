import { Pause, Play, SkipBack, SkipForward } from "@phosphor-icons/react";
import type { FrameResult, TrackResult, VisionTaskStatus } from "../types/vision";

interface TrackingTimelineProps {
  frameResults: FrameResult[];
  tracks: TrackResult[];
  currentFrame: number;
  selectedTrackId: number;
  isPlaying: boolean;
  status: VisionTaskStatus;
  onFrameChange: (frameIndex: number) => void;
  onTogglePlayback: () => void;
}

const sourceStyle: Record<FrameResult["tracksSource"], string> = {
  async_corrected: "border-t-2 border-solid border-[#526f9e] bg-[#526f9e]/25",
  detected: "border-t-2 border-solid border-[#6a7588] bg-[#6a7588]/20",
  weak_tracked: "border-t-2 border-dashed border-[#7b879a] bg-[#7b879a]/10",
  interpolated: "border-t-2 border-dotted border-[#8791a2] bg-transparent",
  empty: "border-t border-[#cfd5df] bg-transparent opacity-50",
  not_reported: "border-t border-[#b9c0cc] bg-transparent",
};

const control =
  "inline-flex h-7 items-center justify-center rounded-md border border-[#cfd5df] bg-white px-2 text-[#4d596d] transition-colors hover:bg-[#f1f3f6] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[#3559a8] active:translate-y-px disabled:cursor-not-allowed disabled:opacity-40";

export function TrackingTimeline({
  frameResults,
  tracks,
  currentFrame,
  selectedTrackId,
  isPlaying,
  status,
  onFrameChange,
  onTogglePlayback,
}: TrackingTimelineProps) {
  const frameCount = frameResults.length;
  const frame = frameResults[currentFrame] ?? frameResults[0];
  const selectedTrack = tracks.find((track) => track.trackId === selectedTrackId);
  const canStep = frameCount > 1;
  const busy = status === "detecting" || status === "tracking";
  const maxObjects = frameResults.reduce(
    (maximum, item) => Math.max(maximum, item.objectCount),
    1,
  );
  const position = (currentFrame / Math.max(frameCount - 1, 1)) * 100;
  const lastFrameIndex = frameResults[frameResults.length - 1]?.frameIndex ?? 0;
  const trackStart = selectedTrack
    ? (selectedTrack.firstFrame / Math.max(lastFrameIndex, 1)) * 100
    : 0;
  const trackWidth = selectedTrack
    ? ((selectedTrack.lastFrame - selectedTrack.firstFrame + 1) /
        Math.max(lastFrameIndex + 1, 1)) * 100
    : 0;

  return (
    <section id="timeline" className="border-t border-[#d9dde5] bg-[#fbfcfd] px-3 py-2.5">
      <div className="flex items-center gap-2">
        <button
          type="button"
          className={control}
          onClick={() => onFrameChange(Math.max(currentFrame - 1, 0))}
          disabled={!canStep || busy}
          aria-label="Previous frame"
        >
          <SkipBack size={12} />
        </button>
        <button
          type="button"
          className={control + " min-w-[64px] gap-1.5 border-[#9dadce] text-[#3559a8]"}
          onClick={onTogglePlayback}
          disabled={!canStep || busy || status === "failed" || status === "uploading"}
        >
          {isPlaying ? <Pause size={11} weight="fill" /> : <Play size={11} weight="fill" />}
          <span className="text-[9px] font-semibold">{isPlaying ? "Pause" : "Play"}</span>
        </button>
        <button
          type="button"
          className={control}
          onClick={() => onFrameChange(Math.min(currentFrame + 1, frameCount - 1))}
          disabled={!canStep || busy}
          aria-label="Next frame"
        >
          <SkipForward size={12} />
        </button>

        <div className="ml-1 min-w-0 flex-1">
          <div className="relative h-8 border-b border-[#cfd5df]">
            <div className="absolute inset-x-0 bottom-0 flex h-7 items-end gap-px overflow-hidden">
              {frameResults.map((item, index) => (
                <button
                  key={item.frameIndex + "-" + index}
                  type="button"
                  onClick={() => onFrameChange(index)}
                  className={
                    "min-w-[1px] flex-1 transition-opacity hover:opacity-100 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-[#3559a8] " +
                    sourceStyle[item.tracksSource] +
                    (index === currentFrame ? " opacity-100" : " opacity-65")
                  }
                  style={{ height: Math.max(5, (item.objectCount / maxObjects) * 24) + "px" }}
                  aria-label={
                    "Frame " + item.frameIndex + ", " + item.tracksSource + ", " +
                    item.objectCount + " objects"
                  }
                  title={
                    "frame_index " + item.frameIndex + "\n" +
                    "timestamp_ms " + item.timestampMs.toFixed(2) + "\n" +
                    "tracks_source " + item.tracksSource + "\n" +
                    "objects " + item.objectCount
                  }
                />
              ))}
            </div>
            <span
              className="pointer-events-none absolute inset-y-0 w-px bg-[#3559a8]"
              style={{ left: position + "%" }}
            />
          </div>
          {selectedTrack && (
            <div className="relative mt-1 h-1.5 bg-[#edf0f4]" title={"Selected track #" + selectedTrack.trackId + " lifespan"}>
              <span
                className="absolute inset-y-0 bg-[#3559a8]/55"
                style={{ left: trackStart + "%", width: trackWidth + "%" }}
              />
            </div>
          )}
        </div>

        <div className="w-[136px] shrink-0 text-right">
          <p className="font-mono text-[9px] text-[#354155]">
            f{String(frame?.frameIndex ?? 0).padStart(3, "0")} / {Math.max(frameCount, 1)}
          </p>
          <p className="mt-0.5 truncate font-mono text-[8px] text-[#8992a2]">
            {frame?.timestampMs.toFixed(1) ?? "0.0"} ms / {frame?.tracksSource ?? "empty"}
          </p>
        </div>
      </div>

      <div className="mt-2 flex flex-wrap items-center gap-x-3 gap-y-1 font-mono text-[8px] text-[#7d8798]">
        <span className="border-t-2 border-solid border-[#526f9e] pt-0.5">async_corrected</span>
        <span className="border-t-2 border-solid border-[#6a7588] pt-0.5">detected</span>
        <span className="border-t-2 border-dashed border-[#7b879a] pt-0.5">weak_tracked</span>
        <span className="border-t-2 border-dotted border-[#8791a2] pt-0.5">interpolated</span>
        <span className="border-t border-[#cfd5df] pt-0.5">empty</span>
      </div>
    </section>
  );
}
