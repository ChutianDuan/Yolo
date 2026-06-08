import { TerminalWindow } from "@phosphor-icons/react";
import type { EventLogEntry } from "../types/vision";

interface EventLogProps {
  events: EventLogEntry[];
}

const levelStyle: Record<EventLogEntry["level"], string> = {
  info: "bg-cyan-400",
  success: "bg-emerald-400",
  warning: "bg-amber-300",
};

export function EventLog({ events }: EventLogProps) {
  return (
    <section className="rounded-lg border border-slate-800 bg-[#0c1117] p-4">
      <div className="flex items-center justify-between gap-3">
        <div className="flex items-center gap-2 text-sm font-semibold text-slate-50">
          <TerminalWindow size={17} className="text-teal-300" />
          Event log
        </div>
        <span className="font-mono text-xs text-slate-500">{events.length} entries</span>
      </div>
      <div className="vision-scrollbar mt-3 max-h-56 space-y-2 overflow-y-auto pr-1">
        {events.map((event) => (
          <article
            key={event.id}
            className="rounded-md border border-slate-800 bg-slate-950/80 p-3"
          >
            <div className="flex items-center gap-2">
              <span className={`h-2 w-2 rounded-full ${levelStyle[event.level]}`} />
              <span className="font-mono text-[11px] text-slate-500">{event.time}</span>
              <span className="min-w-0 truncate text-xs font-semibold text-slate-200">
                {event.message}
              </span>
            </div>
            <p className="mt-1 text-xs leading-5 text-slate-500">{event.detail}</p>
          </article>
        ))}
      </div>
    </section>
  );
}
