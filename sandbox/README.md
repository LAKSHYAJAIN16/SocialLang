# Smallville Sandbox

A dedicated visual simulator for SocialLang, built as a website and (via
Electron) a desktop app — not another code IDE. Pick a game, hit Run, and
watch it happen: agents render as points of light on a night-sky chart, each
one flaring brighter with recent activity and dimming when idle; a
conversation draws a line between the two agents involved that lingers and
fades; a reflection surfaces in the log as a newly cataloged "star." See
`PRODUCT.md` (repo root) for the full brief and `DESIGN.md`'s "Generative
Agents architecture" section for what the underlying engine actually does.

`games/smallville_mafia.sl` and `games/smallville.sl` lead the picker — they're
the reason this exists — but all seven example games are selectable, and you
can paste your own `.sl` source.

## Same engine, not a reimplementation

This runs the *exact* JS engine `web/index.html` already ships
(`src/engine/sociallang.js`), extracted into an importable ES module rather
than re-implemented. `scripts/sync-engine.mjs` re-extracts it from
`../web/index.html`'s first `<script>` block — run `npm run sync-engine`
after any change to `sociallang/lang/{lexer,parser,interpreter,memory}.py`
that also needs mirroring into the JS port (see the repo's `web/README.md`).
Like the rest of the JS port, every `ask()`/`ask_choice()`/`converse()` call
here is answered by a mock provider — this can't hold a real API key.

## Running it

```
npm install
npm run dev            # http://localhost:5173, hot-reloading
npm run build           # production build -> dist/
npm run preview         # serve the production build locally
```

## Desktop app (Electron)

```
npm run electron:dev    # dev server + a live Electron window pointed at it
npm run electron:pack   # build + unpacked app in release/win-unpacked (no installer)
npm run electron:dist   # build + a packaged installer in release/ (electron-builder)
```

`electron/main.js` loads `dist/index.html` directly (or the dev server URL
via `SANDBOX_DEV_URL`, set for you by `electron:dev`) — no separate
desktop-only code path. `npm run build-icons` regenerates `build/icon.ico`/
`build/icon.png` from `public/favicon.svg` if that ever changes.

The `electron:dist` installer target (NSIS) was built and verified on
Windows. `package.json`'s `build.mac`/`build.linux` blocks are configured but
unverified in this pass — a macOS or Linux installer needs a CI runner (or
machine) on that OS to actually produce and test one; don't take their
presence in config as evidence they've been built.

## What's different from the real thing

Same caveats as `web/index.html` (see its own README): no real LLM calls, no
real embeddings, `ask_all`/`ask_choice_all` run sequentially rather than
concurrently. Additionally specific to this sandbox:

- Large-population games (`city.sl`'s 5,050 agents, `outbreak.sl`'s 620) still
  run and render, but the map only labels one location per *type* above ~40
  total locations (city.sl alone has 425) — otherwise the labels turn into
  unreadable soup. Every instance still gets a small unlabeled dot so the
  cluster shape reads at a glance.
- A non-spatial game (no `world{}` at all — `trust_game.sl`, `mafia.sl`)
  has no positions to plot; agents are arranged in a fixed ring instead of a
  real layout, so the chart still shows something rather than going blank.
- A hidden-role game's living agents show "role hidden until revealed" in the
  catalog card instead of their team, revealed once eliminated or the game
  ends — so watching doesn't spoil who's mafia before the game does.
