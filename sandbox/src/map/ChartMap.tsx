import { useEffect, useMemo, useRef, useState } from "react";
import type { Frame } from "../engine/types";
import {
  computeActivity,
  hasHiddenRoles,
  layoutAgents,
  recentConversationLines,
  type PositionedAgent,
} from "./mapMath";

interface ChartMapProps {
  frame: Frame | null;
  selectedSeat: string | null;
  onSelect: (seat: string | null) => void;
  theme: "light" | "dark";
}

interface Tokens {
  bg: string;
  grid: string;
  star: string;
  starDim: string;
  constellation: string;
  accent: string;
  accentStrong: string;
  danger: string;
  text: string;
}

function readTokens(): Tokens {
  const style = getComputedStyle(document.documentElement);
  const get = (name: string) => style.getPropertyValue(name).trim();
  return {
    bg: get("--slc-bg") || "#05070d",
    grid: get("--slc-grid") || "rgba(255,255,255,0.06)",
    star: get("--slc-star") || "#f4f1e6",
    starDim: get("--slc-star-dim") || "rgba(244,241,230,0.32)",
    constellation: get("--slc-constellation") || "rgba(244,241,230,0.16)",
    accent: get("--slc-accent") || "#e8a33d",
    accentStrong: get("--slc-accent-strong") || "#ffc773",
    danger: get("--slc-danger") || "#e08095",
    text: get("--slc-text") || "#e9e7de",
  };
}

const STAR_BASE_RADIUS = 5;
const STAR_MAX_BONUS = 6;
// Above this many world locations, per-instance labels stop being legible
// (city.sl alone declares 425) -- see the location-drawing loop below.
const LOCATION_LABEL_LIMIT = 40;

