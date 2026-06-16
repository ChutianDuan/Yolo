import { useEffect, useRef } from "react";
import {
  Pause,
  Play,
  SkipBack,
  SkipForward,
} from "@phosphor-icons/react";
import type { FrameResult, MediaKind, VisionTaskStatus } from "../types/vision";
import { DetectionOverlay } from "./DetectionOverlay";

interface MediaDimensions {
  width: number;
  height: number;
}

interface VisionCanvasProps {
  frame: FrameResult;
  frameResults: FrameResult[];
  frameCount: number;
  mediaUrl?: string;
  mediaDimensions?: MediaDimensions;
  mediaName: string;
  mediaKind: MediaKind;
  selectedTrackId: number;
  status: VisionTaskStatus;
  isPlaying: boolean;
  onSelectTrack: (trackId: number) => void;
  onFrameChange: (frameIndex: number) => void;
  onTogglePlayback: () => void;
}

const controlButton =
  "inline-flex h-7 w-7 items-center justify-center rounded-md border border-slate-700 bg-slate-950 text-slate-200 transition hover:border-slate-500 hover:bg-slate-900 active:translate-y-px";

const formatTrackId = (trackId: number) =>
  "#" + trackId.toString().padStart(2, "0");

export function VisionCanvas({
  frame,
  frameResults,
  frameCount,
  mediaUrl,
  mediaDimensions,
  mediaName,
  mediaKind,
  selectedTrackId,
  status,
  isPlaying,
  onSelectTrack,
  onFrameChange,
  onTogglePlayback,
}: VisionCanvasProps) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const selectedTrack = frame.tracks.find((track) => track.trackId === selectedTrackId);
  const isProcessing = status === "detecting" || status === "tracking";
  const isComplete = status === "completed";
  const maxObjects = Math.max(...frameResults.map((item) => item.objectCount), 1);
  const frameMax = Math.max(frameCount - 1, 0);
  const frameStyle = mediaDimensions
    ? { aspectRatio: mediaDimensions.width + " / " + mediaDimensions.height }
    : undefined;

  useEffect(() => {
    if (mediaKind !== "video") {
      return undefined;
    }

    const video = videoRef.current;
    if (!video) {
      return undefined;
    }

    const nextTime = Math.max(frame.timestampMs / 1000, 0);
    const syncTime = () => {
      if (!Number.isFinite(nextTime)) {
        return;
      }
      if (Math.abs(video.currentTime - nextTime) > 0.08) {
        video.currentTime = nextTime;
      }
    };

    syncTime();
    video.addEventListener("loadedmetadata", syncTime);
    return () => video.removeEventListener("loadedmetadata", syncTime);
  }, [frame.timestampMs, mediaKind, mediaUrl]);

  useEffect(() => {
    const video = videoRef.current;
    if (mediaKind !== "video" || !video) {
      return;
    }

    if (isPlaying) {
      void video.play().catch(() => undefined);
    } else {
      video.pause();
    }
  }, [isPlaying, mediaKind, mediaUrl]);

  return (
    <section className="flex min-h-0 flex-1 flex-col bg-[#080d13]">
      <div className="flex min-h-0 flex-1 items-center justify-center p-2">
        <div className="w-full max-w-[1280px]">
          <div
            className="technical-frame relative aspect-video overflow-hidden rounded-md border border-slate-700 shadow-panel"
            style={frameStyle}
          >
            {mediaUrl ? (
              mediaKind === "image" ? (
                <img
                  src={mediaUrl}
                  alt=""
                  className="absolute inset-0 h-full w-full object-fill"
                  draggable={false}
                />
              ) : (
                <video
                  ref={videoRef}
                  src={mediaUrl}
                  className="absolute inset-0 h-full w-full object-fill"
                  muted
                  playsInline
                  preload="metadata"
                />
              )
            ) : (
              <>
                <div className="absolute left-0 top-0 h-[42%] w-full bg-gradient-to-b from-slate-700/20 to-transparent" />
                <div className="absolute left-[8%] top-[18%] h-[18%] w-[34%] rounded-sm bg-slate-950/35" />
                <div className="absolute right-[13%] top-[16%] h-[22%] w-[16%] rounded-sm bg-slate-900/50" />
                <div className="road-plane" />
                <div className="sensor-grid" />
              </>
            )}
            {mediaUrl && (
              <div className="pointer-events-none absolute inset-0 bg-gradient-to-b from-slate-950/10 via-transparent to-slate-950/20" />
            )}

            <div className="absolute left-2 top-2 rounded border border-slate-700 bg-slate-950/95 px-2 py-1 font-mono text-[10px] text-slate-300">
              {mediaKind === "video" ? "video" : "image"} · frame {frame.frameIndex.toString().padStart(3, "0")} · {frame.objectCount} boxes
            </div>
            <div className="absolute right-2 top-2 max-w-[60%] truncate rounded border border-slate-700 bg-slate-950/95 px-2 py-1 font-mono text-[10px] text-slate-400">
              {mediaName}
            </div>

            {selectedTrack && (
              <div className="absolute bottom-2 left-2 rounded border border-slate-700 bg-slate-950/95 px-2 py-1.5">
                <div className="flex items-center gap-2 text-[11px]">
                  <span
                    className="h-2 w-2 rounded-full"
                    style={{ backgroundColor: selectedTrack.color }}
                  />
                  <span className="font-mono font-semibold text-slate-50">
                    {formatTrackId(selectedTrack.trackId)}
                  </span>
                  <span className="text-slate-400">{selectedTrack.className}</span>
                  <span className="font-mono text-teal-200">{selectedTrack.speedKmh} km/h</span>
                </div>
              </div>
            )}

            {isProcessing && (
              <div className="absolute left-1/2 top-1/2 z-20 w-72 -translate-x-1/2 -translate-y-1/2 rounded-md border border-slate-700 bg-slate-950/95 p-3 shadow-panel">
                <div className="flex items-center justify-between gap-3">
                  <p className="text-xs font-semibold text-slate-100">
                    {status === "detecting" ? "Running detector" : "Running tracker"}
                  </p>
                  <span className="font-mono text-[10px] text-teal-200">
                    {frame.frameIndex.toString().padStart(3, "0")}
                  </span>
                </div>
                <div className="mt-2 h-1 overflow-hidden rounded-full bg-slate-800">
                  <div className="h-full w-2/3 rounded-full bg-teal-300" />
                </div>
              </div>
            )}

            {!isProcessing && frame.detections.length === 0 && (
              <div className="absolute left-1/2 top-1/2 z-10 w-72 -translate-x-1/2 -translate-y-1/2 rounded-md border border-slate-700 bg-slate-950/95 p-3 text-center shadow-panel">
                <p className="text-xs font-semibold text-slate-100">No detections above threshold</p>
                <p className="mt-1 text-[11px] text-slate-500">Lower confidence or scrub to another frame.</p>
              </div>
            )}

            {isComplete && !isProcessing && frame.detections.length > 0 && (
              <div className="absolute bottom-2 right-2 rounded border border-slate-700 bg-slate-950/95 px-2 py-1 font-mono text-[10px] text-emerald-200">
                complete · {frame.tracks.length} tracks
              </div>
            )}

            <DetectionOverlay
              detections={frame.detections}
              selectedTrackId={selectedTrackId}
              onSelectTrack={onSelectTrack}
              tracks={frame.tracks}
            />
          </div>
        </div>
      </div>

      <div className="border-t border-slate-800 bg-[#0a0d11] px-2 py-2">
        <div className="flex items-center gap-2">
          <button
            type="button"
            className={controlButton}
            onClick={() => onFrameChange(Math.max(frame.frameIndex - 1, 0))}
            aria-label="Previous frame"
          >
            <SkipBack size={13} weight="bold" />
          </button>
          <button
            type="button"
            className="inline-flex h-7 items-center justify-center gap-1.5 rounded-md border border-teal-300 bg-teal-300 px-2 text-xs font-semibold text-slate-950 transition hover:bg-teal-200 active:translate-y-px"
            onClick={onTogglePlayback}
          >
            {isPlaying ? <Pause size={13} weight="bold" /> : <Play size={13} weight="bold" />}
            {isPlaying ? "Pause" : "Play"}
          </button>
          <button
            type="button"
            className={controlButton}
            onClick={() => onFrameChange(Math.min(frame.frameIndex + 1, frameMax))}
            aria-label="Next frame"
          >
            <SkipForward size={13} weight="bold" />
          </button>

          <div className="relative mx-1 flex h-8 min-w-0 flex-1 items-center">
            <div className="pointer-events-none absolute inset-x-0 top-1 flex h-2 items-end gap-px px-[2px]">
              {frameResults.map((item) => (
                <span
                  key={item.frameIndex}
                  className="min-w-0 flex-1 rounded-sm bg-slate-700"
                  style={{
                    height: Math.max(2, (item.objectCount / maxObjects) * 8) + "px",
                    opacity: item.objectCount > 0 ? 0.65 : 0.18,
                  }}
                />
              ))}
            </div>
            <input
              type="range"
              min={0}
              max={frameMax}
              value={Math.min(frame.frameIndex, frameMax)}
              onChange={(event) => onFrameChange(Number(event.target.value))}
              className="range-control relative z-10"
              aria-label="Current frame"
            />
          </div>

          <span className="w-20 text-right font-mono text-[11px] text-slate-400">
            {frame.frameIndex.toString().padStart(3, "0")} / {frameMax}
          </span>
        </div>
      </div>
    </section>
  );
}
