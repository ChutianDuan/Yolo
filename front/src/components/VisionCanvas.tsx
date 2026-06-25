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
  framePosition: number;
  mediaUrl?: string;
  mediaDimensions?: MediaDimensions;
  mediaName: string;
  mediaKind: MediaKind;
  selectedTrackId: number;
  status: VisionTaskStatus;
  statusDetail: string;
  isPlaying: boolean;
  onSelectTrack: (trackId: number) => void;
  onFrameChange: (frameIndex: number) => void;
  onTogglePlayback: () => void;
}

const controlButton =
  "inline-flex h-8 w-8 items-center justify-center rounded-md border border-slate-200 bg-white text-slate-700 transition duration-200 hover:border-slate-300 hover:bg-slate-50 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-teal-600 focus-visible:ring-offset-2 active:translate-y-px";

const formatTrackId = (trackId: number) =>
  "#" + trackId.toString().padStart(2, "0");

export function VisionCanvas({
  frame,
  frameResults,
  frameCount,
  framePosition,
  mediaUrl,
  mediaDimensions,
  mediaName,
  mediaKind,
  selectedTrackId,
  status,
  statusDetail,
  isPlaying,
  onSelectTrack,
  onFrameChange,
  onTogglePlayback,
}: VisionCanvasProps) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const selectedTrack = frame.tracks.find((track) => track.trackId === selectedTrackId);
  const isProcessing = status === "detecting" || status === "tracking";
  const isFailed = status === "failed";
  const isComplete = status === "completed";
  const isStaged = status === "uploading";
  const maxObjects = Math.max(...frameResults.map((item) => item.objectCount), 1);
  const hasAnyDetection = frameResults.some((item) => item.objectCount > 0);
  const frameMax = Math.max(frameCount - 1, 0);
  const canStepTimeline = frameCount > 1;
  const canPlayTimeline =
    canStepTimeline && !isProcessing && !isFailed && !isStaged;
  const usesAnalysisCanvas =
    mediaKind === "video" && /\.(avi|mjpeg|mjpg)$/i.test(mediaName);
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
    <section className="flex min-h-0 flex-1 flex-col bg-white">
      <div className="flex min-h-0 flex-1 items-center justify-center bg-slate-50 p-3 sm:p-4">
        <div className="w-full max-w-[1280px]">
          <div
            className="technical-frame relative aspect-video overflow-hidden rounded-lg border border-slate-200 shadow-panel"
            style={frameStyle}
          >
            {mediaUrl && !usesAnalysisCanvas ? (
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
                <div className="absolute left-0 top-0 h-[42%] w-full bg-gradient-to-b from-white/80 to-transparent" />
                <div className="absolute left-[8%] top-[18%] h-[18%] w-[34%] rounded-sm bg-white/70 shadow-sm" />
                <div className="absolute right-[13%] top-[16%] h-[22%] w-[16%] rounded-sm bg-slate-200/80 shadow-sm" />
                <div className="road-plane" />
                <div className="sensor-grid" />
              </>
            )}
            {usesAnalysisCanvas && (
              <div className="pointer-events-none absolute left-3 bottom-3 z-10 max-w-[22rem] rounded-md border border-amber-200 bg-amber-50/95 px-3 py-2 text-[11px] leading-5 text-amber-900 shadow-sm">
                Browser preview unavailable for this AVI/MJPEG file. Showing backend detection
                boxes on an analysis canvas.
              </div>
            )}
            {mediaUrl && (
              <div className="pointer-events-none absolute inset-0 bg-gradient-to-b from-white/5 via-transparent to-slate-950/10" />
            )}

            <div className="absolute left-2 top-2 rounded-md border border-slate-200 bg-white/95 px-2 py-1 font-mono text-[10px] font-medium text-slate-700 shadow-sm">
              {mediaKind === "video" ? "video" : "image"} · frame {frame.frameIndex.toString().padStart(3, "0")} · {frame.objectCount} boxes
            </div>
            <div className="absolute right-2 top-2 max-w-[60%] truncate rounded-md border border-slate-200 bg-white/95 px-2 py-1 font-mono text-[10px] text-slate-500 shadow-sm">
              {mediaName}
            </div>

            {selectedTrack && (
              <div className="absolute bottom-2 left-2 rounded-md border border-slate-200 bg-white/95 px-2 py-1.5 shadow-sm">
                <div className="flex items-center gap-2 text-[11px]">
                  <span
                    className="h-2 w-2 rounded-full"
                    style={{ backgroundColor: selectedTrack.color }}
                  />
                  <span className="font-mono font-semibold text-slate-950">
                    {formatTrackId(selectedTrack.trackId)}
                  </span>
                  <span className="text-slate-500">{selectedTrack.className}</span>
                  <span className="font-mono font-medium text-teal-700">{selectedTrack.speedKmh} km/h</span>
                </div>
              </div>
            )}

            {isProcessing && (
              <div className="absolute left-1/2 top-1/2 z-20 w-72 -translate-x-1/2 -translate-y-1/2 rounded-lg border border-slate-200 bg-white/95 p-3 shadow-panel">
                <div className="flex items-center justify-between gap-3">
                  <p className="text-xs font-semibold text-slate-950">
                    {status === "detecting" ? "Running detector" : "Running video inference"}
                  </p>
                  <span className="font-mono text-[10px] text-teal-700">
                    {frame.frameIndex.toString().padStart(3, "0")}
                  </span>
                </div>
                <div className="mt-2 h-1 overflow-hidden rounded-full bg-slate-100">
                  <div className="h-full w-2/3 rounded-full bg-teal-700" />
                </div>
              </div>
            )}

            {isFailed && (
              <div className="absolute left-1/2 top-1/2 z-20 w-80 -translate-x-1/2 -translate-y-1/2 rounded-lg border border-rose-200 bg-white/95 p-4 text-center shadow-panel">
                <p className="text-xs font-semibold text-rose-700">Inference failed</p>
                <p className="mt-1 text-[11px] leading-5 text-slate-600">{statusDetail}</p>
              </div>
            )}

            {isStaged && !isProcessing && (
              <div className="absolute left-1/2 top-1/2 z-10 w-80 -translate-x-1/2 -translate-y-1/2 rounded-lg border border-slate-200 bg-white/95 p-4 text-center shadow-panel">
                <p className="text-xs font-semibold text-slate-950">Media ready</p>
                <p className="mt-1 text-[11px] leading-5 text-slate-600">
                  Click Run to send this file to the Drogon API.
                </p>
              </div>
            )}

            {isComplete && !isFailed && !isProcessing && frame.detections.length === 0 && (
              <div className="absolute left-1/2 top-1/2 z-10 w-72 -translate-x-1/2 -translate-y-1/2 rounded-lg border border-slate-200 bg-white/95 p-3 text-center shadow-panel">
                <p className="text-xs font-semibold text-slate-950">
                  {hasAnyDetection ? "No detections on this frame" : "No detections above threshold"}
                </p>
                <p className="mt-1 text-[11px] text-slate-500">
                  {hasAnyDetection
                    ? "Playback will continue, or scrub to a frame with boxes."
                    : "Lower confidence or try another file."}
                </p>
              </div>
            )}

            {isComplete && !isProcessing && frame.detections.length > 0 && (
              <div className="absolute bottom-2 right-2 rounded-md border border-emerald-200 bg-emerald-50 px-2 py-1 font-mono text-[10px] font-medium text-emerald-700">
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

      <div className="border-t border-slate-200 bg-white px-3 py-2">
        <div className="flex items-center gap-2">
          <button
            type="button"
            className={controlButton + " disabled:cursor-not-allowed disabled:opacity-45"}
            onClick={() => onFrameChange(Math.max(framePosition - 1, 0))}
            disabled={!canStepTimeline}
            aria-label="Previous frame"
          >
            <SkipBack size={13} weight="bold" />
          </button>
          <button
            type="button"
            className="inline-flex h-8 items-center justify-center gap-1.5 rounded-md border border-teal-700 bg-teal-700 px-3 text-xs font-semibold text-white transition duration-200 hover:border-teal-800 hover:bg-teal-800 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-teal-600 focus-visible:ring-offset-2 active:translate-y-px disabled:cursor-not-allowed disabled:border-slate-200 disabled:bg-slate-100 disabled:text-slate-400"
            onClick={onTogglePlayback}
            disabled={!canPlayTimeline}
            aria-disabled={!canPlayTimeline}
          >
            {isPlaying ? <Pause size={13} weight="bold" /> : <Play size={13} weight="bold" />}
            {isPlaying ? "Pause" : "Play"}
          </button>
          <button
            type="button"
            className={controlButton + " disabled:cursor-not-allowed disabled:opacity-45"}
            onClick={() => onFrameChange(Math.min(framePosition + 1, frameMax))}
            disabled={!canStepTimeline}
            aria-label="Next frame"
          >
            <SkipForward size={13} weight="bold" />
          </button>

          <div className="relative mx-1 flex h-8 min-w-0 flex-1 items-center">
            <div className="pointer-events-none absolute inset-x-0 top-1 flex h-2 items-end gap-px px-[2px]">
              {frameResults.map((item) => (
                <span
                  key={item.frameIndex}
                  className="min-w-0 flex-1 rounded-sm bg-teal-700"
                  style={{
                    height: Math.max(2, (item.objectCount / maxObjects) * 8) + "px",
                    opacity: item.objectCount > 0 ? 0.75 : 0.2,
                  }}
                />
              ))}
            </div>
            <input
              type="range"
              min={0}
              max={frameMax}
              value={Math.min(framePosition, frameMax)}
              onChange={(event) => onFrameChange(Number(event.target.value))}
              disabled={!canStepTimeline}
              className="range-control relative z-10"
              aria-label="Current frame"
            />
          </div>

          <span className="w-28 text-right font-mono text-[11px] text-slate-500">
            f{frame.frameIndex.toString().padStart(3, "0")} · {framePosition + 1}/{Math.max(frameCount, 1)}
          </span>
        </div>
      </div>
    </section>
  );
}
