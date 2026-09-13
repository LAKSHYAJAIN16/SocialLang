# SocialLang conversation log

A running record of Claude Code sessions on this project — what was asked, what
shipped, and why. Newest entry on top.

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
