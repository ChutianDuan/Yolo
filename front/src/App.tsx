import { useEffect, useMemo, useRef, useState } from "react";
import { InferenceFlow } from "./components/InferenceFlow";
import { MetricsBar } from "./components/MetricsBar";
import { ResultPanel } from "./components/ResultPanel";
import { SystemBaseline } from "./components/SystemBaseline";
import { TopBar } from "./components/TopBar";
import { TrackingTimeline } from "./components/TrackingTimeline";
import { UploadPanel } from "./components/UploadPanel";
import { VisionCanvas } from "./components/VisionCanvas";
import {
  initialEventLog,
  mockFrameResults,
  mockMetrics,
  mockTracks,
} from "./data/mockDetections";
import {
  DROGON_API,
  inferImage,
  inferVideo,
  VisionApiError,
  type MediaDimensions,
  type VisionApiResult,
} from "./services/visionApi";
import type {
  DrogonRequestState,
  EventLogEntry,
  FrameResult,
  InferenceMetrics,
  MediaKind,
  MediaMetadata,
  RequestMetadata,
  RuntimeDetails,
  TrackResult,
  VisionTaskStatus,
} from "./types/vision";

const initialConfidenceThreshold = 0.55;
const videoExtensions = new Set([".avi", ".mjpeg", ".mjpg", ".mkv", ".mov", ".mp4", ".webm"]);
const imageExtensions = new Set([".bmp", ".gif", ".jpeg", ".jpg", ".png", ".tif", ".tiff", ".webp"]);

const emptyFrame = (): FrameResult => ({
  frameIndex: 0,
  timestampMs: 0,
  detections: [],
  tracks: [],
  objectCount: 0,
  classCounts: {},
  isDetectionFrame: false,
  tracksSource: "empty",
});

const emptyMetrics = (): InferenceMetrics => ({ objectCount: 0, activeTracks: 0 });

const formatEventTime = () => {
  const now = new Date();
  return new Intl.DateTimeFormat("en-US", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  }).format(now) + "." + String(now.getMilliseconds()).padStart(3, "0");
};

const fileExtension = (fileName: string) => {
  const index = fileName.lastIndexOf(".");
  return index >= 0 ? fileName.slice(index).toLowerCase() : "";
};

const detectMediaKind = (file: File): MediaKind => {
  const mime = file.type.toLowerCase();
  if (mime.startsWith("video/")) return "video";
  if (mime.startsWith("image/")) return "image";
  const extension = fileExtension(file.name);
  if (videoExtensions.has(extension)) return "video";
  if (imageExtensions.has(extension)) return "image";
  return "image";
};

const getImageDimensions = (url: string): Promise<MediaDimensions> =>
  new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve({ width: image.naturalWidth, height: image.naturalHeight });
    image.onerror = () => reject(new Error("Browser could not read image dimensions"));
    image.src = url;
  });

const getVideoMetadata = (
  url: string,
): Promise<{ dimensions?: MediaDimensions; durationMs?: number }> =>
  new Promise((resolve, reject) => {
    const video = document.createElement("video");
    video.preload = "metadata";
    video.onloadedmetadata = () =>
      resolve({
        dimensions:
          video.videoWidth > 0 && video.videoHeight > 0
            ? { width: video.videoWidth, height: video.videoHeight }
            : undefined,
        durationMs: Number.isFinite(video.duration) ? video.duration * 1000 : undefined,
      });
    video.onerror = () => reject(new Error("Browser preview metadata is unavailable"));
    video.src = url;
  });

const filteredFrameResults = (frames: FrameResult[], threshold: number) =>
  frames.map((frame) => {
    const detections = frame.detections.filter((item) => item.confidence >= threshold);
    const visibleTrackIds = new Set(
      detections.flatMap((item) => item.trackId === undefined ? [] : [item.trackId]),
    );
    const frameTracks = frame.tracks.filter((item) => visibleTrackIds.has(item.trackId));
    const classCounts = detections.reduce<Record<string, number>>((counts, item) => {
      counts[item.className] = (counts[item.className] ?? 0) + 1;
      return counts;
    }, {});
    return {
      ...frame,
      detections,
      tracks: frameTracks,
      objectCount: detections.length,
      classCounts,
    };
  });

