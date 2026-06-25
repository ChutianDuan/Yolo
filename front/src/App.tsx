import { useEffect, useRef, useState } from "react";
import { ResultPanel } from "./components/ResultPanel";
import { TopBar } from "./components/TopBar";
import { VisionCanvas } from "./components/VisionCanvas";
import {
  DEFAULT_FRAME,
  initialEventLog,
  mockMetrics,
  mockTracks,
} from "./data/mockDetections";
import { getFrameResults } from "./services/mockVisionService";
import {
  DROGON_API,
  inferImage,
  inferVideo,
  type MediaDimensions,
  type VisionApiResult,
} from "./services/visionApi";
import type {
  EventLogEntry,
  FrameResult,
  InferenceMetrics,
  MediaKind,
  TrackResult,
  VisionTaskStatus,
} from "./types/vision";

const initialConfidenceThreshold = 0.55;
const initialStatusDetail = "Upload an image or video to call the Drogon API.";
const videoExtensions = new Set([
  ".avi",
  ".mjpeg",
  ".mjpg",
  ".mkv",
  ".mov",
  ".mp4",
  ".webm",
]);
const imageExtensions = new Set([
  ".bmp",
  ".gif",
  ".jpeg",
  ".jpg",
  ".png",
  ".tif",
  ".tiff",
  ".webp",
]);

const formatEventTime = () =>
  new Intl.DateTimeFormat("en-US", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  }).format(new Date());

const emptyFrame = (): FrameResult => ({
  frameIndex: 0,
  timestampMs: 0,
  detections: [],
  tracks: [],
  objectCount: 0,
  classCounts: {},
});

const getImageDimensions = (url: string): Promise<MediaDimensions> =>
  new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () =>
      resolve({
        width: image.naturalWidth,
        height: image.naturalHeight,
      });
    image.onerror = () => reject(new Error("Failed to load image dimensions"));
    image.src = url;
  });

const errorMessage = (error: unknown) =>
  error instanceof Error ? error.message : "Inference request failed";

const fileExtension = (fileName: string) => {
  const index = fileName.lastIndexOf(".");
  return index >= 0 ? fileName.slice(index).toLowerCase() : "";
};

const detectMediaKind = (file: File): MediaKind => {
  const mime = file.type.toLowerCase();
  if (mime.startsWith("video/")) {
    return "video";
  }
  if (mime.startsWith("image/")) {
    return "image";
  }

  const extension = fileExtension(file.name);
  if (videoExtensions.has(extension)) {
    return "video";
  }
  if (imageExtensions.has(extension)) {
    return "image";
  }

  return "image";
};

const formatBytes = (bytes: number) => {
  if (bytes < 1024) {
    return bytes + " B";
  }
  const megabytes = bytes / (1024 * 1024);
  if (megabytes >= 1) {
    return megabytes.toFixed(1) + " MB";
  }
  return (bytes / 1024).toFixed(1) + " KB";
};

