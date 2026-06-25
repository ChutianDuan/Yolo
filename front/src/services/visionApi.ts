import type {
  DetectionResult,
  FrameResult,
  InferenceMetrics,
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

interface BackendImageResponse {
  code: number;
  message: string;
  detections: BackendDetection[];
}

interface BackendVideoFrame {
  frame_index: number;
  timestamp_ms: number;
  tracks: BackendTrack[];
}

interface BackendVideoResponse {
  code: number;
  message: string;
  fps: number;
  width: number;
  height: number;
  frame_count: number;
  display_frame_count: number;
  processed_frame_count: number;
  timing_ms?: {
    total_elapsed_ms?: number;
    onnx_inference_ms?: number;
    optical_flow_ms?: number;
    postprocess_ms?: number;
  };
  frames: BackendVideoFrame[];
}

export interface MediaDimensions {
  width: number;
  height: number;
}

export interface VisionApiResult {
  frames: FrameResult[];
  tracks: TrackResult[];
  metrics: InferenceMetrics;
  dimensions?: MediaDimensions;
}

const apiBaseUrl = (import.meta.env.VITE_API_BASE_URL ?? "").replace(/\/$/, "");

export const apiBaseLabel = apiBaseUrl || "Vite proxy";

export const DROGON_API = {
  image: {
    path: "/infer",
    fieldName: "image",
  },
  video: {
    path: "/infer_video",
    fieldName: "video",
  },
} as const;

const trackColors = [
  "#38bdf8",
  "#f59e0b",
  "#22c55e",
  "#fb7185",
  "#a3e635",
  "#60a5fa",
  "#2dd4bf",
  "#f97316",
  "#c084fc",
  "#facc15",
];

const endpoint = (path: string) => apiBaseUrl + path;

const clamp = (value: number, min: number, max: number) =>
  Math.min(Math.max(value, min), max);

const round = (value: number, digits = 2) =>
  Number.parseFloat(value.toFixed(digits));

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
): FrameResult => ({
  frameIndex,
  timestampMs,
  detections,
  tracks,
  objectCount: detections.length,
  classCounts: summarizeClasses(detections),
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
) => {
  if (text.trim() === "") {
    return null;
  }

  try {
    return JSON.parse(text) as unknown;
  } catch (error) {
    throw new Error(
      "Invalid Drogon API response: HTTP " +
        status +
        " " +
        statusText +
        "; content-type=" +
        contentType +
        "; body=\"" +
        responsePreview(text) +
        "\"",
    );
  }
};

const requestMultipart = async <T>(
  path: string,
  fieldName: string,
  file: File,
): Promise<T> => {
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
    response = await fetch(endpoint(path), {
      method: "POST",
      body: formData,
    });
  } catch (error) {
    console.error("[vision-api] request failed", { path, error });
    throw new Error(
      error instanceof Error
        ? "Drogon API request failed: " + error.message
        : "Drogon API request failed",
    );
  }

  const contentType = response.headers.get("content-type") ?? "not provided";
  const text = await response.text();
  const elapsedMs = round(performance.now() - startedAt, 1);
  console.info("[vision-api] response received", {
    path,
    status: response.status,
    ok: response.ok,
    contentType,
    bytes: text.length,
    elapsedMs,
  });

  const payload = parseJsonPayload(
    text,
    response.status,
    response.statusText,
    contentType,
  );

  if (!response.ok) {
    const message =
      payloadMessage(payload) ??
      "HTTP " +
        response.status +
        " " +
        response.statusText +
        "; content-type=" +
        contentType +
        "; body=\"" +
        responsePreview(text) +
        "\"";
    console.warn("[vision-api] response error", { path, message });
    throw new Error(message);
  }
  if (!isRecord(payload) || typeof payload.code !== "number") {
    throw new Error(
      "Invalid Drogon API response: HTTP " +
        response.status +
        " " +
        response.statusText +
        "; content-type=" +
        contentType +
        "; body=\"" +
        responsePreview(text) +
        "\"",
    );
  }
  if (payload.code !== 0) {
    throw new Error(payloadMessage(payload) ?? "Inference failed");
  }

  return payload as T;
};