function App() {
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [media, setMedia] = useState<MediaMetadata>();
  const [mediaKind, setMediaKind] = useState<MediaKind>("video");
  const [mediaUrl, setMediaUrl] = useState<string>();
  const [mediaDimensions, setMediaDimensions] = useState<MediaDimensions>();
  const [confidenceThreshold, setConfidenceThreshold] = useState(initialConfidenceThreshold);
  const [status, setStatus] = useState<VisionTaskStatus>("idle");
  const [statusDetail, setStatusDetail] = useState("No media staged.");
  const [requestState, setRequestState] = useState<DrogonRequestState>("not_checked");
  const [lastRequest, setLastRequest] = useState<RequestMetadata>();
  const [responseStatus, setResponseStatus] = useState<number>();
  const [rawFrames, setRawFrames] = useState<FrameResult[]>([emptyFrame()]);
  const [tracks, setTracks] = useState<TrackResult[]>([]);
  const [metrics, setMetrics] = useState<InferenceMetrics>(emptyMetrics);
  const [runtime, setRuntime] = useState<RuntimeDetails>({});
  const [events, setEvents] = useState<EventLogEntry[]>([]);
  const [currentFrame, setCurrentFrame] = useState(0);
  const [selectedTrackId, setSelectedTrackId] = useState(0);
  const [isPlaying, setIsPlaying] = useState(false);
  const [isDemo, setIsDemo] = useState(false);

  const contract = mediaKind === "video" ? DROGON_API.video : DROGON_API.image;
  const isBusy = status === "detecting" || status === "tracking";
  const frameResults = useMemo(
    () => filteredFrameResults(rawFrames, confidenceThreshold),
    [confidenceThreshold, rawFrames],
  );
  const currentFrameResult = frameResults[currentFrame] ?? frameResults[0] ?? emptyFrame();
  const currentMetrics: InferenceMetrics = {
    ...metrics,
    objectCount: currentFrameResult.objectCount,
    activeTracks: currentFrameResult.tracks.length,
  };

  const addEvent = (entry: Omit<EventLogEntry, "id" | "time">) => {
    setEvents((previous) => [
      {
        ...entry,
        id: "event-" + Date.now() + "-" + Math.random().toString(16).slice(2),
        time: formatEventTime(),
      },
      ...previous,
    ].slice(0, 30));
  };

  useEffect(() => () => {
    if (mediaUrl) URL.revokeObjectURL(mediaUrl);
  }, [mediaUrl]);

  useEffect(() => {
    if (!isPlaying || frameResults.length <= 1) return undefined;
    const frameDuration = Math.min(
      Math.max(1000 / Math.max(runtime.sourceFps ?? metrics.fps ?? 8, 1), 40),
      500,
    );
    const timer = window.setInterval(() => {
      setCurrentFrame((frame) => frame >= frameResults.length - 1 ? 0 : frame + 1);
    }, frameDuration);
    return () => window.clearInterval(timer);
  }, [frameResults.length, isPlaying, metrics.fps, runtime.sourceFps]);

  useEffect(() => {
    const visibleIds = currentFrameResult.detections.flatMap((item) =>
      item.trackId === undefined ? [] : [item.trackId],
    );
    if (visibleIds.length > 0 && !visibleIds.includes(selectedTrackId)) {
      setSelectedTrackId(visibleIds[0]);
    }
  }, [currentFrameResult, selectedTrackId]);

  const resetResults = () => {
    setRawFrames([emptyFrame()]);
    setTracks([]);
    setMetrics(emptyMetrics());
    setRuntime({});
    setCurrentFrame(0);
    setSelectedTrackId(0);
    setIsPlaying(false);
    setLastRequest(undefined);
    setResponseStatus(undefined);
    setRequestState("not_checked");
  };

  const handleFileSelected = async (file: File) => {
    const nextKind = detectMediaKind(file);
    const nextContract = nextKind === "video" ? DROGON_API.video : DROGON_API.image;
    const nextUrl = URL.createObjectURL(file);

    setSelectedFile(file);
    setMediaKind(nextKind);
    setMediaUrl(nextUrl);
    setMediaDimensions(undefined);
    setMedia({ name: file.name, mime: file.type || "not provided", sizeBytes: file.size });
    setStatus("uploading");
    setStatusDetail("Media staged for POST " + nextContract.path + ".");
    setIsDemo(false);
    setEvents([]);
    resetResults();
    addEvent({
      level: "info",
      kind: "media_selected",
      message: "Media staged for the Drogon request.",
      detail: file.name,
      endpoint: nextContract.path,
      method: "POST",
      fieldName: nextContract.fieldName,
      fileName: file.name,
    });

    try {
      if (nextKind === "image") {
        const dimensions = await getImageDimensions(nextUrl);
        setMediaDimensions(dimensions);
        setMedia((previous) => previous ? { ...previous, ...dimensions } : previous);
      } else {
        const metadata = await getVideoMetadata(nextUrl);
        if (metadata.dimensions) setMediaDimensions(metadata.dimensions);
        setMedia((previous) => previous
          ? {
              ...previous,
              width: metadata.dimensions?.width,
              height: metadata.dimensions?.height,
              durationMs: metadata.durationMs,
            }
          : previous,
        );
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : "Browser metadata is unavailable";
      setStatusDetail(message + ". The backend request can still be attempted.");
      addEvent({
        level: "warning",
        kind: "media_selected",
        message,
        detail: "Preview metadata was not reported by the browser.",
        fileName: file.name,
      });
    }
  };

  const applyInferenceResult = (result: VisionApiResult) => {
    const nextFrames = result.frames.length > 0 ? result.frames : [emptyFrame()];
    const firstVisible = nextFrames.findIndex((frame) => frame.objectCount > 0);
    const nextFrame = firstVisible >= 0 ? firstVisible : 0;
    setRawFrames(nextFrames);
    setTracks(result.tracks);
    setMetrics(result.metrics);
    setRuntime(result.runtime);
    setLastRequest(result.request);
    setResponseStatus(result.request.httpStatus);
    setCurrentFrame(nextFrame);
    setMediaDimensions(result.dimensions ?? mediaDimensions);
    setMedia((previous) => previous && result.dimensions
      ? { ...previous, width: result.dimensions.width, height: result.dimensions.height }
      : previous,
    );
    setSelectedTrackId(
      nextFrames[nextFrame]?.detections[0]?.trackId ?? result.tracks[0]?.trackId ?? 0,
    );
  };

  const handleStartInference = async () => {
    if (!selectedFile || isDemo) return;
    setIsPlaying(false);
    setStatus(mediaKind === "video" ? "tracking" : "detecting");
    setStatusDetail("Requesting " + contract.path + ". Awaiting one HTTP response.");
    setRequestState("requesting");
    setResponseStatus(undefined);
    addEvent({
      level: "info",
      kind: "request_started",
      message: "Drogon inference request started.",
      detail: "Awaiting a non-streaming HTTP response.",
      endpoint: contract.path,
      method: "POST",
      fieldName: contract.fieldName,
      fileName: selectedFile.name,
    });
    addEvent({
      level: "info",
      kind: "upload_sent",
      message: "Multipart body sent with the selected file.",
      detail: selectedFile.name,
      endpoint: contract.path,
      method: "POST",
      fieldName: contract.fieldName,
      fileName: selectedFile.name,
    });

    try {
      let result: VisionApiResult;
      if (mediaKind === "video") {
        result = await inferVideo(selectedFile, 0);
      } else {
        const dimensions = mediaDimensions ?? (mediaUrl ? await getImageDimensions(mediaUrl) : undefined);
        if (!dimensions) throw new Error("Image dimensions are required to map detection boxes");
        result = await inferImage(selectedFile, dimensions, 0);
      }
      applyInferenceResult(result);
      setRequestState("responded");
      setStatus("completed");
      const totalObjects = result.frames.reduce((sum, frame) => sum + frame.objectCount, 0);
      setStatusDetail(
        result.frames.length + " frames mapped, " + result.tracks.length + " tracks, " +
        totalObjects + " response boxes.",
      );
      addEvent({
        level: "success",
        kind: "response_received",
        message: "Drogon returned an HTTP response.",
        detail: result.request.responseBytes + " response bytes",
        ...result.request,
      });
      addEvent({
        level: "success",
        kind: "response_parsed",
        message: "response_json validated and parsed.",
        detail: result.frames.length + " frames in the mapped response.",
        endpoint: result.request.endpoint,
        httpStatus: result.request.httpStatus,
        contentType: result.request.contentType,
      });
      addEvent({
        level: "success",
        kind: "frames_mapped",
        message: "Frame and track fields mapped into the workspace.",
        detail: result.tracks.length + " distinct track IDs.",
        frameCount: result.frames.length,
        trackCount: result.tracks.length,
      });
      addEvent({
        level: "success",
        kind: "overlay_ready",
        message: "Detection overlay and timeline synchronized.",
        detail: "UI score filter " + confidenceThreshold.toFixed(2),
        frameCount: result.frames.length,
        trackCount: result.tracks.length,
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : "Inference request failed";
      const apiError = error instanceof VisionApiError ? error : undefined;
      setStatus("failed");
      setStatusDetail(message);
      setRequestState("failed");
      setResponseStatus(apiError?.httpStatus);
      addEvent({
        level: "error",
        kind: "request_failed",
        message: "Drogon request failed.",
        detail: message,
        endpoint: apiError?.endpoint ?? contract.path,
        method: "POST",
        fieldName: contract.fieldName,
        fileName: selectedFile.name,
        httpStatus: apiError?.httpStatus,
        contentType: apiError?.contentType,
        elapsedMs: apiError?.elapsedMs,
      });
    }
  };

  const handleDemoReplay = () => {
    setSelectedFile(null);
    setMediaUrl(undefined);
    setMediaKind("video");
    setMediaDimensions({ width: 1280, height: 720 });
    setMedia({
      name: "BDD100K demo traffic segment",
      mime: "demo/replay",
      sizeBytes: 0,
      width: 1280,
      height: 720,
      browserFrameCount: mockFrameResults.length,
    });
    setRawFrames(mockFrameResults);
    setTracks(mockTracks);
    setMetrics(mockMetrics);
    setRuntime({
      sourceFps: mockMetrics.fps,
      targetDetectFps: 4,
      frameCount: mockFrameResults.length,
      displayFrameCount: mockFrameResults.length,
      detectedFrameCount: mockFrameResults.filter((frame) => frame.isDetectionFrame).length,
      weakTrackedFrameCount: mockFrameResults.filter((frame) => frame.tracksSource === "weak_tracked").length,
      outputShapes: [[1, 300, 6]],
    });
    setCurrentFrame(42);
    setSelectedTrackId(12);
    setStatus("demo");
    setStatusDetail("Demo replay is isolated from Drogon responses.");
    setRequestState("not_checked");
    setResponseStatus(undefined);
    setLastRequest(undefined);
    setIsDemo(true);
    setIsPlaying(false);
    setEvents(initialEventLog);
  };

  const handleTogglePlayback = () => {
    if (frameResults.length > 1 && !isBusy && status !== "failed" && status !== "uploading") {
      setIsPlaying((playing) => !playing);
    }
  };

  const navigateTo = (target: string) => {
    document.getElementById(target)?.scrollIntoView({ behavior: "smooth", block: "nearest" });
  };

  return (
    <div className="flex min-h-[100dvh] flex-col bg-[#f6f7f9] text-[#202b3c] xl:h-[100dvh] xl:overflow-hidden">
      <a href="#workspace" className="skip-link">Skip to workspace</a>
      <input
        ref={fileInputRef}
        type="file"
        className="sr-only"
        accept="image/*,video/*"
        onChange={(event) => {
          const file = event.target.files?.[0];
          if (file) void handleFileSelected(file);
          event.currentTarget.value = "";
        }}
      />

      <TopBar
        status={status}
        requestState={requestState}
        responseStatus={responseStatus}
        endpoint={contract.path}
        isBusy={isBusy}
        canRun={selectedFile !== null}
        onUploadClick={() => fileInputRef.current?.click()}
        onRun={handleStartInference}
        onNavigate={navigateTo}
      />

      <main className="grid min-h-0 flex-1 grid-cols-1 xl:grid-cols-[250px_minmax(0,1fr)_300px] xl:overflow-hidden">
        <UploadPanel
          media={media}
          mediaKind={mediaKind}
          status={status}
          isBusy={isBusy}
          isDemo={isDemo}
          endpoint={contract.path}
          fieldName={contract.fieldName}
          confidenceThreshold={confidenceThreshold}
          runtime={runtime}
          onUploadClick={() => fileInputRef.current?.click()}
          onConfidenceChange={setConfidenceThreshold}
          onDemoReplay={handleDemoReplay}
        />

        <section className="vision-scrollbar min-h-0 overflow-y-auto bg-[#f6f7f9]">
          <InferenceFlow
            mediaKind={mediaKind}
            status={status}
            endpoint={contract.path}
            metrics={currentMetrics}
            runtime={runtime}
            currentFrame={currentFrameResult}
            errorMessage={status === "failed" ? statusDetail : undefined}
          />
          <MetricsBar metrics={currentMetrics} runtime={runtime} />
          <VisionCanvas
            frame={currentFrameResult}
            mediaUrl={mediaUrl}
            mediaDimensions={mediaDimensions}
            mediaName={media?.name}
            mediaKind={mediaKind}
            selectedTrackId={selectedTrackId}
            status={status}
            statusDetail={statusDetail}
            isPlaying={isPlaying}
            responseSource={isDemo ? "Demo replay" : lastRequest ? "Drogon response" : "media only"}
            onSelectTrack={setSelectedTrackId}
          />
          <TrackingTimeline
            frameResults={frameResults}
            tracks={tracks}
            currentFrame={currentFrame}
            selectedTrackId={selectedTrackId}
            isPlaying={isPlaying}
            status={status}
            onFrameChange={setCurrentFrame}
            onTogglePlayback={handleTogglePlayback}
          />
        </section>

        <ResultPanel
          frame={currentFrameResult}
          tracks={tracks}
          events={events}
          runtime={runtime}
          selectedTrackId={selectedTrackId}
          confidenceThreshold={confidenceThreshold}
          status={status}
          onSelectTrack={setSelectedTrackId}
        />
      </main>

      <SystemBaseline
        mediaKind={mediaKind}
        status={status}
        requestState={requestState}
        runtime={runtime}
      />
    </div>
  );
}

export default App;
