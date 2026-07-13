import type {
  DetectionResult,
  FrameResult,
  FrameSource,
  HighLowDiagnostics,
  InferenceMetrics,
  RequestMetadata,
  RuntimeDetails,
  TrackResult,
} from "../types/vision";

interface BackendBox {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

interface BackendDetection {
  class_id: number;
  class_name?: string;
  score: number;
  box: BackendBox;
}

interface BackendTrack extends BackendDetection {
  track_id: number;
}

interface BackendTiming {
  total_elapsed_ms?: number;
  end_to_end_ms?: number;
  decode_ms?: number;
  preprocess_ms?: number;
  infer_ms?: number;
  onnx_inference_ms?: number;
  postprocess_ms?: number;
  onnx_decode_nms_ms?: number;
  tracker_ms?: number;
  queue_wait_ms?: number;
  optical_flow_ms?: number;
  tracking_postprocess_ms?: number;
}

interface BackendImageResponse {
  code: number;
  message: string;
  output_shapes?: number[][];
  timing_ms?: BackendTiming;
  detections: BackendDetection[];
}

interface BackendVideoFrame {
  frame_index: number;
  timestamp_ms: number;
  is_detection_frame?: boolean;
  corrected_from_frame_index?: number;
  correction_latency_frames?: number;
  tracks_source?: string;
  tracks: BackendTrack[];
}

type BackendDiagnostics = Record<string, number | undefined>;

interface BackendVideoResponse {
  code: number;
  message: string;
  tracking_status?: string;
  fps: number;
  source_fps?: number;
  target_detect_fps?: number;
  effective_detect_fps?: number;
  frame_stride?: number;
  stride_mode?: string;
  onnx_async?: boolean;
  base_frame_stride?: number;
  min_frame_stride_used?: number;
  max_frame_stride_used?: number;
  final_frame_stride?: number;
  width: number;
  height: number;
  frame_count: number;
  source_frame_count?: number;
  processed_frame_count: number;
  display_frame_count: number;
  detected_frame_count?: number;
  async_infer_request_count?: number;
  async_correction_count?: number;
  async_corrected_frame_count?: number;
  forced_detection_count?: number;
  scheduled_detection_count?: number;
  skipped_detection_count?: number;
  weak_tracked_frame_count?: number;
  interpolated_frame_count?: number;
  empty_frame_count?: number;
  output_shapes?: number[][];
  timing_ms?: BackendTiming;
  high_low_diagnostics?: BackendDiagnostics;
  frames: BackendVideoFrame[];
}

interface MultipartResult<T> {
  payload: T;
  request: RequestMetadata;
}

export interface MediaDimensions {
  width: number;
  height: number;
}

export interface VisionApiResult {
  frames: FrameResult[];
  tracks: TrackResult[];
  metrics: InferenceMetrics;
  runtime: RuntimeDetails;
  request: RequestMetadata;
  dimensions?: MediaDimensions;
}

export class VisionApiError extends Error {
  readonly httpStatus?: number;
  readonly contentType?: string;
  readonly elapsedMs?: number;
  readonly endpoint: string;

