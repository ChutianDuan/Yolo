import type { InferenceMetrics, RuntimeDetails } from "../types/vision";

interface MetricsBarProps {
  metrics: InferenceMetrics;
  runtime: RuntimeDetails;
}

const metric = (value: number | undefined, suffix = "") =>
  value === undefined ? "not reported" : value.toFixed(value < 10 ? 2 : 1) + suffix;

export function MetricsBar({ metrics, runtime }: MetricsBarProps) {
  const items = [
    ["source FPS", metric(runtime.sourceFps ?? metrics.fps)],
    ["backend total elapsed", metric(metrics.totalElapsedMs, " ms")],
    ["model inference", metric(metrics.inferenceMs, " ms")],
    ["optical flow", metric(metrics.opticalFlowMs, " ms")],
    ["tracker", metric(metrics.trackerMs, " ms")],
    ["processed frames", runtime.processedFrameCount?.toString() ?? "not reported"],
  ];

  return (
    <section className="grid shrink-0 grid-cols-3 border-b border-[#d9dde5] bg-[#f7f8fa] xl:grid-cols-6">
      {items.map(([label, value]) => (
        <div key={label} className="border-r border-[#e1e4ea] px-3 py-2 last:border-r-0">
          <p className="truncate text-[8px] text-[#8a93a3]">{label}</p>
          <p className="mt-0.5 truncate font-mono text-[9px] font-medium text-[#3b4659]">
            {value}
          </p>
        </div>
      ))}
    </section>
  );
}
