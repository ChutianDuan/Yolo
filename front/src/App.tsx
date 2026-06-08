import { useEffect, useMemo, useRef, useState } from "react";
import { ResultPanel } from "./components/ResultPanel";
import { TopBar } from "./components/TopBar";
import { VisionCanvas } from "./components/VisionCanvas";
import {
  DEFAULT_FRAME,
  initialEventLog,
  mockMetrics,
  modelOptions,
  TOTAL_FRAMES,
} from "./data/mockDetections";
import {
  getAllTracks,
  getFrameResults,
  runDetection,
  runTracking,
} from "./services/mockVisionService";
import type {
  EventLogEntry,
  InferenceMetrics,
  MediaKind,
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

function App() {
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [mediaName, setMediaName] = useState("BDD100K demo traffic segment");
  const [mediaKind, setMediaKind] = useState<MediaKind>("video");
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
  const [metrics, setMetrics] = useState<InferenceMetrics>(mockMetrics);
  const [, setEvents] = useState<EventLogEntry[]>(initialEventLog);

  const tracks = useMemo(() => getAllTracks(), []);
  const currentFrameResult = frameResults[currentFrame] ?? frameResults[0];
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
    setFrameResults(getFrameResults(confidenceThreshold));
  }, [confidenceThreshold]);

  useEffect(() => {
    if (!isPlaying) {
      return undefined;
    }

    const timer = window.setInterval(() => {
      setCurrentFrame((frame) => (frame >= TOTAL_FRAMES - 1 ? 0 : frame + 1));
    }, 520);

    return () => window.clearInterval(timer);
  }, [isPlaying]);

  useEffect(() => {
    const visibleTrackIds = currentFrameResult.detections.flatMap((detection) =>
      detection.trackId === undefined ? [] : [detection.trackId],
    );
    if (visibleTrackIds.length > 0 && !visibleTrackIds.includes(selectedTrackId)) {
      setSelectedTrackId(visibleTrackIds[0]);
    }
  }, [currentFrameResult, selectedTrackId]);

  const handleFileSelected = (file: File) => {
    const nextKind: MediaKind = file.type.startsWith("video") ? "video" : "image";
    setMediaName(file.name);
    setMediaKind(nextKind);
    setStatus("uploading");
    setIsPlaying(false);
    addEvent(
      "success",
      "Media accepted",
      file.name + " registered as " + nextKind + "; mock inference state remains API-compatible.",
    );
  };

  const executeDetection = async () => {
    setStatus("detecting");
    addEvent(
      "info",
      "Detection requested",
      selectedModel.label +
        " running on frame " +
        currentFrame +
        " at " +
        (confidenceThreshold * 100).toFixed(0) +
        "% confidence.",
    );

    const result = await runDetection(currentFrame, confidenceThreshold);
    setFrameResults((previous) =>
      previous.map((frame) => (frame.frameIndex === result.frameIndex ? result : frame)),
    );
    setStatus("completed");
    addEvent(
      "success",
      "Detection complete",
      result.objectCount + " objects passed threshold on frame " + result.frameIndex + ".",
    );
  };

  const executeTracking = async () => {
    setStatus("tracking");
    addEvent(
      "info",
      "Tracking requested",
      tracker +
        " associating detections across " +
        TOTAL_FRAMES +
        " frames with IoU " +
        (iouThreshold * 100).toFixed(0) +
        "%.",
    );

    const result = await runTracking(confidenceThreshold, tracker);
    setFrameResults(result.frames);
    setMetrics(result.metrics);
    setStatus("completed");
    addEvent(
      "success",
      "Tracking complete",
      tracker +
        " produced " +
        tracks.length +
        " track spans with " +
        result.metrics.fps.toFixed(1) +
        " FPS replay metrics.",
    );
  };

  const handleStartInference = async () => {
    await executeDetection();
    await executeTracking();
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
