import { useCallback, useState } from "react";

type Theme = "light" | "dark";
const STORAGE_KEY = "sociallang-sandbox-theme";

function systemPrefersDark(): boolean {
  return window.matchMedia?.("(prefers-color-scheme: dark)").matches ?? true;
}

function resolveInitialTheme(): Theme {
  const saved = window.localStorage.getItem(STORAGE_KEY);
  if (saved === "light" || saved === "dark") return saved;
  return systemPrefersDark() ? "dark" : "light";
}

function applyTheme(theme: Theme) {
  document.documentElement.setAttribute("data-theme", theme);
}

// Applied at module load, before React's first render -- not inside a
// useEffect. ChartMap reads theme colors via getComputedStyle in its own
// effect, and React flushes a child's effects before its parent's; if the
// data-theme attribute were only set from an effect in this hook (called in
// App, ChartMap's parent), the canvas's very first paint -- and every repaint
// on the same commit as a toggle -- would read whatever the attribute
// happened to be a tick late, one render behind. Setting it eagerly here,
// and again synchronously inside toggle() below, means it's simply never
// wrong by the time anything reads it.
applyTheme(resolveInitialTheme());

export function useTheme() {
  const [theme, setTheme] = useState<Theme>(resolveInitialTheme);

  const toggle = useCallback(() => {
    setTheme((prev) => {
      const next: Theme = prev === "dark" ? "light" : "dark";
      applyTheme(next);
      window.localStorage.setItem(STORAGE_KEY, next);
      return next;
    });
  }, []);

  return { theme, toggle };
}