function App() {
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [mediaName, setMediaName] = useState("BDD100K demo traffic segment");
  const [mediaKind, setMediaKind] = useState<MediaKind>("video");
  const [mediaUrl, setMediaUrl] = useState<string>();
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [mediaDimensions, setMediaDimensions] = useState<MediaDimensions>();
  const [confidenceThreshold, setConfidenceThreshold] = useState(initialConfidenceThreshold);
  const [status, setStatus] = useState<VisionTaskStatus>("idle");
  const [statusDetail, setStatusDetail] = useState(initialStatusDetail);
  const [currentFrame, setCurrentFrame] = useState(DEFAULT_FRAME);
  const [selectedTrackId, setSelectedTrackId] = useState(12);
  const [isPlaying, setIsPlaying] = useState(false);
  const [frameResults, setFrameResults] = useState(() =>
    getFrameResults(initialConfidenceThreshold),
  );
  const [tracks, setTracks] = useState<TrackResult[]>(mockTracks);
  const [metrics, setMetrics] = useState<InferenceMetrics>(mockMetrics);
  const [events, setEvents] = useState<EventLogEntry[]>(initialEventLog);

  const currentFrameResult = frameResults[currentFrame] ?? frameResults[0] ?? emptyFrame();
  const isBusy = status === "detecting" || status === "tracking";
  const currentMetrics: InferenceMetrics = {
    ...metrics,
    objectCount: currentFrameResult.objectCount,
    activeTracks: currentFrameResult.tracks.length,
  };

  const addEvent = (
    level: EventLogEntry["level"],
    message: string,
    detail: string,
  ) => {
    setEvents((previous) => [
      {
        id: "event-" + Date.now() + "-" + previous.length,
        time: formatEventTime(),
        level,
        message,
        detail,
      },
      ...previous,
    ].slice(0, 14));
  };

  useEffect(() => {
    if (!selectedFile) {
      setFrameResults(getFrameResults(confidenceThreshold));
      setTracks(mockTracks);
    }
  }, [confidenceThreshold, selectedFile]);

  useEffect(() => {
    return () => {
      if (mediaUrl) {
        URL.revokeObjectURL(mediaUrl);
      }
    };
  }, [mediaUrl]);

  useEffect(() => {
    if (!isPlaying) {
      return undefined;
    }

    const timer = window.setInterval(() => {
      setCurrentFrame((frame) =>
        frame >= frameResults.length - 1 ? 0 : frame + 1,
      );
    }, 520);

    return () => window.clearInterval(timer);
  }, [frameResults.length, isPlaying]);

  useEffect(() => {
    if (frameResults.length <= 1 && isPlaying) {
      setIsPlaying(false);
    }
  }, [frameResults.length, isPlaying]);

  useEffect(() => {
    const visibleTrackIds = currentFrameResult.detections.flatMap((detection) =>
      detection.trackId === undefined ? [] : [detection.trackId],
    );
    if (visibleTrackIds.length > 0 && !visibleTrackIds.includes(selectedTrackId)) {
      setSelectedTrackId(visibleTrackIds[0]);
    }
  }, [currentFrameResult, selectedTrackId]);

  const handleFileSelected = async (file: File) => {
    const nextKind = detectMediaKind(file);
    const nextUrl = URL.createObjectURL(file);
    const contract = nextKind === "video" ? DROGON_API.video : DROGON_API.image;
    const fileDetail =
      file.name +
      " (" +
      formatBytes(file.size) +
      ", MIME " +
      (file.type || "not provided") +
      ") staged as " +
      nextKind +
      "; Run will post multipart field '" +
      contract.fieldName +
      "' to " +
      contract.path +
      ".";

    console.info("[vision] media selected", {
      fileName: file.name,
      fileSize: file.size,
      mime: file.type || null,
      mediaKind: nextKind,
      endpoint: contract.path,
      fieldName: contract.fieldName,
    });

    setSelectedFile(file);
    setMediaUrl(nextUrl);
    setMediaDimensions(undefined);
    setMediaName(file.name);
    setMediaKind(nextKind);
    setStatus("uploading");
    setStatusDetail(fileDetail);
    setIsPlaying(false);
    setCurrentFrame(0);
    setSelectedTrackId(0);
    setFrameResults([emptyFrame()]);
    setTracks([]);
    setMetrics({
      ...mockMetrics,
      objectCount: 0,
      activeTracks: 0,
    });
    addEvent(
      "success",
      "Media accepted",
      fileDetail,
    );

    if (nextKind === "image") {
      try {
        setMediaDimensions(await getImageDimensions(nextUrl));
      } catch (error) {
        const message = errorMessage(error);
        setStatus("failed");
        setStatusDetail(message);
        addEvent("warning", "Image rejected", message);
      }
    }
  };

  const applyInferenceResult = (
    frames: FrameResult[],
    nextTracks: TrackResult[],
    nextMetrics: InferenceMetrics,
    dimensions?: MediaDimensions,
  ) => {
    const nextFrames = frames.length > 0 ? frames : [emptyFrame()];
    const firstVisibleFrameIndex = nextFrames.findIndex((frame) => frame.objectCount > 0);
    const nextFrameIndex = firstVisibleFrameIndex >= 0 ? firstVisibleFrameIndex : 0;

    setFrameResults(nextFrames);
    setTracks(nextTracks);
    setMetrics(nextMetrics);
    setCurrentFrame(nextFrameIndex);
    if (dimensions) {
      setMediaDimensions(dimensions);
    }

    const firstTrackId =
      nextFrames[nextFrameIndex]?.detections[0]?.trackId ?? nextTracks[0]?.trackId ?? 0;
    setSelectedTrackId(firstTrackId);
  };

  const handleStartInference = async () => {
    if (!selectedFile) {
      const message = "Upload an image or video before calling the Drogon API.";
      setStatus("failed");
      setStatusDetail(message);
      addEvent(
        "warning",
        "No media selected",
        message,
      );
      return;
    }

    setIsPlaying(false);
    setStatus(mediaKind === "video" ? "tracking" : "detecting");
    const contract = mediaKind === "video" ? DROGON_API.video : DROGON_API.image;
    const requestDetail =
      "Posting multipart field '" +
      contract.fieldName +
      "' to " +
      contract.path +
      "; UI filters scores below " +
      (confidenceThreshold * 100).toFixed(0) +
      "% confidence.";
    setStatusDetail(requestDetail);
    console.info("[vision] inference start", {
      fileName: selectedFile.name,
      fileSize: selectedFile.size,
      mime: selectedFile.type || null,
      mediaKind,
      endpoint: contract.path,
      fieldName: contract.fieldName,
    });
    addEvent(
      "info",
      mediaKind === "video" ? "Video inference requested" : "Image inference requested",
      requestDetail,
    );

    try {
      let result: VisionApiResult;
      if (mediaKind === "video") {
        result = await inferVideo(selectedFile, confidenceThreshold);
      } else {
        if (!mediaDimensions && !mediaUrl) {
          throw new Error("Image preview is not ready");
        }
        const dimensions = mediaDimensions ?? (await getImageDimensions(mediaUrl ?? ""));
        result = await inferImage(selectedFile, dimensions, confidenceThreshold);
      }

      applyInferenceResult(
        result.frames,
        result.tracks,
        result.metrics,
        result.dimensions,
      );
      const objectCount = result.frames.reduce(
        (sum, frame) => sum + frame.objectCount,
        0,
      );

      setStatus("completed");
      setIsPlaying(mediaKind === "video" && result.frames.length > 1);
      console.info("[vision] inference complete", {
        mediaKind,
        frames: result.frames.length,
        tracks: result.tracks.length,
        objects: objectCount,
      });
      setStatusDetail(
        result.frames.length +
          " frame(s), " +
          objectCount +
          " visible objects, " +
          result.tracks.length +
          " track(s).",
      );
      addEvent(
        "success",
        "Inference complete",
        result.frames.length +
          " frame(s), " +
          objectCount +
          " visible objects, " +
          result.tracks.length +
          " track(s).",
      );
    } catch (error) {
      const message = errorMessage(error);
      console.error("[vision] inference failed", {
        fileName: selectedFile.name,
        mediaKind,
        message,
      });
      setStatus("failed");
      setStatusDetail(message);
      addEvent("warning", "Inference failed", message);
    }
  };

  const handleTogglePlayback = () => {
    if (isBusy || status === "uploading" || status === "failed" || frameResults.length <= 1) {
      return;
    }
    setIsPlaying((playing) => !playing);
  };

  return (
    <main className="h-[100dvh] w-full max-w-full overflow-hidden bg-[#f7f8fb] text-slate-950">
      <input
        ref={fileInputRef}
        type="file"
        className="sr-only"
        accept="image/*,video/*"
        onChange={(event) => {
          const file = event.target.files?.[0];
          if (file) {
            handleFileSelected(file);
          }
        }}
      />

      <div className="mx-auto flex h-full min-h-0 w-full max-w-[1680px] flex-col p-2 sm:p-3">
        <TopBar
          status={status}
          mediaName={mediaName}
          isBusy={isBusy}
          confidenceThreshold={confidenceThreshold}
          metrics={currentMetrics}
          onUploadClick={() => fileInputRef.current?.click()}
          onRun={handleStartInference}
          onConfidenceChange={setConfidenceThreshold}
        />

        <div className="mt-2 grid min-h-0 flex-1 grid-cols-1 grid-rows-[minmax(0,1fr)_260px] overflow-hidden rounded-lg border border-slate-200 bg-white shadow-shell lg:mt-3 lg:grid-cols-[minmax(0,1fr)_320px] lg:grid-rows-1">
          <VisionCanvas
            frame={currentFrameResult}
            frameResults={frameResults}
            frameCount={frameResults.length}
            framePosition={Math.min(currentFrame, Math.max(frameResults.length - 1, 0))}
            mediaUrl={mediaUrl}
            mediaDimensions={mediaDimensions}
            mediaName={mediaName}
            mediaKind={mediaKind}
            selectedTrackId={selectedTrackId}
            isPlaying={isPlaying}
            status={status}
            statusDetail={statusDetail}
            onSelectTrack={setSelectedTrackId}
            onFrameChange={setCurrentFrame}
            onTogglePlayback={handleTogglePlayback}
          />
          <ResultPanel
            frame={currentFrameResult}
            tracks={tracks}
            events={events}
            selectedTrackId={selectedTrackId}
            confidenceThreshold={confidenceThreshold}
            status={status}
            onSelectTrack={setSelectedTrackId}
          />
        </div>
      </div>
    </main>
  );
}

export default App;
