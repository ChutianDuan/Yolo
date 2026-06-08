import type {
  DetectionResult,
  EventLogEntry,
  FrameResult,
  InferenceMetrics,
  ModelOption,
  TrackResult,
} from "../types/vision";

export const TOTAL_FRAMES = 120;
export const DEFAULT_FRAME = 42;

export const modelOptions: ModelOption[] = [
  {
    id: "yolo26s-int8",
    label: "YOLO26s INT8 ONNX",
    runtime: "ONNX Runtime CPU",
  },
  {
    id: "yolo26s-fp16",
    label: "YOLO26s FP16 TensorRT",
    runtime: "TensorRT CUDA",
  },
  {
    id: "yolo11n-edge",
    label: "YOLO11n Edge",
    runtime: "OpenCV DNN",
  },
];

interface ObjectTemplate {
  trackId: number;
  className: string;
  firstFrame: number;
  lastFrame: number;
  confidence: number;
  bbox: {
    x: number;
    y: number;
    width: number;
    height: number;
  };
  drift: {
    x: number;
    y: number;
    width: number;
    height: number;
  };
  color: string;
  speedKmh: number;
  region: string;
  phase: number;
}

export const objectTemplates: ObjectTemplate[] = [
  {
    trackId: 12,
    className: "car",
    firstFrame: 0,
    lastFrame: 119,
    confidence: 0.96,
    bbox: { x: 55, y: 48, width: 15, height: 18 },
    drift: { x: -8, y: 7, width: 2.5, height: 2 },
    color: "#38bdf8",
    speedKmh: 46,
    region: "center lane",
    phase: 1,
  },
  {
    trackId: 21,
    className: "bus",
    firstFrame: 8,
    lastFrame: 116,
    confidence: 0.91,
    bbox: { x: 18, y: 39, width: 19, height: 25 },
    drift: { x: 5, y: 3, width: 1.5, height: 1.5 },
    color: "#f59e0b",
    speedKmh: 34,
    region: "left lane",
    phase: 3,
  },
  {
    trackId: 3,
    className: "person",
    firstFrame: 16,
    lastFrame: 94,
    confidence: 0.84,
    bbox: { x: 72, y: 44, width: 5.8, height: 20 },
    drift: { x: -4, y: 2, width: 0.5, height: 0.8 },
    color: "#22c55e",
    speedKmh: 5,
    region: "right shoulder",
    phase: 6,
  },
  {
    trackId: 7,
    className: "truck",
    firstFrame: 0,
    lastFrame: 84,
    confidence: 0.88,
    bbox: { x: 38, y: 35, width: 13.5, height: 19 },
    drift: { x: 8, y: 4, width: 2.8, height: 2.4 },
    color: "#fb7185",
    speedKmh: 39,
    region: "far lane",
    phase: 9,
  },
  {
    trackId: 52,
    className: "traffic light",
    firstFrame: 0,
    lastFrame: 119,
    confidence: 0.78,
    bbox: { x: 83, y: 12, width: 4.5, height: 12 },
    drift: { x: -1, y: 0.5, width: 0.1, height: 0.1 },
    color: "#a3e635",
    speedKmh: 0,
    region: "intersection signal",
    phase: 12,
  },
  {
    trackId: 63,
    className: "motorcycle",
    firstFrame: 22,
    lastFrame: 111,
    confidence: 0.72,
    bbox: { x: 64, y: 63, width: 7.5, height: 14 },
    drift: { x: -11, y: 4, width: 1, height: 1.4 },
    color: "#60a5fa",
    speedKmh: 51,
    region: "right lane",
    phase: 15,
  },
  {
    trackId: 79,
    className: "bicycle",
    firstFrame: 36,
    lastFrame: 102,
    confidence: 0.63,
    bbox: { x: 9, y: 58, width: 8.2, height: 17 },
    drift: { x: 12, y: 1.2, width: 0.8, height: 1 },
    color: "#2dd4bf",
    speedKmh: 18,
    region: "curb lane",
    phase: 18,
  },
  {
    trackId: 88,
    className: "car",
    firstFrame: 52,
    lastFrame: 119,
    confidence: 0.58,
    bbox: { x: 31, y: 57, width: 10.2, height: 15 },
    drift: { x: 3, y: 6, width: 2, height: 2 },
    color: "#f97316",
    speedKmh: 42,
    region: "merge lane",
    phase: 21,
  },
];

