import { Rows, Waveform } from "@phosphor-icons/react";
import type { FrameResult, TrackResult } from "../types/vision";

interface TrackingTimelineProps {
  frameResults: FrameResult[];
  tracks: TrackResult[];
  currentFrame: number;
  selectedTrackId: number;
  onFrameChange: (frameIndex: number) => void;
  onSelectTrack: (trackId: number) => void;
}

const formatTrackId = (trackId: number) =>
  "#" + trackId.toString().padStart(2, "0");

export function TrackingTimeline({
  frameResults,
  tracks,
  currentFrame,
  selectedTrackId,
  onFrameChange,
  onSelectTrack,
}: TrackingTimelineProps) {
  const frameCount = frameResults.length;
  const maxObjects = Math.max(...frameResults.map((frame) => frame.objectCount), 1);
  const currentPercent = (currentFrame / Math.max(frameCount - 1, 1)) * 100;
  const rulerFrames = [0, Math.floor((frameCount - 1) * 0.25), Math.floor((frameCount - 1) * 0.5), Math.floor((frameCount - 1) * 0.75), frameCount - 1];

  return (
    <section className="rounded-lg border border-slate-800 bg-[#0c1117] p-4">
      <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
        <div>
          <div className="flex items-center gap-2 text-sm font-semibold text-slate-50">
            <Waveform size={17} className="text-teal-300" />
            Frame timeline
          </div>
          <p className="mt-1 text-xs text-slate-500">
            Frame {currentFrame.toString().padStart(3, "0")} · {frameCount} total frames
          </p>
        </div>
        <div className="grid grid-cols-2 overflow-hidden rounded-md border border-slate-800 bg-slate-950 text-center font-mono text-xs">
          <div className="border-r border-slate-800 px-3 py-2 text-slate-300">
            peak {maxObjects}
          </div>
          <div className="px-3 py-2 text-teal-300">
            {tracks.length} tracks
          </div>
        </div>
      </div>

      <div className="mt-4 flex items-center gap-3">
        <span className="font-mono text-[11px] text-slate-500">0</span>
        <input
          type="range"
          min={0}
          max={frameCount - 1}
          value={currentFrame}
          onChange={(event) => onFrameChange(Number(event.target.value))}
          className="range-control"
          aria-label="Timeline scrubber"
        />
        <span className="font-mono text-[11px] text-slate-500">{frameCount - 1}</span>
      </div>

      <div className="mt-4 rounded-md border border-slate-800 bg-slate-950 p-3">
        <div className="relative h-24">
          <div
            className="absolute bottom-0 top-0 z-10 w-px bg-slate-50"
            style={{ left: currentPercent + "%" }}
          >
            <span className="absolute -top-2 left-1/2 h-2 w-2 -translate-x-1/2 rotate-45 bg-slate-50" />
          </div>
          <div className="absolute inset-x-0 top-0 flex justify-between font-mono text-[10px] text-slate-600">
            {rulerFrames.map((frame) => (
              <span key={frame}>{frame}</span>
            ))}
          </div>
          <div className="absolute inset-x-0 bottom-0 flex h-[72px] items-end gap-px">
            {frameResults.map((frame) => {
              const active = frame.frameIndex === currentFrame;
              const height = Math.max(10, (frame.objectCount / maxObjects) * 100) + "%";

              return (
                <button
                  key={frame.frameIndex}
                  type="button"
                  aria-label={"Go to frame " + frame.frameIndex}
                  onClick={() => onFrameChange(frame.frameIndex)}
                  className={
                    "min-w-0 flex-1 rounded-sm transition " +
                    (active ? "bg-slate-50" : "bg-teal-400/45 hover:bg-teal-300")
                  }
                  style={{ height }}
                />
              );
            })}
          </div>
        </div>
      </div>

      <div className="mt-4 rounded-md border border-slate-800 bg-slate-950/70 p-3">
        <div className="mb-3 flex items-center justify-between gap-3">
          <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-[0.14em] text-slate-500">
            <Rows size={14} />
            Track spans
          </div>
          <span className="font-mono text-[11px] text-slate-500">
            marker {currentFrame.toString().padStart(3, "0")}
          </span>
        </div>
        <div className="space-y-2">
          {tracks.map((track) => {
            const start = (track.firstFrame / Math.max(frameCount - 1, 1)) * 100;
            const width =
              ((track.lastFrame - track.firstFrame + 1) / Math.max(frameCount, 1)) * 100;
            const selected = track.trackId === selectedTrackId;
            const visible = currentFrame >= track.firstFrame && currentFrame <= track.lastFrame;

            return (
              <button
                key={track.trackId}
                type="button"
                onClick={() => onSelectTrack(track.trackId)}
                className={
                  "grid w-full grid-cols-[82px_minmax(0,1fr)_54px] items-center gap-3 rounded-md border px-2 py-2 text-left transition active:translate-y-px " +
                  (selected
                    ? "border-teal-400 bg-teal-400/10"
                    : "border-transparent hover:border-slate-700 hover:bg-slate-900")
                }
              >
                <span className="min-w-0">
                  <span className="block font-mono text-[11px] font-semibold text-slate-300">
                    {formatTrackId(track.trackId)}
                  </span>
                  <span className="block truncate text-[10px] text-slate-600">
                    {track.className}
                  </span>
                </span>
                <span className="relative h-5 overflow-hidden rounded-full bg-slate-800">
                  <span
                    className="absolute inset-y-0 w-px bg-white/70"
                    style={{ left: currentPercent + "%" }}
                  />
                  <span
                    className={
                      "absolute top-1/2 h-3 -translate-y-1/2 rounded-full " +
                      (selected ? "ring-2 ring-white/80" : "")
                    }
                    style={{
                      left: start + "%",
                      width: width + "%",
                      backgroundColor: track.color,
                    }}
                  />
                </span>
                <span
                  className={
                    "rounded-sm border px-1.5 py-1 text-center font-mono text-[10px] " +
                    (visible
                      ? "border-emerald-300/40 bg-emerald-300/10 text-emerald-200"
                      : "border-slate-700 bg-slate-900 text-slate-500")
                  }
                >
                  {visible ? "live" : "off"}
                </span>
              </button>
            );
          })}
        </div>
      </div>
    </section>
  );
}
