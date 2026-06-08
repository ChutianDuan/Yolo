import {
  mockFrameResults,
  mockMetrics,
  mockTracks,
  DEFAULT_FRAME,
} from "../data/mockDetections";
import type {
  FrameResult,
  InferenceMetrics,
  TrackerAlgorithm,
} from "../types/vision";

const delay = (ms: number) =>
  new Promise<void>((resolve) => {
    window.setTimeout(resolve, ms);
  });

const filterFrame = (frame: FrameResult, confidenceThreshold: number) => {
  const detections = frame.detections.filter(
    (detection) => detection.confidence >= confidenceThreshold,
  );
  const visibleTrackIds = new Set(
    detections.flatMap((detection) =>
      detection.trackId === undefined ? [] : [detection.trackId],
    ),
  );
  const tracks = frame.tracks.filter((track) => visibleTrackIds.has(track.trackId));

  return {
    ...frame,
    detections,
    tracks,
    objectCount: detections.length,
    classCounts: detections.reduce<Record<string, number>>((summary, detection) => {
      summary[detection.className] = (summary[detection.className] ?? 0) + 1;
      return summary;
    }, {}),
  };
};

const summarizeFrameMetrics = (frames: FrameResult[], frameIndex = DEFAULT_FRAME) => {
  const frame = frames[frameIndex] ?? frames[0];

  return {
    objectCount: frame?.objectCount ?? 0,
    activeTracks: frame?.tracks.length ?? 0,
  };
};

export const getFrameResults = (confidenceThreshold = 0): FrameResult[] =>
  mockFrameResults.map((frame) => filterFrame(frame, confidenceThreshold));

export const runDetection = async (
  frameIndex: number,
  confidenceThreshold: number,
): Promise<FrameResult> => {
  await delay(260);
  return filterFrame(mockFrameResults[frameIndex] ?? mockFrameResults[0], confidenceThreshold);
};

export const runTracking = async (
  confidenceThreshold: number,
  tracker: TrackerAlgorithm,
): Promise<{
  frames: FrameResult[];
  metrics: InferenceMetrics;
}> => {
  await delay(420);

  const frames = getFrameResults(confidenceThreshold);
  const trackerPenalty = tracker === "DeepSORT" ? 3.8 : tracker === "SORT" ? -1.7 : 0;
  const frameMetrics = summarizeFrameMetrics(frames);

  return {
    frames,
    metrics: {
      ...mockMetrics,
      fps: Number((mockMetrics.fps - trackerPenalty).toFixed(1)),
      latencyMs: Number((1000 / Math.max(mockMetrics.fps - trackerPenalty, 1)).toFixed(1)),
      ...frameMetrics,
    },
  };
};

export const getAllTracks = () => mockTracks;
