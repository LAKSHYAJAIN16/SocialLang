# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

React (Vite + TypeScript), reusing SocialLang's existing hand-maintained JS engine
(extracted from `web/index.html` into an importable module) rather than
re-implementing the interpreter. The web build is also wrapped in Electron for a
desktop app, with an installer produced via electron-builder.

## Users

Anyone curious about SocialLang who wants to *watch* a simulation rather than read
its language reference or an event-log HTML replay: people evaluating the
language, people sharing a Generative-Agents-style demo, and the author testing
`games/smallville.sl` / `games/smallville_mafia.sl` (and other `world{}` games)
while developing them. Not gated behind any account or install — the web version
is the low-friction path; the desktop app is for people who want it as a standing
app rather than a browser tab.

## Product Purpose

SocialLang is a real programming language for defining social-simulation games
and running them with LLM agents (see `README.md`/`DESIGN.md`). The existing
`web/index.html` is a code-first IDE (editor + text log + a generic drag-marker
map). This new surface is not an IDE — it's a dedicated visual simulator: pick a
game (starting with the two Smallville-engine ones), watch agents live in a town
— walking between locations, having real generated conversations, forming
reflections — and, for `smallville_mafia.sl`, watch the hidden-role layer (a
secret night kill, a day accusation vote) play out on top of that same lived-in
town. Success is a visitor immediately understanding *what SocialLang simulations
actually look like happening*, without reading a single line of `.sl` source.

## Positioning

Every other way to see a SocialLang run — the JSON log, `visualize.py`'s HTML
replay, the Unity viewer, `web/index.html`'s own generic Sandbox tab — either
requires reading text or was built game-agnostic first and visual second. This
surface is built inside-out from "make a Smallville-style simulation legible and
watchable": personas, plans, live dialogue, and reflections are first-class UI,
not a click-to-expand debug log. No other artifact in this project shows an
agent's plan advancing, a conversation happening bubble-by-bubble, and a hidden
night-kill vote all on one live town map.

## Operating Context

Runs standalone (no server, no account, no API key — same mock-provider
constraint as `web/index.html`: a public page can't hold a real key, so every
`ask()`/`ask_choice()`/`converse()` call is answered by a mock model). A visitor
picks one of the shipped example games (the two Smallville-engine ones front and
center; the other five still available) or pastes their own `.sl` source, then
runs/steps/plays the simulation and watches it on the town map. The desktop build
is the same experience in a native window via Electron, installed rather than
bookmarked.

## Capabilities and Constraints

- Reuses the *exact* engine `web/index.html` already ships (lexer/parser/
  interpreter/memory-retrieval, ported to JS) — extracted into a standalone
  module so both surfaces import the same source instead of drifting.
- Mock provider only, same as `web/index.html` — no real LLM calls, no
  embeddings. A future real-model mode is a plausible follow-up, not in scope now.
- `world{}` is required for the map view to mean anything — non-spatial games
  (e.g. `trust_game.sl`) still run, but the map naturally has nothing to show;
  the UI should degrade to an event feed in that case rather than break.
- No accounts, no persistence beyond `localStorage` for last-picked game / theme.
- Electron packaging targets Windows (NSIS installer) in this pass, built and
  verified on this machine; macOS/Linux installers need CI runners with those
  OSes and are a documented follow-up, not fabricated here.

## Brand Commitments

None yet — no existing name/logo for this surface beyond "SocialLang." Follows
the parent project's factual claims (a real language, not a config schema; LLM
agents; Generative Agents architecture) — nothing here should overstate what the
mock-provider demo actually does.

## Evidence on Hand

Real: the full JS engine, all seven `games/*.sl` example sources (already
base64-embedded in `web/index.html`, reusable here), `DESIGN.md`'s architecture
writeup. No user testimonials, screenshots, or press — none should be invented.

## Product Principles

1. The map is the product — every other panel (dialogue feed, plan readout,
   reflections) exists to explain what's happening on the map, never to replace
   looking at it.
2. Legible over exhaustive — a first-time visitor should understand what's
   happening within seconds of hitting Run, before reading any explanatory copy.
3. Same engine, no drift — behavior differences from the Python reference are
   engine bugs to fix upstream, not this surface's problem to work around.
4. Honest about the mock — never implies a real model is behind what's on
   screen; the copy says so plainly, once, without apologizing for it.
5. One good path, not a config panel — favor a handful of well-chosen defaults
   (which games are featured, what Run/Step/Play do) over exposing every builtin
   or engine flag as a UI control.

## Accessibility & Inclusion

No user-specific requirement stated. Must work at phone width and in both
system light/dark themes (stated constraint); keyboard operability and
color-contrast for the map's agent markers matter since status (alive/dead,
team-hidden-until-revealed) is currently color-coded in the reference IDE — this
surface should not rely on color alone for that distinction.
