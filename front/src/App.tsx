import { useEffect, useRef, useState } from "react";
import { ResultPanel } from "./components/ResultPanel";
import { TopBar } from "./components/TopBar";
import { VisionCanvas } from "./components/VisionCanvas";
import {
  DEFAULT_FRAME,
  initialEventLog,
  mockMetrics,
  mockTracks,
  modelOptions,
} from "./data/mockDetections";
import { getFrameResults } from "./services/mockVisionService";
import {
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
  TrackerAlgorithm,
  VisionTaskStatus,
} from "./types/vision";

const initialConfidenceThreshold = 0.55;
const initialIouThreshold = 0.45;

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

function App() {
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [mediaName, setMediaName] = useState("BDD100K demo traffic segment");
  const [mediaKind, setMediaKind] = useState<MediaKind>("video");
  const [mediaUrl, setMediaUrl] = useState<string>();
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [mediaDimensions, setMediaDimensions] = useState<MediaDimensions>();
  const [modelId] = useState(modelOptions[0].id);
  const [confidenceThreshold, setConfidenceThreshold] = useState(initialConfidenceThreshold);
  const [iouThreshold] = useState(initialIouThreshold);
  const [tracker, setTracker] = useState<TrackerAlgorithm>("ByteTrack");
  const [status, setStatus] = useState<VisionTaskStatus>("idle");
  const [currentFrame, setCurrentFrame] = useState(DEFAULT_FRAME);
  const [selectedTrackId, setSelectedTrackId] = useState(12);
  const [isPlaying, setIsPlaying] = useState(false);
  const [frameResults, setFrameResults] = useState(() =>
    getFrameResults(initialConfidenceThreshold),
  );
  const [tracks, setTracks] = useState<TrackResult[]>(mockTracks);
  const [metrics, setMetrics] = useState<InferenceMetrics>(mockMetrics);
  const [, setEvents] = useState<EventLogEntry[]>(initialEventLog);

  const currentFrameResult = frameResults[currentFrame] ?? frameResults[0] ?? emptyFrame();
  const isBusy = status === "detecting" || status === "tracking";
  const selectedModel = modelOptions.find((model) => model.id === modelId) ?? modelOptions[0];
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
    const visibleTrackIds = currentFrameResult.detections.flatMap((detection) =>
      detection.trackId === undefined ? [] : [detection.trackId],
    );
    if (visibleTrackIds.length > 0 && !visibleTrackIds.includes(selectedTrackId)) {
      setSelectedTrackId(visibleTrackIds[0]);
    }
  }, [currentFrameResult, selectedTrackId]);

  const handleFileSelected = async (file: File) => {
    const nextKind: MediaKind = file.type.startsWith("video") ? "video" : "image";
    const nextUrl = URL.createObjectURL(file);

    setSelectedFile(file);
    setMediaUrl(nextUrl);
    setMediaDimensions(undefined);
    setMediaName(file.name);
    setMediaKind(nextKind);
    setStatus("uploading");
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
      file.name + " registered as " + nextKind + " for Drogon inference.",
    );

    if (nextKind === "image") {
      try {
        setMediaDimensions(await getImageDimensions(nextUrl));
      } catch (error) {
        setStatus("failed");
        addEvent("warning", "Image rejected", errorMessage(error));
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

    setFrameResults(nextFrames);
    setTracks(nextTracks);
    setMetrics(nextMetrics);
    setCurrentFrame(0);
    if (dimensions) {
      setMediaDimensions(dimensions);
    }

    const firstTrackId =
      nextTracks[0]?.trackId ?? nextFrames[0]?.detections[0]?.trackId ?? 0;
    setSelectedTrackId(firstTrackId);
  };

  const handleStartInference = async () => {
    if (!selectedFile) {
      setStatus("failed");
      addEvent(
        "warning",
        "No media selected",
        "Upload an image or video before calling the Drogon API.",
      );
      return;
    }

    setIsPlaying(false);
    setStatus(mediaKind === "video" ? "tracking" : "detecting");
    addEvent(
      "info",
      mediaKind === "video" ? "Video inference requested" : "Image inference requested",
      selectedModel.label +
        " calling " +
        (mediaKind === "video" ? "/infer_video" : "/infer") +
        " at " +
        (confidenceThreshold * 100).toFixed(0) +
        "% confidence.",
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
      setStatus("failed");
      addEvent("warning", "Inference failed", errorMessage(error));
    }
  };

  const handleExport = () => {
    const payload = {
      media: {
        name: mediaName,
        kind: mediaKind,
      },
      model: selectedModel,
      tracker,
      thresholds: {
        confidence: confidenceThreshold,
        iou: iouThreshold,
      },
      currentFrame: currentFrameResult,
      metrics: currentMetrics,
      exportedAt: new Date().toISOString(),
    };
    const blob = new Blob([JSON.stringify(payload, null, 2)], {
      type: "application/json",
    });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = "visiontrack-frame-" + currentFrame + ".json";
    anchor.click();
    URL.revokeObjectURL(url);
    setStatus("completed");
    addEvent(
      "success",
      "Result exported",
      "JSON export created for frame " +
        currentFrame +
        " with " +
        currentFrameResult.objectCount +
        " visible objects.",
    );
  };

  return (
    <main className="flex h-[100dvh] w-full max-w-full overflow-hidden bg-[#080b0f] text-slate-100">
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

      <div className="flex min-h-0 min-w-0 flex-1 flex-col">
        <TopBar
          status={status}
          mediaName={mediaName}
          isBusy={isBusy}
          tracker={tracker}
          confidenceThreshold={confidenceThreshold}
          metrics={currentMetrics}
          onUploadClick={() => fileInputRef.current?.click()}
          onRun={handleStartInference}
          onTrackerChange={setTracker}
          onConfidenceChange={setConfidenceThreshold}
        />

        <div className="grid min-h-0 flex-1 grid-cols-1 border-x border-slate-800 lg:grid-cols-[minmax(0,1fr)_280px]">
          <VisionCanvas
            frame={currentFrameResult}
            frameResults={frameResults}
            frameCount={frameResults.length}
            mediaUrl={mediaUrl}
            mediaDimensions={mediaDimensions}
            mediaName={mediaName}
            mediaKind={mediaKind}
            selectedTrackId={selectedTrackId}
            isPlaying={isPlaying}
            status={status}
            onSelectTrack={setSelectedTrackId}
            onFrameChange={setCurrentFrame}
            onTogglePlayback={() => setIsPlaying((playing) => !playing)}
          />
          <ResultPanel
            frame={currentFrameResult}
            tracks={tracks}
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
