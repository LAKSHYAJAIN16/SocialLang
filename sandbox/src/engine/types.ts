// Loose structural types for the plain-JS SocialLang engine (sociallang.js) --
// it has no .d.ts of its own since it's a hand-maintained port, not a build
// artifact. These describe the shapes the engine's public methods actually
// hand back (see sociallang/lang/interpreter.py for the authoritative Python
// shapes this mirrors).

export interface AgentSnapshot {
  seat: string;
  role: string;
  team: string;
  model: string;
  alive: boolean;
  deathCause: string | null;
  x: number | null;
  y: number | null;
  locationId: string | null;
  persona: string;
  plan: PlanStep[];
  planCursor: number;
  subplan: string[];
  subplanCursor: number;
}

export interface PlanStep {
  time: string;
  activity: string;
}

export interface LocationSnapshot {
  id: string;
  type: string;
  tag: string | null;
  capacity: number | null;
  x: number;
  y: number;
}

export interface WorldSnapshot {
  width: number;
  height: number;
  locations: LocationSnapshot[];
}

export interface LogEntry {
  seq: number | null;
  round: number;
  kind: string;
  text: string;
  author: string | null;
  visibleTo: string[] | null;
}

export interface RunResult {
  winner: string | null;
  rounds: number;
  log: LogEntry[];
  agents: AgentSnapshot[];
  world: WorldSnapshot | null;
}

// A per-round snapshot the sandbox itself accumulates as a simulation
// advances -- not an engine type, but what the UI actually scrubs/plays back.
export interface Frame {
  round: number;
  agents: AgentSnapshot[];
  world: WorldSnapshot | null;
  events: LogEntry[]; // cumulative log up to and including this round
  winner: string | null;
  done: boolean;
}

export type SimStatus = "idle" | "ready" | "running" | "done" | "error";
