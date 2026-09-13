# SocialLang conversation log

A running record of Claude Code sessions on this project — what was asked, what
shipped, and why. Newest entry on top.

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
