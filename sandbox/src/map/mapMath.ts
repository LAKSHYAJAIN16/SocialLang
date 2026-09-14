import type { AgentSnapshot, LogEntry, WorldSnapshot } from "../engine/types";

export interface PositionedAgent {
  agent: AgentSnapshot;
  x: number; // normalized 0..1
  y: number; // normalized 0..1
}

function seatIndex(seat: string): number {
  const match = /\d+/.exec(seat);
  return match ? parseInt(match[0], 10) : 0;
}

/**
 * Maps each agent onto normalized (0..1) chart coordinates. A spatial game
 * (world{} present) uses its real x/y, scaled by the world's declared
 * width/height; an agent not yet spawned parks along a stable per-seat point
 * on the bottom edge instead of stacking at the origin. A non-spatial game
 * (no world{} at all -- trust_game.sl, mafia.sl) has no positions to read at
 * all, so agents are arranged in a fixed ring: the chart still shows
 * something rather than going blank (see PRODUCT.md's "degrade, don't break").
 */
export function layoutAgents(
  agents: AgentSnapshot[],
  world: WorldSnapshot | null,
): PositionedAgent[] {
  if (world && world.width > 0 && world.height > 0) {
    return agents.map((agent) => {
      if (agent.x == null || agent.y == null) {
        const idx = seatIndex(agent.seat);
        return {
          agent,
          x: (idx + 0.5) / Math.max(agents.length, 1),
          y: 0.94,
        };
      }
      return { agent, x: agent.x / world.width, y: agent.y / world.height };
    });
  }
  const n = Math.max(agents.length, 1);
  return agents.map((agent, i) => {
    const angle = (i / n) * Math.PI * 2 - Math.PI / 2;
    return {
      agent,
      x: 0.5 + 0.36 * Math.cos(angle),
      y: 0.5 + 0.36 * Math.sin(angle),
    };
  });
}

export interface ActivityInfo {
  intensity: number; // 0..1, current glow/size multiplier
  lastRound: number;
}

// A client-side proxy for "how important/recent was this agent's last visible
// action" -- the real engine computes a genuine recency+importance+relevance
// score per memory retrieval (see DESIGN.md's memory-patterns section), but
// that per-event importance number isn't part of the log entries the engine
// hands to a sink (only kind/text/author/round are). This reconstructs a
// reasonable proxy from event *kind* alone, decaying with rounds elapsed,
// rather than reaching into the engine to expose a number nothing else needs.
const KIND_WEIGHT: Record<string, number> = {
  reflection: 1,
  dialogue: 0.75,
  plan: 0.6,
  broadcast: 0.55,
  whisper: 0.5,
  ask: 0.4,
  note: 0.35,
  print: 0.2,
};

export function computeActivity(
  events: LogEntry[],
  currentRound: number,
): Map<string, ActivityInfo> {
  const map = new Map<string, ActivityInfo>();
  for (const e of events) {
    if (!e.author) continue;
    const weight = KIND_WEIGHT[e.kind] ?? 0.3;
    const existing = map.get(e.author);
    if (!existing || e.round >= existing.lastRound) {
      map.set(e.author, { lastRound: e.round, intensity: weight });
    }
  }
  for (const [seat, info] of map) {
    const age = Math.max(currentRound - info.lastRound, 0);
    const decay = Math.pow(0.75, age);
    map.set(seat, { lastRound: info.lastRound, intensity: info.intensity * decay });
  }
  return map;
}

export interface ConversationLine {
  a: string;
  b: string;
  age: number;
}

/** Recent converse() exchanges, as fading lines between the two participants
 * -- the chart's signature interaction (see PRODUCT.md's Positioning). */
export function recentConversationLines(
  events: LogEntry[],
  currentRound: number,
  maxAge = 4,
): ConversationLine[] {
  const lines: ConversationLine[] = [];
  for (const e of events) {
    if (e.kind !== "dialogue" || !e.author || !e.visibleTo) continue;
    const age = currentRound - e.round;
    if (age > maxAge || age < 0) continue;
    const other = e.visibleTo.find((seat) => seat !== e.author);
    if (!other) continue;
    lines.push({ a: e.author, b: other, age });
  }
  return lines;
}

/** Whether this game declares more than one distinct team -- a proxy for
 * "has hidden roles" (mafia.sl/village.sl/smallville_mafia.sl all do; a game
 * with one uniform team, like smallville.sl's "villager", does not). Used to
 * keep a living agent's team/role masked until it's eliminated or the game
 * ends, so the sandbox doesn't spoil its own hidden-role games. */
export function hasHiddenRoles(agents: AgentSnapshot[]): boolean {
  const teams = new Set(agents.map((a) => a.team));
  return teams.size > 1;
}
