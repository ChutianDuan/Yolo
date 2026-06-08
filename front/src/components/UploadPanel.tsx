import {
  Cpu,
  FileVideo,
  Gauge,
  Path,
  SlidersHorizontal,
  UploadSimple,
} from "@phosphor-icons/react";
import type { RefObject } from "react";
import type {
  MediaKind,
  ModelOption,
  TrackerAlgorithm,
  VisionTaskStatus,
} from "../types/vision";

interface UploadPanelProps {
  fileInputRef: RefObject<HTMLInputElement>;
  mediaName: string;
  mediaKind: MediaKind;
  modelId: string;
  modelOptions: ModelOption[];
  confidenceThreshold: number;
  iouThreshold: number;
  tracker: TrackerAlgorithm;
  status: VisionTaskStatus;
  isBusy: boolean;
  onFileSelected: (file: File) => void;
  onModelChange: (modelId: string) => void;
  onConfidenceChange: (value: number) => void;
  onIouChange: (value: number) => void;
  onTrackerChange: (tracker: TrackerAlgorithm) => void;
  onStartInference: () => void;
}

const trackerOptions: TrackerAlgorithm[] = ["ByteTrack", "SORT", "DeepSORT"];

const panelLabel = "text-xs font-semibold uppercase tracking-[0.14em] text-slate-500";

export function UploadPanel({
  fileInputRef,
  mediaName,
  mediaKind,
  modelId,
  modelOptions,
  confidenceThreshold,
  iouThreshold,
  tracker,
  status,
  isBusy,
  onFileSelected,
  onModelChange,
  onConfidenceChange,
  onIouChange,
  onTrackerChange,
  onStartInference,
}: UploadPanelProps) {
  const selectedModel = modelOptions.find((model) => model.id === modelId) ?? modelOptions[0];

  return (
    <aside className="flex min-h-0 flex-col gap-4 border-b border-slate-800 bg-[#0c1117] p-4 lg:border-b-0 lg:border-r">
      <div>
        <p className={panelLabel}>Input</p>
        <label
          className="mt-3 flex cursor-pointer flex-col gap-4 rounded-lg border border-dashed border-slate-700 bg-[#101720] p-4 transition hover:border-teal-400/70 hover:bg-[#121d26]"
          htmlFor="media-upload"
        >
          <input
            ref={fileInputRef}
            id="media-upload"
            type="file"
            className="sr-only"
            accept="image/*,video/*"
            onChange={(event) => {
              const file = event.target.files?.[0];
              if (file) {
                onFileSelected(file);
              }
            }}
          />
          <div className="flex items-start gap-3">
            <div className="flex h-11 w-11 shrink-0 items-center justify-center rounded-md bg-teal-400 text-slate-950">
              <UploadSimple size={22} weight="bold" />
            </div>
            <div className="min-w-0">
              <p className="text-sm font-semibold text-slate-50">Upload image or video</p>
              <p className="mt-1 text-xs leading-5 text-slate-400">
                Use a local file or keep the built-in traffic sequence for the mock run.
              </p>
            </div>
          </div>
          <div className="rounded-md border border-slate-800 bg-slate-950/80 p-3">
            <div className="flex items-center gap-2 text-xs font-medium text-slate-300">
              <FileVideo size={16} className="text-teal-300" />
              <span className="truncate">{mediaName}</span>
            </div>
            <div className="mt-2 flex items-center justify-between text-[11px] text-slate-500">
              <span>{mediaKind === "video" ? "Video sequence" : "Single image"}</span>
              <span>{status.replace(/-/g, " ")}</span>
            </div>
          </div>
        </label>
      </div>

      <div className="rounded-lg border border-slate-800 bg-[#101720] p-4">
        <div className="flex items-center justify-between gap-3">
          <p className={panelLabel}>Model</p>
          <Cpu size={16} className="text-slate-500" />
        </div>
        <label className="mt-3 block text-xs font-medium text-slate-400" htmlFor="model-select">
          Detection model
        </label>
        <select
          id="model-select"
          value={modelId}
          onChange={(event) => onModelChange(event.target.value)}
          className="mt-2 h-10 w-full rounded-md border border-slate-700 bg-slate-950 px-3 text-sm text-slate-100 outline-none transition focus:border-teal-400"
        >
          {modelOptions.map((model) => (
            <option key={model.id} value={model.id}>
              {model.label}
            </option>
          ))}
        </select>
        <div className="mt-3 rounded-md border border-slate-800 bg-slate-950/80 p-3 text-xs text-slate-400">
          Runtime profile: <span className="text-slate-200">{selectedModel.runtime}</span>
        </div>
      </div>

      <div className="rounded-lg border border-slate-800 bg-[#101720] p-4">
        <div className="flex items-center justify-between gap-3">
          <p className={panelLabel}>Thresholds</p>
          <SlidersHorizontal size={16} className="text-slate-500" />
        </div>

        <div className="mt-4">
          <div className="mb-2 flex items-center justify-between text-xs">
            <label className="font-medium text-slate-300" htmlFor="confidence-threshold">
              Confidence threshold
            </label>
            <span className="font-mono text-teal-300">
              {(confidenceThreshold * 100).toFixed(0)}%
            </span>
          </div>
          <input
            id="confidence-threshold"
            type="range"
            min={0.35}
            max={0.95}
            step={0.01}
            value={confidenceThreshold}
            onChange={(event) => onConfidenceChange(Number(event.target.value))}
            className="range-control"
          />
        </div>

        <div className="mt-5">
          <div className="mb-2 flex items-center justify-between text-xs">
            <label className="font-medium text-slate-300" htmlFor="iou-threshold">
              IoU threshold
            </label>
            <span className="font-mono text-cyan-300">{(iouThreshold * 100).toFixed(0)}%</span>
          </div>
          <input
            id="iou-threshold"
            type="range"
            min={0.25}
            max={0.75}
            step={0.01}
            value={iouThreshold}
            onChange={(event) => onIouChange(Number(event.target.value))}
            className="range-control"
          />
        </div>
      </div>

      <div className="rounded-lg border border-slate-800 bg-[#101720] p-4">
        <div className="flex items-center justify-between gap-3">
          <p className={panelLabel}>Tracker</p>
          <Path size={16} className="text-slate-500" />
        </div>
        <div className="mt-3 grid grid-cols-1 gap-2">
          {trackerOptions.map((option) => {
            const isSelected = option === tracker;

            return (
              <button
                key={option}
                type="button"
                onClick={() => onTrackerChange(option)}
                className={`flex h-10 items-center justify-between rounded-md border px-3 text-sm transition active:translate-y-px ${
                  isSelected
                    ? "border-teal-400 bg-teal-400 text-slate-950"
                    : "border-slate-700 bg-slate-950 text-slate-300 hover:border-slate-500"
                }`}
              >
                <span className="font-medium">{option}</span>
                <span className="font-mono text-[11px] opacity-75">
                  {option === "ByteTrack" ? "ID stable" : option === "SORT" ? "fast" : "re-ID"}
                </span>
              </button>
            );
          })}
        </div>
      </div>

      <button
        type="button"
        onClick={onStartInference}
        disabled={isBusy}
        className="mt-auto inline-flex h-11 items-center justify-center gap-2 rounded-md bg-slate-50 px-4 text-sm font-semibold text-slate-950 transition hover:bg-teal-200 active:translate-y-px disabled:cursor-not-allowed disabled:opacity-60"
      >
        <Gauge size={18} weight="bold" />
        {isBusy ? "Inference running" : "Start inference"}
      </button>
    </aside>
  );
}
