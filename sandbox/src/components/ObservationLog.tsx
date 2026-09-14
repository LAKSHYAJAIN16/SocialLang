import { useEffect, useMemo, useRef, useState } from "react";
import type { LogEntry } from "../engine/types";

interface ObservationLogProps {
  events: LogEntry[];
}

type FilterKind = "all" | "dialogue" | "reflection" | "broadcast";

const FILTERS: Array<{ id: FilterKind; label: string }> = [
  { id: "all", label: "All" },
  { id: "dialogue", label: "Dialogue" },
  { id: "reflection", label: "Reflections" },
  { id: "broadcast", label: "Town" },
];

// A large-population game (city.sl's 5,050 agents, outbreak.sl's 620) can log
// thousands of lines; the log stays legible by only keeping the most recent
// slice in the DOM rather than rendering the whole history every round.
const MAX_VISIBLE = 300;

function matchesFilter(entry: LogEntry, filter: FilterKind): boolean {
  if (filter === "all") return entry.kind !== "ask" && entry.kind !== "print";
  if (filter === "broadcast") return entry.kind === "broadcast" || entry.kind === "whisper" || entry.kind === "plan";
  return entry.kind === filter;
}

export function ObservationLog({ events }: ObservationLogProps) {
  const [filter, setFilter] = useState<FilterKind>("all");
  const listRef = useRef<HTMLDivElement | null>(null);

  const filtered = useMemo(() => {
    const matched = events.filter((e) => matchesFilter(e, filter));
    return matched.slice(-MAX_VISIBLE);
  }, [events, filter]);

  useEffect(() => {
    const el = listRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [filtered.length]);

  return (
    <section className="observation-log" aria-label="Observation log">
      <div className="observation-log__tabs" role="tablist">
        {FILTERS.map((f) => (
          <button
            key={f.id}
            type="button"
            role="tab"
            aria-selected={filter === f.id}
            className={`observation-log__tab${filter === f.id ? " is-active" : ""}`}
            onClick={() => setFilter(f.id)}
          >
            {f.label}
          </button>
        ))}
      </div>
      <div className="observation-log__list" ref={listRef}>
        {filtered.length === 0 && (
          <p className="observation-log__empty">Nothing logged yet -- press Step or Run.</p>
        )}
        {filtered.map((e, i) => (
          <div
            key={`${e.seq ?? "p"}-${i}`}
            className={`observation-log__entry observation-log__entry--${e.kind}`}
          >
            <span className="observation-log__round slc-tabular">{e.round}</span>
            {e.author && <span className="observation-log__author">{e.author}</span>}
            {e.kind === "reflection" && <span className="observation-log__badge">new</span>}
            <span className="observation-log__text">{e.text}</span>
          </div>
        ))}
      </div>
    </section>
  );
}
