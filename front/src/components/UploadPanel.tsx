import {
  FileImage,
  FileVideo,
  Flask,
  SlidersHorizontal,
  UploadSimple,
} from "@phosphor-icons/react";
import type {
  MediaKind,
  MediaMetadata,
  RuntimeDetails,
  VisionTaskStatus,
} from "../types/vision";

interface UploadPanelProps {
  media?: MediaMetadata;
  mediaKind: MediaKind;
  status: VisionTaskStatus;
  isBusy: boolean;
  isDemo: boolean;
  endpoint: string;
  fieldName: string;
  confidenceThreshold: number;
  runtime: RuntimeDetails;
  onUploadClick: () => void;
  onConfidenceChange: (value: number) => void;
  onDemoReplay: () => void;
}

const formatBytes = (bytes: number) => {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(1) + " MB";
};

const formatDuration = (durationMs?: number) => {
  if (durationMs === undefined) return "not reported";
  const seconds = Math.round(durationMs / 1000);
  const minutes = Math.floor(seconds / 60);
  return minutes + ":" + String(seconds % 60).padStart(2, "0");
};

const reported = (value: string | number | undefined) =>
  value === undefined || value === "" ? "not reported" : String(value);

function Field({ label, value }: { label: string; value: string }) {
  return (
    <div className="grid grid-cols-[78px_minmax(0,1fr)] gap-2 py-1.5 text-[10px]">
      <dt className="text-[#8a93a3]">{label}</dt>
      <dd className="min-w-0 break-words font-mono text-[#3b4659]">{value}</dd>
    </div>
  );
}

export function UploadPanel({
  media,
  mediaKind,
  status,
  isBusy,
  isDemo,
  endpoint,
  fieldName,
  confidenceThreshold,
  runtime,
  onUploadClick,
  onConfidenceChange,
  onDemoReplay,
}: UploadPanelProps) {
  const MediaIcon = mediaKind === "video" ? FileVideo : FileImage;

  return (
    <aside
      id="workspace"
      className="vision-scrollbar min-h-0 overflow-y-auto border-r border-[#d9dde5] bg-[#f8f9fb]"
    >
      <section className="border-b border-[#d9dde5] px-4 py-4">
        <div className="flex items-center justify-between">
          <h2 className="text-[12px] font-semibold text-[#202b3c]">Input media</h2>
          {isDemo && (
            <span className="border border-[#b8c6e5] bg-[#edf2fb] px-1.5 py-0.5 font-mono text-[9px] text-[#3559a8]">
              DEMO
            </span>
          )}
        </div>

        <button
          type="button"
          onClick={onUploadClick}
          disabled={isBusy}
          className="mt-3 flex w-full items-center gap-3 rounded-md border border-dashed border-[#bfc6d1] bg-white px-3 py-3 text-left transition-colors hover:border-[#7288b9] hover:bg-[#f8faff] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[#3559a8] active:translate-y-px disabled:cursor-not-allowed disabled:opacity-45"
        >
          <span className="flex h-8 w-8 shrink-0 items-center justify-center rounded-md border border-[#d9dde5] text-[#526078]">
            <UploadSimple size={16} />
          </span>
          <span>
            <span className="block text-[11px] font-semibold text-[#263247]">
              Select image or video
            </span>
            <span className="mt-0.5 block text-[9px] text-[#7d8798]">
              Local file, multipart upload
            </span>
          </span>
        </button>

        {media ? (
          <div className="mt-3 border-l-2 border-[#3559a8] pl-3">
            <div className="flex items-start gap-2">
              <MediaIcon size={15} className="mt-0.5 shrink-0 text-[#56647b]" />
              <p className="min-w-0 break-all text-[11px] font-medium leading-4 text-[#2c3749]">
                {media.name}
              </p>
            </div>
            <dl className="mt-2">
              <Field label="kind" value={mediaKind} />
              <Field label="MIME" value={media.mime || "not provided"} />
              <Field label="size" value={formatBytes(media.sizeBytes)} />
              <Field
                label="dimensions"
                value={
                  media.width && media.height
                    ? media.width + " x " + media.height
                    : "not reported"
                }
              />
              {mediaKind === "video" && (
                <>
                  <Field label="duration" value={formatDuration(media.durationMs)} />
                  <Field
                    label="frames"
                    value={reported(runtime.frameCount ?? media.browserFrameCount)}
                  />
                </>
              )}
            </dl>
          </div>
        ) : (
          <p className="mt-3 text-[10px] leading-4 text-[#7d8798]">
            No file staged. The canvas stays empty until media is selected.
          </p>
        )}

        <button
          type="button"
          onClick={onDemoReplay}
          disabled={isBusy}
          className="mt-3 inline-flex h-8 items-center gap-1.5 rounded-md border border-[#cfd5df] bg-transparent px-2.5 text-[10px] font-medium text-[#536077] transition-colors hover:bg-white focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[#3559a8] active:translate-y-px disabled:opacity-45"
        >
          <Flask size={13} />
          Load Demo replay
        </button>
      </section>

      <section className="border-b border-[#d9dde5] px-4 py-4">
        <h2 className="text-[12px] font-semibold text-[#202b3c]">Endpoint</h2>
        <dl className="mt-2">
          <Field label="method" value="POST" />
          <Field label="path" value={endpoint} />
          <Field label="encoding" value="multipart/form-data" />
          <Field label="field" value={fieldName} />
          <Field label="state" value={status.replace(/_/g, " ")} />
        </dl>
      </section>

      <section id="runtime-parameters" className="border-b border-[#d9dde5] px-4 py-4">
        <div className="flex items-center justify-between">
          <h2 className="text-[12px] font-semibold text-[#202b3c]">Runtime parameters</h2>
          <SlidersHorizontal size={14} className="text-[#7b8597]" />
        </div>
        <label className="mt-3 block" htmlFor="confidence-threshold">
          <span className="flex items-center justify-between text-[10px]">
            <span className="font-medium text-[#4b566a]">UI score filter</span>
            <span className="font-mono text-[#3559a8]">
              {confidenceThreshold.toFixed(2)}
            </span>
          </span>
          <input
            id="confidence-threshold"
            type="range"
            min={0.05}
            max={0.95}
            step={0.01}
            value={confidenceThreshold}
            disabled={isBusy}
            onChange={(event) => onConfidenceChange(Number(event.target.value))}
            className="range-control mt-2"
          />
          <span className="mt-2 block text-[9px] leading-4 text-[#858e9e]">
            Filters mapped response boxes in this UI. It does not change the server model threshold.
          </span>
        </label>
        <dl className="mt-3 border-t border-[#e1e4ea] pt-2">
          <Field label="input" value="1 x 3 x 736 x 1280 (README config)" />
          <Field label="resize" value="letterbox (README default)" />
          <Field label="detect FPS" value={reported(runtime.targetDetectFps)} />
          <Field label="stride" value={reported(runtime.finalFrameStride)} />
          <Field label="backend" value="not reported by response" />
        </dl>
      </section>

      <section className="px-4 py-4">
        <h2 className="text-[12px] font-semibold text-[#202b3c]">Request contract</h2>
        <pre className="mt-3 overflow-x-auto rounded-md border border-[#d9dde5] bg-white p-3 font-mono text-[9px] leading-4 text-[#4c586d]">
{`POST ${endpoint}\nContent-Type: multipart/form-data\nfield: ${fieldName}\nresponse: response_json`}
        </pre>
      </section>
    </aside>
  );
}
