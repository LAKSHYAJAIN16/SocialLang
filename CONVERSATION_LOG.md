# SocialLang conversation log

A running record of Claude Code sessions on this project — what was asked, what
shipped, and why. Newest entry on top.

---

## 2026-09-13 (third entry, same day)

**Asked:** "is there a sandbox that I can utilize to test this out? use
/impeccable to make the UI. and it should be a website and a desktop app."

**Shipped:** `sandbox/` — a new React+TypeScript app, not another code IDE:
a dedicated visual simulator. Went through /impeccable's actual direction
process (product interview -> PRODUCT.md; seven candidate visual worlds
derived from the audience's own culture, weighed against the skill's
catalog challengers via `concept-seed.mjs`; the assigned pick lost outright
to a fused catalog challenger — "star atlas chart" — on both axes, so that's
what got built, presented to the user as a real choice with three ASCII-
preview alternates rather than just building my own favorite). Direction:
**Night-Sky Chart** — the town as a star atlas, agents as points of light
that flare with recent activity, `converse()` exchanges as fading
constellation lines, reflections as newly cataloged stars.

Reuses `web/index.html`'s exact JS engine (extracted into an ES module by
`sandbox/scripts/sync-engine.mjs`) rather than a third reimplementation.
Building the agent catalog card surfaced a real pre-existing gap: neither
engine's agent-snapshot function actually exposed persona/plan/cursor
fields, even though the builtins that write them existed — fixed on both
the Python and JS sides (`sociallang/lang/interpreter.py`, `web/index.html`),
75 Python tests still green.