  constructor(
    message: string,
    endpointPath: string,
    metadata: { httpStatus?: number; contentType?: string; elapsedMs?: number } = {},
  ) {
    super(message);
    this.name = "VisionApiError";
    this.endpoint = endpointPath;
    this.httpStatus = metadata.httpStatus;
    this.contentType = metadata.contentType;
    this.elapsedMs = metadata.elapsedMs;
  }
}

const apiBaseUrl = (import.meta.env.VITE_API_BASE_URL ?? "").replace(/\/$/, "");

export const apiBaseLabel = apiBaseUrl || "Vite proxy";

export const DROGON_API = {
  image: { path: "/infer", fieldName: "image" },
  video: { path: "/infer_video", fieldName: "video" },
} as const;

const trackColors = [
  "#607d9b",
  "#8a715b",
  "#607c70",
  "#7c708e",
  "#7b7f8d",
  "#54738d",
  "#7f685f",
];

const endpoint = (path: string) => apiBaseUrl + path;
const clamp = (value: number, min: number, max: number) =>
  Math.min(Math.max(value, min), max);
const round = (value: number, digits = 2) =>
  Number.parseFloat(value.toFixed(digits));
const numberOrUndefined = (value: unknown) =>
  typeof value === "number" && Number.isFinite(value) ? value : undefined;

const classNameOf = (item: BackendDetection) =>
  item.class_name ?? "class " + item.class_id;

const summarizeClasses = (detections: DetectionResult[]) =>
  detections.reduce<Record<string, number>>((summary, detection) => {
    summary[detection.className] = (summary[detection.className] ?? 0) + 1;
    return summary;
  }, {});

const assertDimensions = (dimensions: MediaDimensions) => {
  if (dimensions.width <= 0 || dimensions.height <= 0) {
    throw new Error("Invalid media dimensions");
  }
};

const boxToPercent = (box: BackendBox, dimensions: MediaDimensions) => {
  assertDimensions(dimensions);
  const x1 = clamp(Math.min(box.x1, box.x2), 0, dimensions.width);
  const x2 = clamp(Math.max(box.x1, box.x2), 0, dimensions.width);
  const y1 = clamp(Math.min(box.y1, box.y2), 0, dimensions.height);
  const y2 = clamp(Math.max(box.y1, box.y2), 0, dimensions.height);

  return {
    x: round((x1 / dimensions.width) * 100),
    y: round((y1 / dimensions.height) * 100),
    width: round(((x2 - x1) / dimensions.width) * 100),
    height: round(((y2 - y1) / dimensions.height) * 100),
  };
};

const createDetection = (
  item: BackendDetection,
  dimensions: MediaDimensions,
  frameIndex: number,
  index: number,
  trackId?: number,
): DetectionResult => ({
  id: "det-" + frameIndex + "-" + (trackId ?? item.class_id) + "-" + index,
  frameIndex,
  className: classNameOf(item),
  confidence: round(item.score, 4),
  bbox: boxToPercent(item.box, dimensions),
  trackId,
});

const createFrame = (
  frameIndex: number,
  timestampMs: number,
  detections: DetectionResult[],
  tracks: TrackResult[],
  source: FrameSource,
  isDetectionFrame: boolean,
  correctedFromFrameIndex?: number,
  correctionLatencyFrames?: number,
): FrameResult => ({
  frameIndex,
  timestampMs,
  detections,
  tracks,
  objectCount: detections.length,
  classCounts: summarizeClasses(detections),
  isDetectionFrame,
  tracksSource: source,
  correctedFromFrameIndex,
  correctionLatencyFrames,
});

const responsePreview = (text: string) => {
  const compact = text.trim().replace(/\s+/g, " ");
  return compact.length > 220 ? compact.slice(0, 220) + "..." : compact;
};

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const payloadMessage = (payload: unknown) =>
  isRecord(payload) && typeof payload.message === "string"
    ? payload.message
    : undefined;

const parseJsonPayload = (
  text: string,
  status: number,
  statusText: string,
  contentType: string,
  path: string,
  elapsedMs: number,
) => {
  if (text.trim() === "") {
    return null;
  }

  try {
    return JSON.parse(text) as unknown;
  } catch {
    throw new VisionApiError(
      "Invalid Drogon API response: HTTP " + status + " " + statusText +
        "; content-type=" + contentType + "; body=\"" + responsePreview(text) + "\"",
      path,
      { httpStatus: status, contentType, elapsedMs },
    );
  }
};

const requestMultipart = async <T>(
  path: string,
  fieldName: "image" | "video",
  file: File,
): Promise<MultipartResult<T>> => {
  const formData = new FormData();
  formData.append(fieldName, file);
  const startedAt = performance.now();

  console.info("[vision-api] request start", {
    path,
    fieldName,
    fileName: file.name,
    fileSize: file.size,
    mime: file.type || null,
  });

  let response: Response;
  try {
    response = await fetch(endpoint(path), { method: "POST", body: formData });
  } catch (error) {
    const message = error instanceof Error ? error.message : "Network request failed";
    throw new VisionApiError("Drogon API request failed: " + message, path);
  }

  const contentType = response.headers.get("content-type") ?? "not provided";
  const text = await response.text();
  const elapsedMs = round(performance.now() - startedAt, 1);
  const request: RequestMetadata = {
    endpoint: path,
    method: "POST",
    fieldName,
    httpStatus: response.status,
    contentType,
    elapsedMs,
    responseBytes: new Blob([text]).size,
  };
  const payload = parseJsonPayload(
    text,
    response.status,
    response.statusText,
    contentType,
    path,
    elapsedMs,
  );

  if (!response.ok) {
    const message = payloadMessage(payload) ??
      "HTTP " + response.status + " " + response.statusText +
        "; content-type=" + contentType + "; body=\"" + responsePreview(text) + "\"";
    throw new VisionApiError(message, path, {
      httpStatus: response.status,
      contentType,
      elapsedMs,
    });
  }
  if (!isRecord(payload) || typeof payload.code !== "number") {
    throw new VisionApiError(
      "Invalid Drogon API response: HTTP " + response.status + " " + response.statusText +
        "; content-type=" + contentType + "; body=\"" + responsePreview(text) + "\"",
      path,
      { httpStatus: response.status, contentType, elapsedMs },
    );
  }
  if (payload.code !== 0) {
    throw new VisionApiError(payloadMessage(payload) ?? "Inference failed", path, {
      httpStatus: response.status,
      contentType,
      elapsedMs,
    });
  }

  return { payload: payload as T, request };
};

const assertVideoResponse = (response: BackendVideoResponse) => {
  if (!Number.isFinite(response.width) || !Number.isFinite(response.height)) {
    throw new Error("Invalid video response dimensions");
  }
  assertDimensions({ width: response.width, height: response.height });
  if (!Array.isArray(response.frames)) {
    throw new Error("Invalid video response: frames must be an array");
  }
};

const makeTrack = (
  trackId: number,
  className: string,
  frames: number[],
  confidences: number[],
  finalFrameIndex: number,
): TrackResult => {
  const firstFrame = frames.reduce(
    (minimum, frame) => Math.min(minimum, frame),
    frames[0] ?? 0,
  );
  const lastFrame = frames.reduce(
    (maximum, frame) => Math.max(maximum, frame),
    frames[0] ?? 0,
  );
  const averageConfidence =
    confidences.reduce((sum, value) => sum + value, 0) / Math.max(confidences.length, 1);

  return {
    trackId,
    className,
    firstFrame,
    lastFrame,
    frames,
    averageConfidence: round(averageConfidence, 4),
    status: lastFrame >= finalFrameIndex - 2 ? "active" : "exited",
    color: trackColors[Math.abs(trackId) % trackColors.length],
  };
};

const filterDetections = <T extends BackendDetection>(
  detections: T[],
  confidenceThreshold: number,
) => detections.filter((item) => item.score >= confidenceThreshold);

const frameSource = (value?: string): FrameSource => {
  if (
    value === "async_corrected" ||
    value === "detected" ||
    value === "weak_tracked" ||
    value === "interpolated" ||
    value === "empty"
  ) {
    return value;
  }
  return "not_reported";
};

const diagnosticsMap: Record<string, keyof HighLowDiagnostics> = {
  stable_track_count: "stableTrackCount",
  provisional_track_count: "provisionalTrackCount",
  max_stable_track_count: "maxStableTrackCount",
  max_provisional_track_count: "maxProvisionalTrackCount",
  provisional_created_count: "provisionalCreatedCount",
  provisional_promoted_count: "provisionalPromotedCount",
  provisional_expired_count: "provisionalExpiredCount",
  provisional_deduplicated_count: "provisionalDeduplicatedCount",
  stable_stable_duplicate_count: "stableStableDuplicateCount",
  stable_provisional_duplicate_count: "stableProvisionalDuplicateCount",
  provisional_provisional_duplicate_count: "provisionalProvisionalDuplicateCount",
  output_suppressed_duplicate_count: "outputSuppressedDuplicateCount",
  low_res_geometry_rejection_count: "lowResGeometryRejectionCount",
  low_res_class_conflict_count: "lowResClassConflictCount",
  flow_track_count: "flowTrackCount",
  flow_rejected_track_count: "flowRejectedTrackCount",
  flow_low_point_rejection_count: "flowLowPointRejectionCount",
  flow_invalid_ratio_rejection_count: "flowInvalidRatioRejectionCount",
  flow_forward_backward_rejection_count: "flowForwardBackwardRejectionCount",
  flow_motion_dispersion_rejection_count: "flowMotionDispersionRejectionCount",
  flow_motion_jump_rejection_count: "flowMotionJumpRejectionCount",
  flow_boundary_rejection_count: "flowBoundaryRejectionCount",
  direct_flow_update_count: "directFlowUpdateCount",
  global_flow_update_count: "globalFlowUpdateCount",
  flow_age_output_suppression_count: "flowAgeOutputSuppressionCount",
  flow_age_expired_count: "flowAgeExpiredCount",
  exiting_track_suppression_count: "exitingTrackSuppressionCount",
  urgent_low_res_detection_count: "urgentLowResDetectionCount",
  urgent_high_res_detection_count: "urgentHighResDetectionCount",
  urgent_flow_quality_count: "urgentFlowQualityCount",
  urgent_track_change_count: "urgentTrackChangeCount",
  urgent_duplicate_count: "urgentDuplicateCount",
  urgent_flow_age_count: "urgentFlowAgeCount",
  urgent_geometry_count: "urgentGeometryCount",
  urgent_class_conflict_count: "urgentClassConflictCount",
};

const mapDiagnostics = (source?: BackendDiagnostics): HighLowDiagnostics | undefined => {
  if (!source) {
    return undefined;
  }
  const result: HighLowDiagnostics = {};
  Object.entries(diagnosticsMap).forEach(([backendKey, frontendKey]) => {
    const value = numberOrUndefined(source[backendKey]);
    if (value !== undefined) {
      result[frontendKey] = value;
    }
  });
  return result;
};

const metricsFromTiming = (
  timing: BackendTiming | undefined,
  objectCount: number,
  activeTracks: number,
  fps?: number,
): InferenceMetrics => ({
  fps: numberOrUndefined(fps),
  latencyMs: numberOrUndefined(timing?.end_to_end_ms),
  totalElapsedMs: numberOrUndefined(timing?.total_elapsed_ms),
  decodeMs: numberOrUndefined(timing?.decode_ms),
  preprocessMs: numberOrUndefined(timing?.preprocess_ms),
  inferenceMs: numberOrUndefined(timing?.infer_ms ?? timing?.onnx_inference_ms),
  modelPostprocessMs: numberOrUndefined(
    timing?.postprocess_ms ?? timing?.onnx_decode_nms_ms,
  ),
  trackerMs: numberOrUndefined(timing?.tracker_ms),
  queueWaitMs: numberOrUndefined(timing?.queue_wait_ms),
  opticalFlowMs: numberOrUndefined(timing?.optical_flow_ms),
  trackingPostprocessMs: numberOrUndefined(timing?.tracking_postprocess_ms),
  objectCount,
  activeTracks,
});

export const inferImage = async (
  file: File,
  dimensions: MediaDimensions,
  confidenceThreshold: number,
): Promise<VisionApiResult> => {
  assertDimensions(dimensions);
  const { payload: response, request } = await requestMultipart<BackendImageResponse>(
    DROGON_API.image.path,
    DROGON_API.image.fieldName,
    file,
  );
  const detections = filterDetections(response.detections ?? [], confidenceThreshold)
    .map((item, index) => createDetection(item, dimensions, 0, index, index + 1));
  const tracks = detections.map((detection, index) =>
    makeTrack(
      detection.trackId ?? index + 1,
      detection.className,
      [0],
      [detection.confidence],
      0,
    ),
  );

  return {
    frames: [createFrame(0, 0, detections, tracks, "detected", true)],
    tracks,
    metrics: metricsFromTiming(response.timing_ms, detections.length, tracks.length),
    runtime: { outputShapes: response.output_shapes },
    request,
    dimensions,
  };
};

export const inferVideo = async (
  file: File,
  confidenceThreshold: number,
): Promise<VisionApiResult> => {
  const { payload: response, request } = await requestMultipart<BackendVideoResponse>(
    DROGON_API.video.path,
    DROGON_API.video.fieldName,
    file,
  );
  assertVideoResponse(response);
  const dimensions = { width: response.width, height: response.height };
  const finalFrameIndex = response.frames.reduce(
    (maximum, frame) => Math.max(maximum, frame.frame_index),
    0,
  );
  const trackFrames = new Map<
    number,
    { className: string; frames: number[]; confidences: number[] }
  >();
  const visibleTracksByFrame = response.frames.map((frame) =>
    filterDetections(frame.tracks ?? [], confidenceThreshold),
  );

  visibleTracksByFrame.forEach((frameTracks, frameListIndex) => {
    const frame = response.frames[frameListIndex];
    frameTracks.forEach((track) => {
      const existing = trackFrames.get(track.track_id);
      if (existing) {
        existing.frames.push(frame.frame_index);
        existing.confidences.push(track.score);
      } else {
        trackFrames.set(track.track_id, {
          className: classNameOf(track),
          frames: [frame.frame_index],
          confidences: [track.score],
        });
      }
    });
  });

  const tracks = Array.from(trackFrames, ([trackId, item]) =>
    makeTrack(trackId, item.className, item.frames, item.confidences, finalFrameIndex),
  );
  const trackById = new Map(tracks.map((track) => [track.trackId, track]));
  const frames = response.frames.map((frame, frameListIndex) => {
    const detections = visibleTracksByFrame[frameListIndex].map((track, index) =>
      createDetection(track, dimensions, frame.frame_index, index, track.track_id),
    );
    const frameTracks = detections.flatMap((detection) => {
      const track = detection.trackId === undefined ? undefined : trackById.get(detection.trackId);
      return track ? [track] : [];
    });
    return createFrame(
      frame.frame_index,
      frame.timestamp_ms,
      detections,
      frameTracks,
      frameSource(frame.tracks_source),
      frame.is_detection_frame ?? false,
      frame.corrected_from_frame_index,
      frame.correction_latency_frames,
    );
  });

  const firstFrame = frames[0];
  const runtime: RuntimeDetails = {
    trackingStatus: response.tracking_status,
    sourceFps: numberOrUndefined(response.source_fps),
    targetDetectFps: numberOrUndefined(response.target_detect_fps),
    effectiveDetectFps: numberOrUndefined(response.effective_detect_fps),
    frameStride: numberOrUndefined(response.frame_stride),
    strideMode: response.stride_mode,
    onnxAsync: response.onnx_async,
    baseFrameStride: numberOrUndefined(response.base_frame_stride),
    minFrameStrideUsed: numberOrUndefined(response.min_frame_stride_used),
    maxFrameStrideUsed: numberOrUndefined(response.max_frame_stride_used),
    finalFrameStride: numberOrUndefined(response.final_frame_stride),
    frameCount: numberOrUndefined(response.frame_count),
    sourceFrameCount: numberOrUndefined(response.source_frame_count),
    processedFrameCount: numberOrUndefined(response.processed_frame_count),
    displayFrameCount: numberOrUndefined(response.display_frame_count),
    detectedFrameCount: numberOrUndefined(response.detected_frame_count),
    asyncInferRequestCount: numberOrUndefined(response.async_infer_request_count),
    asyncCorrectionCount: numberOrUndefined(response.async_correction_count),
    asyncCorrectedFrameCount: numberOrUndefined(response.async_corrected_frame_count),
    forcedDetectionCount: numberOrUndefined(response.forced_detection_count),
    scheduledDetectionCount: numberOrUndefined(response.scheduled_detection_count),
    skippedDetectionCount: numberOrUndefined(response.skipped_detection_count),
    weakTrackedFrameCount: numberOrUndefined(response.weak_tracked_frame_count),
    interpolatedFrameCount: numberOrUndefined(response.interpolated_frame_count),
    emptyFrameCount: numberOrUndefined(response.empty_frame_count),
    outputShapes: response.output_shapes,
    highLowDiagnostics: mapDiagnostics(response.high_low_diagnostics),
  };

  return {
    frames,
    tracks,
    metrics: metricsFromTiming(
      response.timing_ms,
      firstFrame?.objectCount ?? 0,
      firstFrame?.tracks.length ?? 0,
      response.fps,
    ),
    runtime,
    request,
    dimensions,
  };
};
