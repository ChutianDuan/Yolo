import { useState } from "react";
import {
  FileArrowUp,
  GearSix,
  Play,
  Scan,
} from "@phosphor-icons/react";
import type { DrogonRequestState, VisionTaskStatus } from "../types/vision";
import { apiBaseLabel } from "../services/visionApi";

interface TopBarProps {
  status: VisionTaskStatus;
  requestState: DrogonRequestState;
  responseStatus?: number;
  endpoint: string;
  isBusy: boolean;
  canRun: boolean;
  onUploadClick: () => void;
  onRun: () => void;
  onNavigate: (target: string) => void;
}

const navItems = [
  { label: "Workspace", target: "workspace" },
  { label: "Runs", target: "runs" },
  { label: "Compare", target: "timeline" },
  { label: "Diagnostics", target: "diagnostics" },
];

const requestLabel = (
  requestState: DrogonRequestState,
  responseStatus?: number,
) => {
  if (requestState === "requesting") return "Requesting";
  if (requestState === "responded") return "Responded" + (responseStatus ? " " + responseStatus : "");
  if (requestState === "failed") return "Failed" + (responseStatus ? " " + responseStatus : "");
  return "Not checked";
};

const actionClass =
  "inline-flex h-8 items-center justify-center gap-1.5 whitespace-nowrap rounded-md border px-2.5 text-[11px] font-semibold transition-colors duration-200 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[#3559a8] focus-visible:ring-offset-2 active:translate-y-px disabled:cursor-not-allowed disabled:opacity-45";

export function TopBar({
  status,
  requestState,
  responseStatus,
  endpoint,
  isBusy,
  canRun,
  onUploadClick,
  onRun,
  onNavigate,
}: TopBarProps) {
  const [activeTarget, setActiveTarget] = useState("workspace");

  const navigate = (target: string) => {
    setActiveTarget(target);
    onNavigate(target);
  };

  return (
    <header className="z-30 flex h-16 shrink-0 items-center border-b border-[#d9dde5] bg-[#fbfcfd] px-4">
      <div className="flex min-w-[235px] items-center gap-2.5">
        <span className="flex h-8 w-8 items-center justify-center rounded-md border border-[#cfd5df] bg-white text-[#1d2b44]">
          <Scan size={17} weight="regular" />
        </span>
        <div className="leading-tight">
          <h1 className="text-[13px] font-semibold tracking-[-0.01em] text-[#182235]">
            VisionTrack Console
          </h1>
          <p className="mt-0.5 text-[10px] text-[#778195]">Drogon vision runtime</p>
        </div>
      </div>

      <nav className="hidden h-full items-center xl:flex" aria-label="Workspace navigation">
        {navItems.map((item) => (
          <button
            key={item.target}
            type="button"
            onClick={() => navigate(item.target)}
            className={
              "relative h-full px-3 text-[11px] font-medium transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-[#3559a8] " +
              (activeTarget === item.target
                ? "text-[#1d2b44] after:absolute after:inset-x-3 after:bottom-0 after:h-0.5 after:bg-[#3559a8]"
                : "text-[#6e788b] hover:text-[#263247]")
            }
          >
            {item.label}
          </button>
        ))}
      </nav>

      <div className="ml-auto flex min-w-0 items-center gap-3">
        <dl className="hidden items-center gap-3 xl:flex">
          <div>
            <dt className="text-[9px] text-[#8a93a3]">API Base</dt>
            <dd className="mt-0.5 max-w-[120px] truncate font-mono text-[10px] text-[#3e495d]">
              {apiBaseLabel}
            </dd>
          </div>
          <div>
            <dt className="text-[9px] text-[#8a93a3]">Drogon</dt>
            <dd
              className={
                "mt-0.5 font-mono text-[10px] " +
                (requestState === "failed"
                  ? "text-[#b43c45]"
                  : requestState === "requesting"
                    ? "text-[#9a641d]"
                    : "text-[#3e495d]")
              }
            >
              {status === "demo" ? "Not checked (Demo)" : requestLabel(requestState, responseStatus)}
            </dd>
          </div>
          <div>
            <dt className="text-[9px] text-[#8a93a3]">Endpoint</dt>
            <dd className="mt-0.5 font-mono text-[10px] text-[#3e495d]">{endpoint}</dd>
          </div>
        </dl>

        <button
          type="button"
          className={actionClass + " border-[#cfd5df] bg-white text-[#344054] hover:bg-[#f3f5f8]"}
          onClick={() => navigate("runtime-parameters")}
        >
          <GearSix size={14} />
          <span className="hidden 2xl:inline">Settings</span>
        </button>
        <button
          type="button"
          className={actionClass + " border-[#cfd5df] bg-white text-[#344054] hover:bg-[#f3f5f8]"}
          onClick={onUploadClick}
          disabled={isBusy}
        >
          <FileArrowUp size={14} />
          Upload
        </button>
        <button
          type="button"
          className={actionClass + " border-[#3559a8] bg-[#3559a8] text-white hover:bg-[#2f4f95]"}
          onClick={onRun}
          disabled={isBusy || !canRun || status === "demo"}
        >
          <Play size={14} weight="fill" />
          {isBusy ? "Running" : "Run"}
        </button>
      </div>
    </header>
  );
}
