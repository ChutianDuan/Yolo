import { Clock, Cpu, Pulse, Stack } from "@phosphor-icons/react";
import type { InferenceMetrics, VisionTaskStatus } from "../types/vision";

interface MetricsBarProps {
  metrics: InferenceMetrics;
  status: VisionTaskStatus;
}

const timingItems = [
  { key: "preprocessMs", label: "Preprocess", tone: "bg-cyan-300" },
  { key: "inferenceMs", label: "Inference", tone: "bg-teal-300" },
  { key: "postprocessMs", label: "Postprocess", tone: "bg-emerald-300" },
] as const;

const statusCopy: Record<VisionTaskStatus, { label: string; detail: string; tone: string }> = {
  idle: {
    label: "Ready",
    detail: "Local mock data is loaded and API-shaped responses are available.",
    tone: "border-slate-700 bg-slate-950 text-slate-300",
  },
  uploading: {
    label: "Media loaded",
    detail: "Input metadata is staged for the next detector pass.",
    tone: "border-cyan-300/40 bg-cyan-300/10 text-cyan-200",
  },
  detecting: {
    label: "Detecting",
    detail: "Detector output is refreshing frame boxes and confidence scores.",
    tone: "border-teal-300/40 bg-teal-300/10 text-teal-200",
  },
  tracking: {
    label: "Tracking",
    detail: "Tracker is associating detections into persistent object IDs.",
    tone: "border-teal-300/40 bg-teal-300/10 text-teal-200",
  },
  completed: {
    label: "Completed",
    detail: "Latest frame, track spans, and metrics are synchronized.",
    tone: "border-emerald-300/40 bg-emerald-300/10 text-emerald-200",
  },
  failed: {
    label: "Failed",
    detail: "The UI is ready to display a backend failure state.",
    tone: "border-rose-300/40 bg-rose-300/10 text-rose-200",
  },
};

export function MetricsBar({ metrics, status }: MetricsBarProps) {
  const totalTiming =
    metrics.preprocessMs + metrics.inferenceMs + metrics.postprocessMs;
  const currentStatus = statusCopy[status];
  const isProcessing = status === "detecting" || status === "tracking";

  return (
    <section className="rounded-lg border border-slate-800 bg-[#0c1117] p-4">
      <div className={"mb-4 rounded-md border p-3 " + currentStatus.tone}>
        <div className="flex items-center justify-between gap-3">
          <div>
            <p className="text-sm font-semibold">{currentStatus.label}</p>
            <p className="mt-1 text-xs opacity-75">{currentStatus.detail}</p>
          </div>
          <span className="font-mono text-xs opacity-80">
            {isProcessing ? "refreshing" : totalTiming.toFixed(1) + "ms"}
          </span>
        </div>
      </div>

      <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
        <div className="rounded-md border border-slate-800 bg-slate-950/80 p-3">
          <div className="flex items-center gap-2 text-xs text-slate-500">
            <Pulse size={15} />
            FPS
          </div>
          <p className="mt-2 font-mono text-xl font-semibold text-teal-300">
            {metrics.fps.toFixed(1)}
          </p>
        </div>
        <div className="rounded-md border border-slate-800 bg-slate-950/80 p-3">
          <div className="flex items-center gap-2 text-xs text-slate-500">
            <Clock size={15} />
            Latency
          </div>
          <p className="mt-2 font-mono text-xl font-semibold text-cyan-300">
            {metrics.latencyMs.toFixed(1)}ms
          </p>
        </div>
        <div className="rounded-md border border-slate-800 bg-slate-950/80 p-3">
          <div className="flex items-center gap-2 text-xs text-slate-500">
            <Stack size={15} />
            Detections
          </div>
          <p className="mt-2 font-mono text-xl font-semibold text-slate-100">
            {metrics.objectCount}
          </p>
        </div>
        <div className="rounded-md border border-slate-800 bg-slate-950/80 p-3">
          <div className="flex items-center gap-2 text-xs text-slate-500">
            <Cpu size={15} />
            Active Tracks
          </div>
          <p className="mt-2 font-mono text-xl font-semibold text-slate-100">
            {metrics.activeTracks}
          </p>
        </div>
      </div>

      <div className="mt-4 rounded-md border border-slate-800 bg-slate-950/80 p-3">
        <div className="mb-3 flex items-center justify-between gap-3 text-xs">
          <span className="font-semibold text-slate-300">Inference performance</span>
          <span className="font-mono text-slate-500">
            {totalTiming.toFixed(1)}ms total pipeline
          </span>
        </div>
        <div className="mb-4 flex h-2 overflow-hidden rounded-full bg-slate-800">
          {timingItems.map((item) => {
            const value = metrics[item.key];
            const width = Math.max(5, (value / totalTiming) * 100) + "%";

            return <div key={item.key} className={item.tone} style={{ width }} />;
          })}
        </div>
        <div className="space-y-3">
          {timingItems.map((item) => {
            const value = metrics[item.key];
            const width = Math.max(5, (value / totalTiming) * 100) + "%";

            return (
              <div key={item.key}>
                <div className="mb-1 flex items-center justify-between text-xs">
                  <span className="text-slate-400">{item.label}</span>
                  <span className="font-mono text-slate-300">{value.toFixed(1)}ms</span>
                </div>
                <div className="h-2 overflow-hidden rounded-full bg-slate-800">
                  <div className={"h-full rounded-full " + item.tone} style={{ width }} />
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </section>
  );
}
