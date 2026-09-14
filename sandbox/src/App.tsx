import { useEffect, useState } from "react";
import { Header } from "./components/Header";
import { ControlBar } from "./components/ControlBar";
import { ObservationLog } from "./components/ObservationLog";
import { AgentPanel } from "./components/AgentPanel";
import { CustomSourcePanel } from "./components/CustomSourcePanel";
import { ChartMap } from "./map/ChartMap";
import { useSimulation } from "./engine/useSimulation";
import { useTheme } from "./useTheme";
import { GAMES, DEFAULT_GAME_ID } from "./games";
import { hasHiddenRoles } from "./map/mapMath";

export default function App() {
  const { theme, toggle: toggleTheme } = useTheme();
  const sim = useSimulation();
  const [selectedGameId, setSelectedGameId] = useState(DEFAULT_GAME_ID);
  const [showCustom, setShowCustom] = useState(false);
  const [customSource, setCustomSource] = useState("");
  const [customActive, setCustomActive] = useState(false);
  const [selectedSeat, setSelectedSeat] = useState<string | null>(null);

  useEffect(() => {
    const game = GAMES.find((g) => g.id === selectedGameId);
    if (game) sim.load(game.source);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedGameId]);

  useEffect(() => {
    setSelectedSeat(null);
  }, [selectedGameId, customActive]);

  const frame = sim.frame;
  const hiddenRoles = frame ? hasHiddenRoles(frame.agents) : false;
  const selectedAgent = frame?.agents.find((a) => a.seat === selectedSeat) ?? null;

  return (
    <div className="app-shell">
      <Header
        games={GAMES}
        selectedId={selectedGameId}
        onSelect={(id) => {
          setCustomActive(false);
          setShowCustom(false);
          setSelectedGameId(id);
        }}
        theme={theme}
        onToggleTheme={toggleTheme}
        round={frame?.round ?? 0}
        onOpenCustom={() => setShowCustom(true)}
        customActive={customActive}
      />

      {showCustom ? (
        <CustomSourcePanel
          initialSource={customSource || GAMES[0].source}
          error={sim.error}
          onLoad={(source) => {
            setCustomSource(source);
            setCustomActive(true);
            setShowCustom(false);
            sim.load(source);
          }}
          onCancel={() => setShowCustom(false)}
        />
      ) : (
        <main className="app-main">
          <div className="app-main__map">
            {sim.status === "error" ? (
              <div className="app-main__error">
                <p>This program didn't parse:</p>
                <pre>{sim.error}</pre>
              </div>
            ) : (
              <ChartMap
                frame={frame}
                selectedSeat={selectedSeat}
                onSelect={setSelectedSeat}
                theme={theme}
              />
            )}
          </div>
          <aside className="app-main__side">
            <AgentPanel
              agent={selectedAgent}
              hiddenRoles={hiddenRoles}
              gameDone={frame?.done ?? false}
            />
            <ObservationLog events={frame?.events ?? []} />
          </aside>
        </main>
      )}

      <ControlBar
        status={sim.status}
        isPlaying={sim.isPlaying}
        cursor={sim.cursor}
        frameCount={sim.frames.length}
        winner={frame?.winner ?? null}
        seed={sim.seed}
        onSeedCommit={sim.reloadWithSeed}
        onStep={sim.step}
        onRun={sim.run}
        onPlayPause={sim.isPlaying ? sim.pause : sim.play}
        onScrub={sim.scrubTo}
      />

      <footer className="app-footer">
        <p>
          Every agent here is answered by a mock model, not a real LLM -- this page can't hold
          an API key. Same engine as the Python reference (<code>sociallang run</code>), same
          grammar, same builtins.
        </p>
      </footer>
    </div>
  );
}