const assertVideoResponse = (response: BackendVideoResponse) => {
  if (!Number.isFinite(response.width) || !Number.isFinite(response.height)) {
    throw new Error("Invalid video response dimensions");
  }
  assertDimensions({
    width: response.width,
    height: response.height,
  });
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
  const firstFrame = Math.min(...frames);
  const lastFrame = Math.max(...frames);
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
    speedKmh: 0,
    region: "tracked frame",
  };
};

const filterDetections = <T extends BackendDetection>(
  detections: T[],
  confidenceThreshold: number,
) => detections.filter((item) => item.score >= confidenceThreshold);

export const inferImage = async (
  file: File,
  dimensions: MediaDimensions,
  confidenceThreshold: number,
): Promise<VisionApiResult> => {
  assertDimensions(dimensions);

  const startedAt = performance.now();
  const response = await requestMultipart<BackendImageResponse>(
    DROGON_API.image.path,
    DROGON_API.image.fieldName,
    file,
  );
  const elapsedMs = performance.now() - startedAt;

  const detections = filterDetections(
    response.detections ?? [],
    confidenceThreshold,
  ).map((item, index) => createDetection(item, dimensions, 0, index, index + 1));
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
    frames: [createFrame(0, 0, detections, tracks)],
    tracks,
    metrics: {
      fps: elapsedMs > 0 ? round(1000 / elapsedMs, 1) : 0,
      latencyMs: round(elapsedMs, 1),
      preprocessMs: 0,
      inferenceMs: round(elapsedMs, 1),
      postprocessMs: 0,
      objectCount: detections.length,
      activeTracks: tracks.length,
    },
    dimensions,
  };
};

export const inferVideo = async (
  file: File,
  confidenceThreshold: number,
): Promise<VisionApiResult> => {
  const response = await requestMultipart<BackendVideoResponse>(
    DROGON_API.video.path,
    DROGON_API.video.fieldName,
    file,
  );
  assertVideoResponse(response);
  const dimensions = {
    width: response.width,
    height: response.height,
  };

  const responseFrames = response.frames;
  const finalFrameIndex = responseFrames.length > 0
    ? Math.max(...responseFrames.map((frame) => frame.frame_index))
    : 0;
  const trackFrames = new Map<
    number,
    { className: string; frames: number[]; confidences: number[] }
  >();

  const visibleTracksByFrame = responseFrames.map((frame) =>
    filterDetections(frame.tracks ?? [], confidenceThreshold),
  );

  visibleTracksByFrame.forEach((tracks, frameListIndex) => {
    const frame = responseFrames[frameListIndex];
    tracks.forEach((track) => {
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

  const frames = responseFrames.map((frame, frameListIndex) => {
    const detections = visibleTracksByFrame[frameListIndex].map((track, index) =>
      createDetection(track, dimensions, frame.frame_index, index, track.track_id),
    );
    const frameTracks = detections.flatMap((detection) => {
      if (detection.trackId === undefined) {
        return [];
      }
      const track = trackById.get(detection.trackId);
      return track ? [track] : [];
    });

    return createFrame(
      frame.frame_index,
      frame.timestamp_ms,
      detections,
      frameTracks,
    );
  });

  const timing = response.timing_ms ?? {};
  const totalElapsedMs = timing.total_elapsed_ms ?? 0;
  const displayedFrames =
    response.display_frame_count || response.frame_count || frames.length || 1;

  return {
    frames,
    tracks,
    metrics: {
      fps: round(response.fps ?? 0, 1),
      latencyMs: round(totalElapsedMs / Math.max(displayedFrames, 1), 1),
      preprocessMs: round(timing.optical_flow_ms ?? 0, 1),
      inferenceMs: round(timing.onnx_inference_ms ?? 0, 1),
      postprocessMs: round(timing.postprocess_ms ?? 0, 1),
      objectCount: frames[0]?.objectCount ?? 0,
      activeTracks: frames[0]?.tracks.length ?? 0,
    },
    dimensions,
  };
};