const makeFrameRange = (start: number, end: number) =>
  Array.from({ length: end - start + 1 }, (_, index) => start + index);

const clamp = (value: number, min: number, max: number) =>
  Math.min(Math.max(value, min), max);

const round = (value: number, digits = 2) =>
  Number.parseFloat(value.toFixed(digits));

export const mockTracks: TrackResult[] = objectTemplates.map((item) => ({
  trackId: item.trackId,
  className: item.className,
  firstFrame: item.firstFrame,
  lastFrame: item.lastFrame,
  frames: makeFrameRange(item.firstFrame, item.lastFrame),
  averageConfidence: item.confidence,
  status: item.lastFrame >= TOTAL_FRAMES - 4 ? "active" : "exited",
  color: item.color,
  speedKmh: item.speedKmh,
  region: item.region,
}));

const createDetection = (
  template: ObjectTemplate,
  frameIndex: number,
): DetectionResult | null => {
  if (frameIndex < template.firstFrame || frameIndex > template.lastFrame) {
    return null;
  }

  const span = Math.max(template.lastFrame - template.firstFrame, 1);
  const progress = (frameIndex - template.firstFrame) / span;
  const wave = Math.sin((frameIndex + template.phase) / 8) * 0.025;
  const confidence = clamp(template.confidence + wave, 0.3, 0.99);

  return {
    id: `det-${template.trackId}-${frameIndex}`,
    frameIndex: frameIndex,
    className: template.className,
    confidence: round(confidence),
    bbox: {
      x: round(clamp(template.bbox.x + template.drift.x * progress, 3, 92)),
      y: round(clamp(template.bbox.y + template.drift.y * progress, 4, 86)),
      width: round(clamp(template.bbox.width + template.drift.width * progress, 3, 26)),
      height: round(clamp(template.bbox.height + template.drift.height * progress, 6, 34)),
    },
    trackId: template.trackId,
  };
};

const summarizeClasses = (detections: DetectionResult[]) =>
  detections.reduce<Record<string, number>>((summary, detection) => {
    summary[detection.className] = (summary[detection.className] ?? 0) + 1;
    return summary;
  }, {});

const createFrameResult = (frameIndex: number): FrameResult => {
  const detections = objectTemplates
    .map((template) => createDetection(template, frameIndex))
    .filter((detection): detection is DetectionResult => detection !== null);

  const tracks = mockTracks.filter(
    (track) => frameIndex >= track.firstFrame && frameIndex <= track.lastFrame,
  );

  return {
    frameIndex,
    timestampMs: frameIndex * 33.33,
    detections,
    tracks,
    objectCount: detections.length,
    classCounts: summarizeClasses(detections),
  };
};

export const mockFrameResults: FrameResult[] = Array.from(
  { length: TOTAL_FRAMES },
  (_, frameIndex) => createFrameResult(frameIndex),
);

export const mockMetrics: InferenceMetrics = {
  fps: 29.7,
  latencyMs: 33.6,
  preprocessMs: 4.4,
  inferenceMs: 21.8,
  postprocessMs: 7.4,
  objectCount: 0,
  activeTracks: 0,
};

export const initialEventLog: EventLogEntry[] = [
  {
    id: "event-001",
    time: "00:00.000",
    level: "info",
    message: "Demo media loaded",
    detail: "120-frame urban traffic sequence is ready for local inference replay.",
  },
  {
    id: "event-002",
    time: "00:00.118",
    level: "success",
    message: "Detection pass completed",
    detail: "car #12, person #03, truck #07, and five additional objects passed model confidence checks.",
  },
  {
    id: "event-003",
    time: "00:00.174",
    level: "info",
    message: "Track matching updated",
    detail: "ByteTrack associated detections with active trajectories using IoU and motion continuity.",
  },
  {
    id: "event-004",
    time: "00:01.452",
    level: "warning",
    message: "Track #07 temporarily lost",
    detail: "truck #07 was occluded for 6 frames near the far lane boundary.",
  },
  {
    id: "event-005",
    time: "00:01.658",
    level: "success",
    message: "Track #07 recovered",
    detail: "truck #07 was matched again after Kalman prediction and confidence recovery.",
  },
];
