import { useState } from "react";
import type { SimStatus } from "../engine/types";
import { PauseIcon, PlayIcon, RunIcon, StepIcon } from "./Icons";

interface ControlBarProps {
  status: SimStatus;
  isPlaying: boolean;
  cursor: number;
  frameCount: number;
  winner: string | null;
  seed: number;
  onSeedCommit: (seed: number) => void;
  onStep: () => void;
  onRun: () => void;
  onPlayPause: () => void;
  onScrub: (index: number) => void;
}

export function ControlBar({
  status,
  isPlaying,
  cursor,
  frameCount,
  winner,
  seed,
  onSeedCommit,
  onStep,
  onRun,
  onPlayPause,
  onScrub,
}: ControlBarProps) {
  const disabled = status === "idle" || status === "error";
  const atEnd = status === "done" && cursor === frameCount - 1;
  const [seedDraft, setSeedDraft] = useState(String(seed));
  // Adjusted during render rather than via a useEffect (React's recommended
  // pattern for "reset local state when a prop changes"): a plain effect here
  // would commit the stale draft for one extra frame, then re-render again to
  // fix it, for no benefit over comparing against the last-seen prop directly.
  const [lastSeed, setLastSeed] = useState(seed);
  if (seed !== lastSeed) {
    setLastSeed(seed);
    setSeedDraft(String(seed));
  }

  const commitSeed = () => {
    const parsed = Number(seedDraft);
    if (Number.isFinite(parsed) && parsed !== seed) onSeedCommit(parsed);
    else setSeedDraft(String(seed));
  };

  return (
    <div className="control-bar">
      <div className="control-bar__transport">
        <button
          type="button"
          className="control-button"
          onClick={onStep}
          disabled={disabled || atEnd}
          title="Advance one round"
        >
          <StepIcon />
          <span>Step</span>
        </button>
        <button
          type="button"
          className="control-button"
          onClick={onPlayPause}
          disabled={disabled || (atEnd && !isPlaying)}
          title={isPlaying ? "Pause" : "Play"}
        >
          {isPlaying ? <PauseIcon /> : <PlayIcon />}
          <span>{isPlaying ? "Pause" : "Play"}</span>
        </button>
        <button
          type="button"
          className="control-button control-button--accent"
          onClick={onRun}
          disabled={disabled || atEnd}
          title="Fast-forward to the end"
        >
          <RunIcon />
          <span>Run</span>
        </button>
      </div>

      <div className="control-bar__scrub">
        <input
          type="range"
          min={0}
          max={Math.max(frameCount - 1, 0)}
          value={cursor}
          onChange={(e) => onScrub(Number(e.target.value))}
          disabled={frameCount <= 1}
          aria-label="Scrub through rounds"
        />
        <span className="control-bar__scrub-label slc-tabular">
          {cursor} / {Math.max(frameCount - 1, 0)}
        </span>
      </div>

      <div className="control-bar__meta">
        <label className="control-bar__seed">
          <span>Seed</span>
          <input
            type="number"
            className="slc-tabular"
            value={seedDraft}
            onChange={(e) => setSeedDraft(e.target.value)}
            onBlur={commitSeed}
            onKeyDown={(e) => {
              if (e.key === "Enter") commitSeed();
            }}
          />
        </label>
        {winner && (
          <span className="control-bar__winner">
            Winner: <strong>{winner}</strong>
          </span>
        )}
      </div>
    </div>
  );
}
