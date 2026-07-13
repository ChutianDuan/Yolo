import type { EventLogEntry } from "../types/vision";

interface EventLogProps {
  events: EventLogEntry[];
}

export function EventLog({ events }: EventLogProps) {
  return (
    <section id="runs" className="border-t border-[#d9dde5] px-3 py-3">
      <div className="flex items-center justify-between">
        <h3 className="text-[11px] font-semibold text-[#263247]">Run events</h3>
        <span className="font-mono text-[8px] text-[#8a93a3]">{events.length} entries</span>
      </div>

      {events.length === 0 ? (
        <p className="mt-3 border-l border-[#cfd5df] pl-3 text-[9px] leading-4 text-[#8992a2]">
          Request events will appear here after media selection.
        </p>
      ) : (
        <div className="relative mt-3 space-y-0 before:absolute before:bottom-2 before:left-[3px] before:top-2 before:w-px before:bg-[#d5dae3]">
          {events.map((event) => (
            <article key={event.id} className="relative grid grid-cols-[15px_minmax(0,1fr)] pb-3">
              <span
                className={
                  "relative z-10 mt-1 h-[7px] w-[7px] border bg-[#fbfcfd] " +
                  (event.level === "error"
                    ? "border-[#b43c45]"
                    : event.level === "warning"
                      ? "border-[#b98132]"
                      : event.level === "success"
                        ? "border-[#526f6a]"
                        : "border-[#71809a]")
                }
              />
              <div className="min-w-0">
                <div className="flex items-baseline justify-between gap-2">
                  <p className="truncate font-mono text-[8px] font-semibold text-[#3b4659]">
                    {event.kind}
                  </p>
                  <time className="shrink-0 font-mono text-[8px] text-[#969eaa]">{event.time}</time>
                </div>
                <p className="mt-0.5 text-[9px] leading-4 text-[#687386]">{event.message}</p>
                <dl className="mt-1 grid grid-cols-2 gap-x-2 font-mono text-[8px] text-[#8a93a3]">
                  {event.endpoint && <div className="truncate">{event.method ?? "POST"} {event.endpoint}</div>}
                  {event.fieldName && <div className="truncate">field {event.fieldName}</div>}
                  {event.httpStatus !== undefined && <div>HTTP {event.httpStatus}</div>}
                  {event.elapsedMs !== undefined && <div>{event.elapsedMs.toFixed(1)} ms</div>}
                  {event.frameCount !== undefined && <div>{event.frameCount} frames</div>}
                  {event.trackCount !== undefined && <div>{event.trackCount} tracks</div>}
                  {event.contentType && <div className="col-span-2 truncate">{event.contentType}</div>}
                </dl>
                {event.level === "error" && (
                  <p className="mt-1 break-words border-l border-[#d9aeb2] pl-2 font-mono text-[8px] leading-4 text-[#8b454c]">
                    {event.detail}
                  </p>
                )}
              </div>
            </article>
          ))}
        </div>
      )}
    </section>
  );
}
