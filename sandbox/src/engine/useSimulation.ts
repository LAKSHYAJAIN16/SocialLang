import { useCallback, useRef, useState } from "react";
import {
  parse,
  assignAgents,
  Interpreter,
  MockProvider,
  SeededRandom,
} from "./sociallang.js";
import type { Frame, LogEntry, SimStatus, WorldSnapshot } from "./types";

const PLAY_INTERVAL_MS = 750;
// A safety cap only -- every shipped example game reaches its own
// win_condition long before this (see DESIGN.md's per-game round counts);
// this just stops a pathological pasted-in custom .sl from looping forever
// when Run fast-forwards it synchronously.
const SAFETY_MAX_ROUNDS = 300;

export interface SimulationHandle {
  status: SimStatus;
  error: string | null;
  frames: Frame[];
  cursor: number;
  frame: Frame | null;
  seed: number;
  load: (source: string) => void;
  reloadWithSeed: (seed: number) => void;
  step: () => void;
  run: () => void;
  play: () => void;
  pause: () => void;
  isPlaying: boolean;
  scrubTo: (index: number) => void;
}

export function useSimulation(): SimulationHandle {
  const [status, setStatus] = useState<SimStatus>("idle");
  const [error, setError] = useState<string | null>(null);
  const [frames, setFrames] = useState<Frame[]>([]);
  const [cursor, setCursor] = useState(0);
  const [seed, setSeed] = useState(1);
  const [isPlaying, setIsPlaying] = useState(false);

  const interpRef = useRef<any>(null);
  const worldRef = useRef<WorldSnapshot | null>(null);
  const eventsRef = useRef<LogEntry[]>([]);
  const playTimerRef = useRef<number | null>(null);
  const framesLenRef = useRef(0);
  const sourceRef = useRef<string>("");

  const stopPlaying = useCallback(() => {
    if (playTimerRef.current !== null) {
      window.clearInterval(playTimerRef.current);
      playTimerRef.current = null;
    }
    setIsPlaying(false);
  }, []);

  const pushFrame = useCallback((frame: Frame) => {
    setFrames((prev) => {
      const next = [...prev, frame];
      framesLenRef.current = next.length;
      return next;
    });
    setCursor(() => framesLenRef.current - 1);
  }, []);

  // The one place a fresh Interpreter gets built, shared by load() (a new
  // program) and reloadWithSeed() (the same program, a different seed) -- so
  // there's exactly one code path to keep in sync with the engine's
  // constructor shape, not two drifting copies.
  const buildInterpreter = useCallback((source: string, withSeed: number) => {
    sourceRef.current = source;
    setError(null);
    try {
      const sim = parse(source);
      // Enough distinct mock models that assignAgents' rng.sample() never has
      // to fall back to reusing one -- see assignAgents in sociallang.js.
      // Every provider is an identical stateless MockProvider anyway, so
      // having "enough" costs nothing.
      const rosterSize = Math.max(sim.agentsMax, 1);
      const roster = new Map<string, any>();
      for (let i = 0; i < rosterSize; i += 1) roster.set(`mock-${i}`, new MockProvider());

      const assignRng = new SeededRandom(withSeed);
      const { agents, rolesByName } = assignAgents(sim, roster, assignRng);

      eventsRef.current = [];
      const sink = {
        onWorld: () => {},
        onAgents: () => {},
        onEvent: (entry: LogEntry) => {
          eventsRef.current = [...eventsRef.current, entry];
        },
        onDone: () => {},
      };

      const interp = new Interpreter(sim, agents, rolesByName, { seed: withSeed, sink });
      interpRef.current = interp;
      worldRef.current = interp.getWorldSnapshot();

      const initialFrame: Frame = {
        round: 0,
        agents: interp.getAgentsSnapshot(),
        world: worldRef.current,
        events: [],
        winner: null,
        done: false,
      };
      framesLenRef.current = 1;
      setFrames([initialFrame]);
      setCursor(0);
      setStatus("ready");
    } catch (e: unknown) {
      const message = e instanceof Error ? e.message : String(e);
      setError(message);
      setStatus("error");
      setFrames([]);
      framesLenRef.current = 0;
      interpRef.current = null;
    }
  }, []);

  const load = useCallback(
    (source: string) => {
      stopPlaying();
      buildInterpreter(source, seed);
    },
    [buildInterpreter, seed, stopPlaying],
  );

  const reloadWithSeed = useCallback(
    (nextSeed: number) => {
      stopPlaying();
      setSeed(nextSeed);
      if (sourceRef.current) buildInterpreter(sourceRef.current, nextSeed);
    },
    [buildInterpreter, stopPlaying],
  );

  const stepOnce = useCallback((): boolean => {
    const interp = interpRef.current;
    if (!interp) return true;
    const { done, winner } = interp.stepOnce();
    const frame: Frame = {
      round: interp.round,
      agents: interp.getAgentsSnapshot(),
      world: worldRef.current,
      events: eventsRef.current,
      winner,
      done,
    };
    pushFrame(frame);
    if (done) setStatus("done");
    return done;
  }, [pushFrame]);

  const step = useCallback(() => {
    if (!interpRef.current || status === "done") return;
    setStatus((prevStatus) => (prevStatus === "done" ? prevStatus : "running"));
    const done = stepOnce();
    if (!done) setStatus("ready");
  }, [status, stepOnce]);

  const run = useCallback(() => {
    if (!interpRef.current) return;
    setStatus("running");
    let rounds = 0;
    let done = false;
    while (!done && rounds < SAFETY_MAX_ROUNDS) {
      done = stepOnce();
      rounds += 1;
    }
    if (!done) setStatus("ready");
  }, [stepOnce]);

  const play = useCallback(() => {
    if (!interpRef.current || playTimerRef.current !== null) return;
    setStatus("running");
    setIsPlaying(true);
    playTimerRef.current = window.setInterval(() => {
      const done = stepOnce();
      if (done) stopPlaying();
    }, PLAY_INTERVAL_MS);
  }, [stepOnce, stopPlaying]);

  const pause = useCallback(() => {
    stopPlaying();
    setStatus((prevStatus) => (prevStatus === "done" ? prevStatus : "ready"));
  }, [stopPlaying]);

  const scrubTo = useCallback((index: number) => {
    setCursor(Math.max(0, Math.min(index, framesLenRef.current - 1)));
  }, []);

  return {
    status,
    error,
    frames,
    cursor,
    frame: frames[cursor] ?? null,
    seed,
    load,
    reloadWithSeed,
    step,
    run,
    play,
    pause,
    isPlaying,
    scrubTo,
  };
}
