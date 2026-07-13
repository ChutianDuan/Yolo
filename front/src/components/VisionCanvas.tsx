import { useEffect, useRef } from "react";
import { ImageSquare } from "@phosphor-icons/react";
import type { FrameResult, MediaKind, VisionTaskStatus } from "../types/vision";
import type { MediaDimensions } from "../services/visionApi";
import { DetectionOverlay } from "./DetectionOverlay";

interface VisionCanvasProps {
  frame: FrameResult;
  mediaUrl?: string;
  mediaDimensions?: MediaDimensions;
  mediaName?: string;
  mediaKind: MediaKind;
  selectedTrackId: number;
  status: VisionTaskStatus;
  statusDetail: string;
  isPlaying: boolean;
  responseSource: string;
  onSelectTrack: (trackId: number) => void;
}

export function VisionCanvas({
  frame,
  mediaUrl,
  mediaDimensions,
  mediaName,
  mediaKind,
  selectedTrackId,
  status,
  statusDetail,
  isPlaying,
  responseSource,
  onSelectTrack,
}: VisionCanvasProps) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const processing = status === "detecting" || status === "tracking";
  const usesAnalysisCanvas =
    mediaKind === "video" && !!mediaName && /\.(avi|mjpeg|mjpg)$/i.test(mediaName);
  const frameStyle = mediaDimensions
    ? { aspectRatio: mediaDimensions.width + " / " + mediaDimensions.height }
    : undefined;

  useEffect(() => {
    if (mediaKind !== "video") return undefined;
    const video = videoRef.current;
    if (!video) return undefined;
    const nextTime = Math.max(frame.timestampMs / 1000, 0);
    const syncTime = () => {
      if (Number.isFinite(nextTime) && Math.abs(video.currentTime - nextTime) > 0.08) {
        video.currentTime = nextTime;
      }
    };
    syncTime();
    video.addEventListener("loadedmetadata", syncTime);
    return () => video.removeEventListener("loadedmetadata", syncTime);
  }, [frame.timestampMs, mediaKind, mediaUrl]);

  useEffect(() => {
    const video = videoRef.current;
    if (mediaKind !== "video" || !video) return;
    if (isPlaying) {
      void video.play().catch(() => undefined);
    } else {
      video.pause();
    }
  }, [isPlaying, mediaKind, mediaUrl]);

  const emptyResult = status === "completed" && frame.detections.length === 0;
  const hasMedia = Boolean(mediaUrl) || status === "demo";

  return (
    <section className="bg-[#f6f7f9] px-3 pb-2 pt-3" aria-labelledby="canvas-title">
      <div className="mb-2 flex items-center justify-between">
        <h2 id="canvas-title" className="text-[11px] font-semibold text-[#263247]">
          Vision Canvas
        </h2>
        <span className="font-mono text-[8px] text-[#8992a2]">
          {mediaDimensions
            ? mediaDimensions.width + " x " + mediaDimensions.height
            : "source dimensions not reported"}
        </span>
      </div>

      <div className="mx-auto flex max-h-[52vh] w-full items-center justify-center">
        <div
          className="technical-frame relative aspect-video max-h-[52vh] w-full overflow-hidden border border-[#cfd5df] bg-[#eef1f5]"
          style={frameStyle}
        >
          {mediaUrl && !usesAnalysisCanvas ? (
            mediaKind === "image" ? (
              <img
                src={mediaUrl}
                alt={mediaName ? "Selected media: " + mediaName : "Selected image"}
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
                aria-label={mediaName ? "Selected video: " + mediaName : "Selected video"}
              />
            )
          ) : (
            <div className="sensor-grid absolute inset-0" />
          )}

          {hasMedia && (
            <>
              <div className="pointer-events-none absolute inset-x-0 top-0 h-10 bg-gradient-to-b from-[#111827]/35 to-transparent" />
              <div className="pointer-events-none absolute left-2 top-2 max-w-[62%] bg-[#fbfcfd]/92 px-2 py-1 font-mono text-[9px] text-[#273348]">
                {mediaKind} · frame {String(frame.frameIndex).padStart(3, "0")} · {frame.tracks.length} tracks · {frame.tracksSource}
              </div>
              <div className="pointer-events-none absolute right-2 top-2 max-w-[34%] truncate bg-[#fbfcfd]/92 px-2 py-1 font-mono text-[9px] text-[#657188]">
                {mediaName ?? "Demo replay"} / {responseSource}
              </div>
            </>
          )}

          {!hasMedia && status === "idle" && (
            <div className="absolute inset-0 flex flex-col items-center justify-center text-center">
              <ImageSquare size={24} className="text-[#8791a2]" />
              <p className="mt-3 text-[11px] font-semibold text-[#4a566a]">No media staged</p>
              <p className="mt-1 max-w-xs text-[9px] leading-4 text-[#8992a2]">
                Upload an image or video, then run the corresponding Drogon endpoint.
              </p>
            </div>
          )}

          {status === "uploading" && (
            <div className="absolute inset-x-0 bottom-0 border-t border-[#d9dde5] bg-[#fbfcfd]/95 px-3 py-2">
              <p className="text-[10px] font-semibold text-[#344054]">Media ready</p>
              <p className="mt-0.5 text-[9px] text-[#7d8798]">Run to submit the staged multipart request.</p>
            </div>
          )}

          {processing && (
            <div className="absolute inset-x-0 bottom-0 border-t border-[#9eb1d9] bg-[#f1f5fc]/95 px-3 py-2">
              <div className="flex items-center justify-between gap-4">
                <p className="text-[10px] font-semibold text-[#3559a8]">
                  Requesting {mediaKind === "video" ? "/infer_video" : "/infer"}
                </p>
                <span className="request-pulse h-px w-24 bg-[#3559a8]" />
              </div>
              <p className="mt-0.5 text-[9px] text-[#6f7b90]">
                The HTTP endpoint does not report per-node progress.
              </p>
            </div>
          )}

          {status === "failed" && (
            <div className="absolute inset-x-0 bottom-0 border-t border-[#d9aeb2] bg-[#fff7f7]/95 px-3 py-2">
              <p className="text-[10px] font-semibold text-[#b43c45]">Request failed</p>
              <p className="mt-0.5 break-words font-mono text-[9px] leading-4 text-[#6d4850]">
                {statusDetail}
              </p>
            </div>
          )}

          {emptyResult && (
            <div className="absolute inset-x-0 bottom-0 border-t border-[#d9dde5] bg-[#fbfcfd]/95 px-3 py-2">
              <p className="text-[10px] font-semibold text-[#344054]">
                {frame.tracksSource === "empty"
                  ? "No detections on this frame"
                  : "No detections above UI threshold"}
              </p>
              <p className="mt-0.5 text-[9px] text-[#7d8798]">
                Adjust the UI score filter or inspect another frame.
              </p>
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

      {usesAnalysisCanvas && (
        <p className="mt-2 border-l-2 border-[#b98132] pl-2 text-[9px] leading-4 text-[#76572b]">
          Browser preview is unavailable for this AVI/MJPEG file. Detection boxes remain available on the analysis canvas.
        </p>
      )}
    </section>
  );
}
