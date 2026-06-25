import { useState } from "react";
import {
  FileArrowUp,
  Gauge,
  Play,
  Scan,
} from "@phosphor-icons/react";
import type {
  InferenceMetrics,
  VisionTaskStatus,
} from "../types/vision";
import { DROGON_API, apiBaseLabel } from "../services/visionApi";

interface TopBarProps {
  status: VisionTaskStatus;
  mediaName: string;
  isBusy: boolean;
  confidenceThreshold: number;
  metrics: InferenceMetrics;
  onUploadClick: () => void;
  onRun: () => void;
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

const buttonClass =
  "inline-flex h-9 items-center justify-center gap-1.5 rounded-md border px-3 text-xs font-semibold transition duration-200 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-teal-600 focus-visible:ring-offset-2 active:translate-y-px disabled:cursor-not-allowed disabled:opacity-50 whitespace-nowrap";

function MetricRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-center justify-between gap-5 py-1.5 text-xs">
      <span className="text-slate-500">{label}</span>
      <span className="font-mono font-medium text-slate-900">{value}</span>
    </div>
  );
}

export function TopBar({
  status,
  mediaName,
  isBusy,
  confidenceThreshold,
  metrics,
  onUploadClick,
  onRun,
  onConfidenceChange,
}: TopBarProps) {
  const [metricsOpen, setMetricsOpen] = useState(false);
  const totalPipeline =
    metrics.preprocessMs + metrics.inferenceMs + metrics.postprocessMs;

  return (
    <header className="z-40 rounded-lg border border-slate-200 bg-white shadow-sm">
      <div className="flex min-w-0 flex-wrap items-center gap-2 px-3 py-2">
        <div className="flex min-w-0 items-center gap-2 pr-1 sm:pr-2">
          <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-md bg-slate-950 text-white shadow-sm">
            <Scan size={16} weight="bold" />
          </div>
          <div className="min-w-0">
            <h1 className="truncate text-sm font-semibold text-slate-950">VisionTrack</h1>
            <p className="hidden max-w-[300px] truncate text-[11px] text-slate-500 md:block">
              Drogon API via {apiBaseLabel} · {mediaName}
            </p>
          </div>
        </div>

        <div className="hidden h-6 w-px bg-slate-200 sm:block" />

        <button
          type="button"
          className={buttonClass + " border-slate-200 bg-white text-slate-800 hover:border-slate-300 hover:bg-slate-50"}
          onClick={onUploadClick}
          disabled={isBusy}
        >
          <FileArrowUp size={14} weight="bold" />
          Upload
        </button>

        <button
          type="button"
          className={buttonClass + " border-teal-700 bg-teal-700 text-white hover:border-teal-800 hover:bg-teal-800"}
          onClick={onRun}
          disabled={isBusy}
        >
          <Play size={14} weight="bold" />
          {isBusy ? "Running" : "Run"}
        </button>

        <label className="hidden min-w-[158px] items-center gap-2 text-[11px] font-medium text-slate-500 md:flex">
          UI filter
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
          <span className="w-9 font-mono text-slate-700">{confidenceThreshold.toFixed(2)}</span>
        </label>

        <div className="hidden items-center gap-1 rounded-md border border-slate-200 bg-slate-50 px-2 py-1.5 font-mono text-[10px] text-slate-500 lg:flex">
          <span className="font-semibold text-slate-700">Drogon</span>
          <span>{DROGON_API.image.path}:{DROGON_API.image.fieldName}</span>
          <span className="text-slate-300">·</span>
          <span>{DROGON_API.video.path}:{DROGON_API.video.fieldName}</span>
        </div>

        <div className="relative ml-auto">
          <button
            type="button"
            className={buttonClass + " border-slate-200 bg-slate-50 text-slate-800 hover:border-slate-300 hover:bg-white"}
            onClick={() => setMetricsOpen((open) => !open)}
            aria-expanded={metricsOpen}
          >
            <Gauge size={14} weight="bold" />
            Metrics
          </button>
          {metricsOpen && (
            <div className="absolute right-0 top-11 z-50 w-64 rounded-md border border-slate-200 bg-white p-3 shadow-panel">
              <div className="mb-2 flex items-center justify-between border-b border-slate-100 pb-2">
                <span className="text-xs font-semibold text-slate-950">Inference metrics</span>
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

        <div className="flex h-9 items-center gap-1.5 rounded-md border border-slate-200 bg-white px-2 text-[11px] font-semibold text-slate-700">
          <span
            className={
              "h-1.5 w-1.5 rounded-full " +
              (status === "failed"
                ? "bg-rose-500"
                : isBusy
                  ? "bg-amber-500"
                  : "bg-emerald-500")
            }
          />
          {statusText[status]}
        </div>
      </div>
    </header>
  );
}
