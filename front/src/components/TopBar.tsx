import { useState } from "react";
import {
  FileArrowUp,
  Gauge,
  Play,
  Scan,
} from "@phosphor-icons/react";
import type {
  InferenceMetrics,
  TrackerAlgorithm,
  VisionTaskStatus,
} from "../types/vision";

interface TopBarProps {
  status: VisionTaskStatus;
  mediaName: string;
  isBusy: boolean;
  tracker: TrackerAlgorithm;
  confidenceThreshold: number;
  metrics: InferenceMetrics;
  onUploadClick: () => void;
  onRun: () => void;
  onTrackerChange: (tracker: TrackerAlgorithm) => void;
  onConfidenceChange: (value: number) => void;
}

const statusText: Record<VisionTaskStatus, string> = {
  idle: "ready",
  uploading: "loaded",
  detecting: "detecting",
  tracking: "tracking",
  completed: "complete",
  failed: "failed",
};

const trackerOptions: TrackerAlgorithm[] = ["ByteTrack", "SORT", "DeepSORT"];

const buttonClass =
  "inline-flex h-8 items-center justify-center gap-1.5 rounded-md border px-2.5 text-xs font-medium transition active:translate-y-px disabled:cursor-not-allowed disabled:opacity-50 whitespace-nowrap";

function MetricRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-center justify-between gap-5 py-1.5 text-xs">
      <span className="text-slate-500">{label}</span>
      <span className="font-mono text-slate-100">{value}</span>
    </div>
  );
}

export function TopBar({
  status,
  mediaName,
  isBusy,
  tracker,
  confidenceThreshold,
  metrics,
  onUploadClick,
  onRun,
  onTrackerChange,
  onConfidenceChange,
}: TopBarProps) {
  const [metricsOpen, setMetricsOpen] = useState(false);
  const totalPipeline =
    metrics.preprocessMs + metrics.inferenceMs + metrics.postprocessMs;

  return (
    <header className="z-40 border-b border-slate-800 bg-[#0a0d11]">
      <div className="flex h-12 min-w-0 items-center gap-2 px-3">
        <div className="flex min-w-0 items-center gap-2 pr-2">
          <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-md border border-slate-700 bg-slate-900 text-teal-300">
            <Scan size={16} weight="bold" />
          </div>
          <div className="min-w-0">
            <h1 className="truncate text-sm font-semibold text-slate-50">VisionTrack</h1>
            <p className="hidden max-w-[260px] truncate text-[10px] text-slate-500 md:block">
              {mediaName}
            </p>
          </div>
        </div>

        <div className="h-6 w-px bg-slate-800" />

        <button
          type="button"
          className={buttonClass + " border-slate-700 bg-slate-900 text-slate-100 hover:border-slate-500"}
          onClick={onUploadClick}
          disabled={isBusy}
        >
          <FileArrowUp size={14} weight="bold" />
          Upload
        </button>

        <button
          type="button"
          className={buttonClass + " border-teal-300 bg-teal-300 text-slate-950 hover:bg-teal-200"}
          onClick={onRun}
          disabled={isBusy}
        >
          <Play size={14} weight="bold" />
          {isBusy ? "Running" : "Run"}
        </button>

        <label className="hidden items-center gap-1.5 text-[11px] text-slate-500 sm:flex">
          Tracker
          <select
            value={tracker}
            onChange={(event) => onTrackerChange(event.target.value as TrackerAlgorithm)}
            disabled={isBusy}
            className="h-8 rounded-md border border-slate-700 bg-slate-950 px-2 text-xs text-slate-100 outline-none transition focus:border-teal-300 disabled:opacity-60"
          >
            {trackerOptions.map((option) => (
              <option key={option} value={option}>
                {option}
              </option>
            ))}
          </select>
        </label>

        <label className="hidden min-w-[150px] items-center gap-2 text-[11px] text-slate-500 md:flex">
          Conf
          <input
            type="range"
            min={0.35}
            max={0.95}
            step={0.01}
            value={confidenceThreshold}
            onChange={(event) => onConfidenceChange(Number(event.target.value))}
            disabled={isBusy}
            className="range-control h-1.5"
          />
          <span className="w-9 font-mono text-slate-200">{confidenceThreshold.toFixed(2)}</span>
        </label>

        <div className="relative ml-auto">
          <button
            type="button"
            className={buttonClass + " border-slate-700 bg-slate-950 text-slate-100 hover:border-slate-500"}
            onClick={() => setMetricsOpen((open) => !open)}
            aria-expanded={metricsOpen}
          >
            <Gauge size={14} weight="bold" />
            Metrics
          </button>
          {metricsOpen && (
            <div className="absolute right-0 top-10 z-50 w-64 rounded-md border border-slate-700 bg-[#0b1016] p-3 shadow-panel">
              <div className="mb-2 flex items-center justify-between border-b border-slate-800 pb-2">
                <span className="text-xs font-semibold text-slate-100">Inference metrics</span>
                <span className="font-mono text-[10px] text-slate-500">
                  {totalPipeline.toFixed(1)}ms
                </span>
              </div>
              <MetricRow label="FPS" value={metrics.fps.toFixed(1)} />
              <MetricRow label="Latency" value={metrics.latencyMs.toFixed(1) + "ms"} />
              <MetricRow label="Preprocess" value={metrics.preprocessMs.toFixed(1) + "ms"} />
              <MetricRow label="Inference" value={metrics.inferenceMs.toFixed(1) + "ms"} />
              <MetricRow label="Postprocess" value={metrics.postprocessMs.toFixed(1) + "ms"} />
              <MetricRow label="Objects" value={String(metrics.objectCount)} />
              <MetricRow label="Tracks" value={String(metrics.activeTracks)} />
            </div>
          )}
        </div>

        <div className="flex items-center gap-1.5 rounded-md border border-slate-700 bg-slate-950 px-2 py-1 text-[11px] font-medium text-slate-300">
          <span
            className={
              "h-1.5 w-1.5 rounded-full " +
              (status === "failed"
                ? "bg-rose-300"
                : isBusy
                  ? "bg-amber-300"
                  : "bg-emerald-300")
            }
          />
          {statusText[status]}
        </div>
      </div>
    </header>
  );
}