Verified live in a real Chrome tab (not just build-succeeded): both themes,
agent selection/persona/plan display, log filters, custom-source paste, all
seven games. Found and fixed two real bugs this way: the canvas going stale
on theme toggle (a React effect-ordering race, not just "add a dependency"
— fixed by applying the theme attribute eagerly/synchronously rather than
from a childward-flushing effect), and city.sl's 425 locations turning into
unreadable label soup (fixed with a per-type label-density threshold). Ran
the design skill's mechanical detector (degraded mode, still useful) plus a
manual craft-floor pass, which caught a real violation (a 2px colored
`border-left` marking an agent's current plan step) and fixed it with a dot
marker instead; added one authored motion (the winner badge's entrance).

**Left incomplete, disclosed rather than glossed over:** the desktop app
(Electron) is fully wired — `electron/main.js`, electron-builder config,
generated `.ico`/`.png` icons, `npm run electron:dev`/`electron:dist` — but
the actual installer build could not be completed *in this session*.
Extracting Electron's own distribution into `release/` hits a persistent
EPERM renaming the extraction directory; a temporary (uncommitted) retry
patch in `node_modules` proved the lock holds for the entire packaging
process's lifetime, not a few seconds like a normal antivirus scan, and a
separate background dev-server process was independently killed by the
harness for low memory during the same stretch — both point to this
sandboxed environment's resources, not the app or its config. The desktop
app is otherwise ready to build on a normal machine or in CI.

**Also fixed:** `resize_window`/browser-automation screenshots turned out to
be unreliable in this session (stuck at a fixed viewport regardless of the
requested size) — disclosed to the user rather than faked; mobile-width
verification is implemented (a tested `@media (max-width: 720px)` block,
same pattern as `web/index.html`'s) but not screenshot-confirmed this
session.

---

## 2026-09-13 (follow-up, same day)

**Asked:** correction on the prior entry below — the ask wasn't "add Generative
Agents primitives as a flat demo," it was "use the language to build an actual
Smallville simulation, then show composability: Mafia playable on top of the
Smallville engine, like an API." Also: commit and push consistently going
forward, not just at the end of a long session.

**Shipped:**

- `games/smallville_mafia.sl` — Mafia as a ruleset layered on `smallville.sl`'s
  engine instead of its own flat night/day loop. Every agent (mafia included)
  gets a persona, plans and lives a real day (decompose/move/react/converse/
  reflect, identical machinery to `smallville.sl`), *then* `Night` (mafia secretly
  vote a kill at a `Den`, `whisper`ed) and `DayVote` (the town accuses someone)
  run on top — same shape as `village.sl`'s hidden-role layer, but each day-vote's
  suspicion is grounded in `generative(10)` memory over a real day of dialogue,
  not one scripted "what do you want to say" prompt.
- Verified structurally, not just parsed: ran it 10 seeds under `--mock-only`
  (all complete in 1-3 rounds, both `town`/`mafia` outcomes occur), inspected the
  event log for a real run (night kill stayed hidden via `whisper`, day vote
  eliminated someone, persona/plan/dialogue/reflection events all present
  alongside the Mafia-specific ones).
- `tests/test_interpreter.py::test_smallville_mafia_sl_runs_to_completion_with_mock_provider`
  — checks the Mafia layer actually fired (a `whisper` event, a `"killed in the
  night"` death) on top of the Smallville layer actually firing (persona set,
  `plan`/`dialogue` events present). 74 → 75 passing.
- Mirrored in `web/index.html`'s JS port (7th picker entry) — verified under Node
  across 4 seeds, same structural results as the Python version.
- Docs: README.md (games/architecture-section update), DESIGN.md (a full
  paragraph on what's actually different from `village.sl` and why it's thin),
  web/README.md (game count 6 → 7).
- Committed and pushed twice this session (once for the prior entry's work, once
  for this one) rather than batching everything into one end-of-session commit,
  per the reminder above.

---

## 2026-09-13

**Asked:** general check-in on the project, then: "make sure that ur programming
[i.e. actually shipping code, not just talking] and writing a log of all of the
conversations," then "it needs to be a complete project written on top of
simulcra [sic, Park et al. 2023's "Generative Agents" / Smallville paper]."

**Starting state:** repo clean, `main` at `6f6bc5c` (Sandbox Step mode). The
memory-stream half of Park et al. 2023 already existed (`generative(k)` memory,
`reflect()`), but the paper's other three mechanisms — persona, recursive
planning, reacting, dialogue generation — didn't.

**Shipped:**

- New `Agent` fields (`persona`, `plan`/`plan_cursor`, `subplan`/`subplan_cursor`,
  `last_reflect_seq`) and nine new builtins in `sociallang/lang/interpreter.py`:
  `set_persona`, `make_plan`, `current_step`, `decompose_step`, `current_action`,
  `advance_plan`, `react`, `converse`, `maybe_reflect`.
- `games/smallville.sl` — a new sixth example game wiring all of it together
  (4 villagers, personas, a spatial `world{}`, one 12-hour day: plan → decompose →
  move → react → converse → threshold-reflect each hour).
- `tests/test_generative_agents.py` (7 new tests, direct-Interpreter style) plus
  a `smallville.sl` end-to-end smoke test in `tests/test_interpreter.py`. Full
  suite: 66 → 74 passing.
- Mirrored every bit of the above in `web/index.html`'s hand-maintained JS port
  (Agent fields, the nine builtins, the farewell heuristic, `smallville.sl`
  embedded as a sixth picker entry, syntax-highlighter keyword list, tutorial
  tables) — verified by extracting the JS and running it under Node against
  `smallville.sl` and all five prior games.
- `sociallang/visualize.py`: CSS for the two new event kinds (`plan`, `dialogue`).
- Docs: README.md (new "Generative Agents architecture" section + builtins/games
  tables), DESIGN.md (full architecture writeup incl. what's still simplified
  vs. the paper), web/README.md (game count).

**Left alone / known simplifications** (documented in DESIGN.md rather than
silently glossed over): `react()`/`converse()` are single-LLM-call versions of
the paper's separate importance-gated trigger and per-turn retrieval-augmented
continuation; planning is two levels deep (day → chunk) rather than the paper's
three (day → hour → 5-15 min); persona is one free-text field, not a structured
trait/relationship schema.
