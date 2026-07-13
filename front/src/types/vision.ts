export type MediaKind = "image" | "video";

export type TrackerAlgorithm = "ByteTrack" | "SORT" | "DeepSORT";

export type FrameSource =
  | "async_corrected"
  | "detected"
  | "weak_tracked"
  | "interpolated"
  | "empty"
  | "not_reported";

export type VisionTaskStatus =
  | "idle"
  | "demo"
  | "uploading"
  | "detecting"
  | "tracking"
  | "completed"
  | "failed";

export type DrogonRequestState =
  | "not_checked"
  | "requesting"
  | "responded"
  | "failed";

export type EventLogLevel = "info" | "success" | "warning" | "error";

export type RunEventKind =
  | "media_selected"
  | "demo_replay"
  | "request_started"
  | "upload_sent"
  | "response_received"
  | "response_parsed"
  | "frames_mapped"
  | "overlay_ready"
  | "request_failed";

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
}

export interface FrameResult {
  frameIndex: number;
  timestampMs: number;
  detections: DetectionResult[];
  tracks: TrackResult[];
  objectCount: number;
  classCounts: Record<string, number>;
  isDetectionFrame: boolean;
  tracksSource: FrameSource;
  correctedFromFrameIndex?: number;
  correctionLatencyFrames?: number;
}

export interface InferenceMetrics {
  fps?: number;
  latencyMs?: number;
  totalElapsedMs?: number;
  decodeMs?: number;
  preprocessMs?: number;
  inferenceMs?: number;
  modelPostprocessMs?: number;
  trackerMs?: number;
  queueWaitMs?: number;
  opticalFlowMs?: number;
  trackingPostprocessMs?: number;
  objectCount: number;
  activeTracks: number;
}

export interface HighLowDiagnostics {
  stableTrackCount?: number;
  provisionalTrackCount?: number;
  maxStableTrackCount?: number;
  maxProvisionalTrackCount?: number;
  provisionalCreatedCount?: number;
  provisionalPromotedCount?: number;
  provisionalExpiredCount?: number;
  provisionalDeduplicatedCount?: number;
  stableStableDuplicateCount?: number;
  stableProvisionalDuplicateCount?: number;
  provisionalProvisionalDuplicateCount?: number;
  outputSuppressedDuplicateCount?: number;
  lowResGeometryRejectionCount?: number;
  lowResClassConflictCount?: number;
  flowTrackCount?: number;
  flowRejectedTrackCount?: number;
  flowLowPointRejectionCount?: number;
  flowInvalidRatioRejectionCount?: number;
  flowForwardBackwardRejectionCount?: number;
  flowMotionDispersionRejectionCount?: number;
  flowMotionJumpRejectionCount?: number;
  flowBoundaryRejectionCount?: number;
  directFlowUpdateCount?: number;
  globalFlowUpdateCount?: number;
  flowAgeOutputSuppressionCount?: number;
  flowAgeExpiredCount?: number;
  exitingTrackSuppressionCount?: number;
  urgentLowResDetectionCount?: number;
  urgentHighResDetectionCount?: number;
  urgentFlowQualityCount?: number;
  urgentTrackChangeCount?: number;
  urgentDuplicateCount?: number;
  urgentFlowAgeCount?: number;
  urgentGeometryCount?: number;
  urgentClassConflictCount?: number;
}

export interface RuntimeDetails {
  trackingStatus?: string;
  sourceFps?: number;
  targetDetectFps?: number;
  effectiveDetectFps?: number;
  frameStride?: number;
  strideMode?: string;
  onnxAsync?: boolean;
  baseFrameStride?: number;
  minFrameStrideUsed?: number;
  maxFrameStrideUsed?: number;
  finalFrameStride?: number;
  frameCount?: number;
  sourceFrameCount?: number;
  processedFrameCount?: number;
  displayFrameCount?: number;
  detectedFrameCount?: number;
  asyncInferRequestCount?: number;
  asyncCorrectionCount?: number;
  asyncCorrectedFrameCount?: number;
  forcedDetectionCount?: number;
  scheduledDetectionCount?: number;
  skippedDetectionCount?: number;
  weakTrackedFrameCount?: number;
  interpolatedFrameCount?: number;
  emptyFrameCount?: number;
  outputShapes?: number[][];
  highLowDiagnostics?: HighLowDiagnostics;
}

export interface RequestMetadata {
  endpoint: string;
  method: "POST";
  fieldName: "image" | "video";
  httpStatus: number;
  contentType: string;
  elapsedMs: number;
  responseBytes: number;
}

export interface EventLogEntry {
  id: string;
  time: string;
  level: EventLogLevel;
  kind: RunEventKind;
  message: string;
  detail: string;
  endpoint?: string;
  method?: string;
  fieldName?: string;
  fileName?: string;
  httpStatus?: number;
  contentType?: string;
  elapsedMs?: number;
  frameCount?: number;
  trackCount?: number;
}

export interface MediaMetadata {
  name: string;
  mime: string;
  sizeBytes: number;
  width?: number;
  height?: number;
  durationMs?: number;
  browserFrameCount?: number;
}
