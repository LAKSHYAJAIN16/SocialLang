import type { GameEntry } from "../games";
import { MoonIcon, SunIcon } from "./Icons";

interface HeaderProps {
  games: GameEntry[];
  selectedId: string;
  onSelect: (id: string) => void;
  theme: "light" | "dark";
  onToggleTheme: () => void;
  round: number;
  onOpenCustom: () => void;
  customActive: boolean;
}

export function Header({
  games,
  selectedId,
  onSelect,
  theme,
  onToggleTheme,
  round,
  onOpenCustom,
  customActive,
}: HeaderProps) {
  return (
    <header className="app-header">
      <div className="app-header__brand">
        <span className="app-header__wordmark">SOCIALSANDBOX</span>
        <span className="app-header__tagline">a night-sky sandbox for watched simulations</span>
      </div>

      <div className="app-header__picker">
        <select
          className="game-select"
          value={customActive ? "__custom__" : selectedId}
          onChange={(e) => {
            if (e.target.value === "__custom__") {
              onOpenCustom();
            } else {
              onSelect(e.target.value);
            }
          }}
          aria-label="Choose a game"
        >
          {games.map((g) => (
            <option key={g.id} value={g.id}>
              {g.title} · {g.agents}
            </option>
          ))}
          <option value="__custom__">Paste custom .sl source…</option>
        </select>
      </div>

      <div className="app-header__right">
        <span className="app-header__plate slc-tabular">
          PLATE <span>{String(round).padStart(2, "0")}</span>
        </span>
        <button
          type="button"
          className="icon-button"
          onClick={onToggleTheme}
          aria-label={theme === "dark" ? "Switch to light theme" : "Switch to dark theme"}
          title={theme === "dark" ? "Switch to light theme" : "Switch to dark theme"}
        >
          {theme === "dark" ? <MoonIcon /> : <SunIcon />}
        </button>
      </div>
    </header>
  );
}