export function ChartMap({ frame, selectedSeat, onSelect, theme }: ChartMapProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const containerRef = useRef<HTMLDivElement | null>(null);
  const [size, setSize] = useState({ width: 0, height: 0 });
  const positionsRef = useRef<Array<PositionedAgent & { px: number; py: number }>>([]);
  const [hoverSeat, setHoverSeat] = useState<string | null>(null);
  const [hoverPos, setHoverPos] = useState<{ x: number; y: number } | null>(null);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const observer = new ResizeObserver((entries) => {
      const entry = entries[0];
      if (!entry) return;
      const { width, height } = entry.contentRect;
      setSize({ width, height });
    });
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  const positioned = useMemo(
    () => (frame ? layoutAgents(frame.agents, frame.world) : []),
    [frame],
  );
  const activity = useMemo(
    () => (frame ? computeActivity(frame.events, frame.round) : new Map()),
    [frame],
  );
  const lines = useMemo(
    () => (frame ? recentConversationLines(frame.events, frame.round) : []),
    [frame],
  );
  const hiddenRoles = useMemo(
    () => (frame ? hasHiddenRoles(frame.agents) : false),
    [frame],
  );
  const gameDone = frame?.done ?? false;

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || size.width === 0 || size.height === 0) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = size.width * dpr;
    canvas.height = size.height * dpr;
    canvas.style.width = `${size.width}px`;
    canvas.style.height = `${size.height}px`;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    ctx.scale(dpr, dpr);

    const tokens = readTokens();
    const w = size.width;
    const h = size.height;
    const pad = 28;
    // Location labels draw *below* their anchor point (see the `+ 30` in the
    // label fillText call below), so the usable vertical range needs extra
    // headroom at the bottom or a location near y=1 gets its name clipped by
    // the container edge.
    const padBottom = 56;
    const toPx = (nx: number, ny: number) => ({
      px: pad + nx * (w - pad * 2),
      py: pad + ny * (h - pad - padBottom),
    });

    ctx.clearRect(0, 0, w, h);

    // Faint coordinate grid, like a chart plate.
    ctx.strokeStyle = tokens.grid;
    ctx.lineWidth = 1;
    const step = 48;
    for (let x = pad; x <= w - pad; x += step) {
      ctx.beginPath();
      ctx.moveTo(x, pad);
      ctx.lineTo(x, h - pad);
      ctx.stroke();
    }
    for (let y = pad; y <= h - pad; y += step) {
      ctx.beginPath();
      ctx.moveTo(pad, y);
      ctx.lineTo(w - pad, y);
      ctx.stroke();
    }

    // Location anchors -- soft halo + italic name, like a named constellation
    // region on a chart plate. A small-cast game (smallville*.sl, village.sl)
    // gets a full halo+label per instance; a large-scale one (city.sl's 425
    // locations, mostly hundreds of identical "Home"s) would just turn into
    // unreadable label soup at that density, so above LOCATION_LABEL_LIMIT
    // this draws one label per distinct *type*, at that type's first
    // instance, and every instance still gets a small unlabeled dim dot so
    // the cluster shape reads at a glance.
    if (frame?.world) {
      const locations = frame.world.locations;
      const dense = locations.length > LOCATION_LABEL_LIMIT;
      const labelOnce = new Set<string>();
      ctx.textAlign = "center";
      ctx.textBaseline = "top";
      for (const loc of locations) {
        const { px, py } = toPx(loc.x / frame.world.width, loc.y / frame.world.height);
        const showLabel = !dense || !labelOnce.has(loc.type);
        if (showLabel) {
          labelOnce.add(loc.type);
          const halo = ctx.createRadialGradient(px, py, 0, px, py, dense ? 30 : 46);
          halo.addColorStop(0, tokens.constellation);
          halo.addColorStop(1, "transparent");
          ctx.fillStyle = halo;
          ctx.beginPath();
          ctx.arc(px, py, dense ? 30 : 46, 0, Math.PI * 2);
          ctx.fill();

          ctx.fillStyle = tokens.constellation.replace(/[\d.]+\)$/, "0.9)");
          ctx.font = 'italic 12px "Newsreader", Georgia, serif';
          ctx.fillText(loc.type, px, py + (dense ? 22 : 30));
        } else {
          ctx.fillStyle = tokens.constellation;
          ctx.beginPath();
          ctx.arc(px, py, 2, 0, Math.PI * 2);
          ctx.fill();
        }
      }
    }

    // Conversation lines: fresher exchanges are brighter, fading with age.
    const bySeat = new Map(
      positioned.map((p) => {
        const { px, py } = toPx(p.x, p.y);
        return [p.agent.seat, { px, py }];
      }),
    );
    for (const line of lines) {
      const from = bySeat.get(line.a);
      const to = bySeat.get(line.b);
      if (!from || !to) continue;
      const alpha = Math.max(0, 1 - line.age / 4.5);
      ctx.strokeStyle = tokens.accent;
      ctx.globalAlpha = alpha * 0.7;
      ctx.lineWidth = 1.4;
      ctx.beginPath();
      ctx.moveTo(from.px, from.py);
      ctx.lineTo(to.px, to.py);
      ctx.stroke();
      ctx.globalAlpha = 1;
    }

    // Agents, as points of light.
    const nextPositions: Array<PositionedAgent & { px: number; py: number }> = [];
    for (const p of positioned) {
      const { px, py } = toPx(p.x, p.y);
      nextPositions.push({ ...p, px, py });
      const info = activity.get(p.agent.seat);
      const intensity = info?.intensity ?? 0;
      const isSelected = p.agent.seat === selectedSeat;
      const isEliminated = !p.agent.alive;

      if (isEliminated) {
        ctx.strokeStyle = tokens.danger;
        ctx.lineWidth = 1.6;
        const r = 5;
        ctx.beginPath();
        ctx.moveTo(px - r, py - r);
        ctx.lineTo(px + r, py + r);
        ctx.moveTo(px + r, py - r);
        ctx.lineTo(px - r, py + r);
        ctx.stroke();
        continue;
      }

      const radius = STAR_BASE_RADIUS + STAR_MAX_BONUS * intensity + (isSelected ? 2 : 0);
      if (intensity > 0.05) {
        const glow = ctx.createRadialGradient(px, py, 0, px, py, radius * 3.2);
        glow.addColorStop(0, tokens.accentStrong);
        glow.addColorStop(1, "transparent");
        ctx.globalAlpha = Math.min(intensity, 1);
        ctx.fillStyle = glow;
        ctx.beginPath();
        ctx.arc(px, py, radius * 3.2, 0, Math.PI * 2);
        ctx.fill();
        ctx.globalAlpha = 1;
      }

      ctx.fillStyle = intensity > 0.05 ? tokens.accentStrong : tokens.star;
      if (intensity <= 0.05) ctx.globalAlpha = 0.55 + 0.45 * (1 - Math.min(intensity, 1));
      ctx.beginPath();
      ctx.arc(px, py, radius, 0, Math.PI * 2);
      ctx.fill();
      ctx.globalAlpha = 1;

      if (isSelected) {
        ctx.strokeStyle = tokens.accent;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(px, py, radius + 5, 0, Math.PI * 2);
        ctx.stroke();
      }
    }
    positionsRef.current = nextPositions;
  }, [size, positioned, activity, lines, selectedSeat, frame, gameDone, theme]);

  const hitTest = (clientX: number, clientY: number): string | null => {
    const canvas = canvasRef.current;
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    const x = clientX - rect.left;
    const y = clientY - rect.top;
    let closest: { seat: string; dist: number } | null = null;
    for (const p of positionsRef.current) {
      const dist = Math.hypot(p.px - x, p.py - y);
      if (dist <= 16 && (!closest || dist < closest.dist)) {
        closest = { seat: p.agent.seat, dist };
      }
    }
    return closest?.seat ?? null;
  };

  const hoveredAgent =
    hoverSeat != null ? frame?.agents.find((a) => a.seat === hoverSeat) ?? null : null;
  const showTeamFor = (alive: boolean) => !hiddenRoles || !alive || gameDone;

  return (
    <div className="chart-map" ref={containerRef}>
      <canvas
        ref={canvasRef}
        role="img"
        aria-label="Town chart: agents as points of light at their current locations"
        onMouseMove={(e) => {
          const seat = hitTest(e.clientX, e.clientY);
          setHoverSeat(seat);
          setHoverPos(seat ? { x: e.clientX, y: e.clientY } : null);
        }}
        onMouseLeave={() => {
          setHoverSeat(null);
          setHoverPos(null);
        }}
        onClick={(e) => {
          const seat = hitTest(e.clientX, e.clientY);
          onSelect(seat);
        }}
      />
      {gameDone && frame?.winner && (
        <div className="chart-map__winner">
          Winner: <strong>{frame.winner}</strong>
        </div>
      )}
      {hoveredAgent && hoverPos && (
        <div
          className="chart-map__tooltip"
          style={{ left: hoverPos.x + 14, top: hoverPos.y + 14 }}
        >
          <strong>{hoveredAgent.seat}</strong>
          {showTeamFor(hoveredAgent.alive) && (
            <span className="chart-map__tooltip-team"> · {hoveredAgent.role}</span>
          )}
          {!hoveredAgent.alive && hoveredAgent.deathCause && (
            <div className="chart-map__tooltip-death">{hoveredAgent.deathCause}</div>
          )}
        </div>
      )}
    </div>
  );
}
