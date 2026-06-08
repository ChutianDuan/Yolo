export type MediaKind = "image" | "video";

export type TrackerAlgorithm = "ByteTrack" | "SORT" | "DeepSORT";

export type VisionTaskStatus =
  | "idle"
  | "uploading"
  | "detecting"
  | "tracking"
  | "completed"
  | "failed";

export type EventLogLevel = "info" | "success" | "warning";

export interface BoundingBox {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface DetectionResult {
  id: string;
  frameIndex: number;
  className: string;
  confidence: number;
  bbox: BoundingBox;
  trackId?: number;
}

export interface TrackResult {
  trackId: number;
  className: string;
  firstFrame: number;
  lastFrame: number;
  frames: number[];
  averageConfidence: number;
  status: "active" | "occluded" | "exited";
  color: string;
  speedKmh: number;
  region: string;
}

export interface FrameResult {
  frameIndex: number;
  timestampMs: number;
  detections: DetectionResult[];
  tracks: TrackResult[];
  objectCount: number;
  classCounts: Record<string, number>;
}

export interface InferenceMetrics {
  fps: number;
  latencyMs: number;
  preprocessMs: number;
  inferenceMs: number;
  postprocessMs: number;
  objectCount: number;
  activeTracks: number;
}

export interface EventLogEntry {
  id: string;
  time: string;
  level: EventLogLevel;
  message: string;
  detail: string;
}

export interface ModelOption {
  id: string;
  label: string;
  runtime: string;
}
