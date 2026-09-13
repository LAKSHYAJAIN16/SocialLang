# SocialLang design doc

## What this is

SocialLang is a real programming language — its own lexer, parser, and tree-walking
interpreter (`sociallang/lang/`) — for defining social games and running them with LLM
agents. A `.sl` program declares a population of agents, the roles they can hold, the
phases the game moves through, and the rules of each phase as actual code: variables,
loops, conditionals, functions. There's no host language required to add a new game —
write a `.sl` file and run it.

**This supersedes an earlier draft of this doc**, which specified a declarative YAML
config schema instead. That's the wrong shape for "custom rules" — a config file can't
express "if the accused player has been silent for 2 rounds, skip the vote" without the
engine growing a bespoke feature for every such rule. A real language with control flow
doesn't have that ceiling: the rule is just code.

`sociallang/providers/` is unchanged from that earlier version — MafiaSim's LLM provider
plumbing (`ChatProvider` abstraction, all four adapters, roster loading, retry logic),
ported as-is because it's already game-agnostic.

## Language overview

```
sim Mafia {
  agents: 6..10                       // population size drawn from this range at start

  memory recent(n) {                  // a custom memory pattern, written in SocialLang
    return last(events, n)            // itself -- see "Memory patterns" below
  }

  role Mafia {
    team: "mafia"
    memory: recent(20)                // mafia only see their last 20 visible events
    sees: teammates                   // told who else shares this role
    count: 2
  }

  role Villager {
    team: "town"
    memory: full_history              // built-in: every visible event, no windowing
    sees: none
    count: remainder                  // fills whatever's left of the drawn population
  }

  phase Night {
    let targets = alive()
    let votes = {}
    for m in with_role("Mafia") {
      if m.alive {
        votes[m] = ask_choice(m, "Who should the mafia eliminate tonight?", targets)
      }
    }
    let victim = tally(votes)
    if victim != null {
      eliminate(victim, "killed by the mafia")
    }
  }

  win_condition {
    let mafia_left = 0
    let town_left = 0
    for a in alive() {
      if a.team == "mafia" { mafia_left = mafia_left + 1 } else { town_left = town_left + 1 }
    }
    if mafia_left == 0 { return "town" }
    if mafia_left >= town_left { return "mafia" }
  }

  loop {
    run Night
    let winner = check_win()
    if winner != null { return winner }
  }
}
```

The worked examples: `games/mafia.sl` (a complete Mafia implementation),
`games/trust_game.sl` (a structurally different game — no hidden roles, no elimination,
just repeated cooperate/defect rounds with a round-count win condition),
`games/city.sl` (a spatial, 5,050-agent demo — see "World and scale" below),
`games/village.sl` (hidden-role social deduction combined with a `world {}` — wolves
plan in a private Den, the village votes at a public Plaza, location gates who
actually hears the night chatter), and `games/outbreak.sl` (a 620-agent spatial
contagion sim where a 100-agent LLM tier votes on a lockdown policy each round via
`ask_choice_all`, and the vote's outcome measurably changes how far the "infection"
spreads) — proof the language isn't accidentally Mafia-shaped, or small-population-shaped.

### Grammar

Top-level: a program is exactly one `sim <Name> { ... }` block containing, in any order:

- `agents: <n>` or `agents: <lo>..<hi>` — population size (fixed or a range, redrawn each run)
- `role <Name> { team: "..", memory: <pattern>(<args>), sees: teammates|none, count: <n>|remainder }`
  — at most one role may use `count: remainder`
- `world { width: <n>, height: <n>, location <Type> { tag: "..", capacity: <n>, count: <n> } ... }`
  — optional; see "World and scale" below
- `memory <name>(<params>) { <statements> }` — a custom memory pattern (see below)
- `fn <name>(<params>) { <statements> }` — an ordinary function
- `phase <name> { <statements> }` — a named block of game logic, invoked with `run <name>`
- `win_condition { <statements> }` — evaluated on demand via the `check_win()` builtin;
  `return <value>` reports a winner, falling off the end (or `return` with no value)
  means "no winner yet"
- `loop { <statements> }` — the simulation's main loop, re-executed once per round (up to
  a safety cap); `run <phase>` invokes a phase inline, and `return <value>` anywhere in
  the loop (or in a phase it calls) ends the simulation with that value as the winner

Statements: `let`, plain assignment (`x = ...`, `d[k] = ...`), `if`/`else if`/`else`,
`while`, `for x in <expr>`, `return`, `break`, `run <phase>`, and bare expression calls.
Expressions: numbers, strings, `true`/`false`/`null`, list `[...]` and dict `{k: v}`
literals, `and`/`or`/`not`, comparisons, arithmetic, `.attr` access, `[index]`, and
function calls with positional and `name=value` keyword arguments. `.attr` accepts any
word after the dot, including reserved ones like `role` — `agent.role` works the same
way `role`-as-a-role-decl-field-name already did, since an attribute name right after
`.` is never ambiguous with the keyword's other meaning. `+` concatenates two
strings or adds two numbers; concatenating a string with anything else needs an explicit
`str(x)` first.

### Built-in functions

| Function | Does |
|---|---|
| `ask(agent, prompt, temperature=, max_tokens=)` | Calls the agent's model, returns its raw text reply |
| `ask_choice(agent, prompt, options)` | Same, constrained to match one item of `options` |
| `broadcast(text)` / `broadcast(agent, text)` | Publishes an event every agent can see |
| `whisper(agents, text)` | Publishes an event only the given agents can see |
| `remember(agent, text)` | Adds a private note to one agent's own memory |
| `reflect(agent)` | Asks the agent to synthesize 1-3 insights from its retrieved memory (see below); returns them and also stores them as new, high-importance memories |
| `alive()` | List of currently-alive agents |
| `all_agents()` | Every agent, alive or not — the raw roster (`agents` itself can't be referenced as an expression; it's a reserved top-level field keyword, so this is how a script gets the full, unfiltered list) |
| `with_role(name)` | List of agents (alive or not) holding that role |
| `team_of(agent)` | That agent's role's team string |
| `eliminate(agent, cause=)` | Marks an agent dead and broadcasts it |
| `tally(votes)` | Given a dict of agent → target, returns the majority target (ties broken randomly) |
| `count(x)`, `last(list, n)`, `random_choice(list)`, `str(x)`, `print(x)` | Utility |
| `check_win()` | Runs `win_condition` once, returns its result (or `null`) |
| `ask_all(agents, prompt, temperature=, max_tokens=, max_workers=)` | Bulk `ask()`: fires every agent's `provider.complete()` call concurrently (a thread pool, since `ChatProvider.complete` is a stateless blocking call), returns `{agent: text}` |
| `ask_choice_all(agents, prompt, options)` | Bulk `ask_choice()`, same concurrency, returns `{agent: chosen_option}` |
| `locations()` / `locations_by_tag(tag)` | All procedurally-placed `Location`s from the `world` block, optionally filtered by tag |
| `spawn_agents_at(agents, tag=)` | Randomly places each agent at a location (optionally restricted to one tag), setting its position |
| `move_to(agent, location)` | Moves one agent to a specific `Location` |
| `location_of(agent)` | The `Location` an agent currently occupies, or `null` |
| `agents_at(location)` | Agents currently at a given `Location` |
| `nearby(agent, radius)` | Other agents within `radius` of this agent's position |

### World and scale

An optional `world { }` block declares a spatial layout procedurally, so a game
doesn't hand-place coordinates for a large population:

```
world {
  width: 200
  height: 200
  location Home   { tag: "private", count: 400 }
  location Market { tag: "public", capacity: 200, count: 20 }
}
```

Each `location <Type> { }` declares a *kind* of place — a tag, an optional capacity,
and how many instances of it should exist. At sim start, the interpreter scatters that
many concrete instances across the `width` x `height` grid using the same seeded RNG
as everything else (seeded rejection-sampling with a minimum spacing, so instances
don't stack), so placement is reproducible under `--seed` without the game author ever
writing a coordinate. `Agent`s get `x`/`y`/`location` fields (all `null` until
`spawn_agents_at`/`move_to` is called), and the spatial builtins above are how a
program queries or changes them.

This is also the enabling piece for large populations: SocialLang doesn't have (and
doesn't need) a separate "scripted vs. LLM" tier as new syntax — a game already has
everything required to express it with a plain `fn` and an `if` on role/team, since
`role { count: 5000 }` and function calls were already in the language. `games/city.sl`
demonstrates the pattern: 5,000 `Citizen` agents wander each round via an ordinary
`fn` with zero LLM calls, while 50 `Journalist` agents are asked for a headline every
round via `ask_choice_all` — which is the other scale-enabling piece: dispatching that
batch's `provider.complete()` calls concurrently instead of serializing 50 sequential
round trips. Running all 5,050 agents through a full game (`sociallang run
games/city.sl --mock-only`) takes about a second. The other requirement at that scale
was an engine fix, not a language feature: `_visible_events_for` used to do a full scan
of the entire event log on every single `ask()` call (`O(agents x events)` per round,
over a growing log); it's now backed by a per-agent public/private event index
maintained incrementally as events are written, so it's `O(that agent's own visible
event count)` instead.

### Roster and identity are not part of the language

A `sim` describes game *shape*: how many seats, what roles, what happens each phase.
It doesn't say which LLMs fill those seats — that's a run-time concern. `sociallang run`
loads a roster (`config/models.yaml`, MafiaSim's own format, reused as-is) and assigns
one model per seat (`P1`, `P2`, ... — anonymized seat names, not model names, so the
prompt an agent sees never leaks its own identity or another agent's).

## Memory patterns

Every role names a memory pattern (`memory: <name>(<args>)`), which controls what slice
of the shared event log that role's agents see in their prompt each time they're
`ask()`ed. Three are native (implemented in Python, for speed/correctness of the
underlying math); anything else is looked up among the program's own `memory name(...) { }`
blocks, which run as ordinary SocialLang functions receiving two implicit bindings —
`events` (every event visible to this agent, in order) and `query` (the current prompt
text) — plus whatever parameters the role passed. A custom pattern with the same name as
a native one shadows it.

- **`full_history`** — every visible event, unfiltered. The default when a role's memory
  args are trivial.
- **`recent(n)`** — the last `n` visible events. (Also demonstrated as a from-scratch
  custom pattern in `games/mafia.sl`, to show the native version isn't privileged syntax
  — a user pattern with the same name and same one-line body works identically.)
- **`generative(k)`** — top-`k` retrieval by a recency + importance + relevance score,
  modeled on the memory-stream retrieval in Park et al. 2023, *"Generative Agents:
  Interactive Simulacra of Human Behavior"* (Stanford), https://arxiv.org/abs/2304.03442.
  The paper scores each memory by exponential-decay recency, an LLM-rated 1-10
  importance assigned at write time, and embedding cosine similarity to the current
  query, then retrieves the top-k by weighted sum. This implementation (`sociallang/lang/memory.py`)
  keeps that three-factor shape but swaps the two components that would need extra LLM
  calls or an embedding index for cheap deterministic stand-ins: importance is a lexical
  heuristic (density of game-stakes words + length) instead of an LLM rating, and
  relevance is Jaccard token overlap instead of embedding similarity. Both are the
  obvious place to plug in a real embedding provider and an importance-rating LLM call
  later without changing the retrieval formula.

The paper's other major mechanism, **reflection** — periodically synthesizing
higher-level insights from a batch of recent memories, then storing the insight back
into the memory stream as a new, distinctively important memory — is the `reflect()`
builtin: it retrieves an agent's most important/recent memories (via the same
recency+importance+relevance scoring as `generative`), asks the agent's own model to
state 1-3 conclusions, and writes each one back as a `reflection`-kind event with
importance `0.9`. A script can call `reflect()` on any cadence it likes, but the
paper's actual trigger is `maybe_reflect(agent, threshold=)`
(`Interpreter._bi_maybe_reflect`): it sums the importance of every event visible to
the agent since its `last_reflect_seq` cursor, and only calls `reflect()` (advancing
that cursor) once the sum crosses `threshold`. Reflection events are themselves
written back at importance `0.9`, so they count toward the *next* threshold sum and
get retrieved again by that same recency+importance+relevance scoring the next time
reflection runs — one flat mechanism producing the paper's reflection hierarchy
(reflections synthesized from earlier reflections) without a separate tree
structure to maintain.

### Generative Agents architecture

Memory retrieval and reflection (above) are only half of Park et al. 2023's
architecture. The rest -- persona, planning, reacting, and dialogue generation --
is implemented too, as builtins in `interpreter.py` (and mirrored in
`web/index.html`'s JS port, see below):

- **Persona** — `set_persona(agent, text)` sets `Agent.persona`, a free-text
  backstory folded into every prompt that agent sees (`_identity_for`). The paper's
  agents also carry structured traits/relationships; this implementation keeps it
  to one free-text field a game can fill however it likes (see `games/smallville.sl`'s
  `personas()`), rather than a fixed schema.
- **Planning** — the paper's plans are generated top-down and recursively
  decomposed: a full day sketched in broad strokes, then the current chunk broken
  into hourly actions, then further into 5-15 minute ones. This implementation
  keeps two levels rather than three: `make_plan(agent, goal, steps=)` asks the
  agent's model for a broad-strokes schedule (parsed from `"time: activity"` lines
  into `Agent.plan`, a list of `{time, activity}` steps, also written to memory as a
  `plan`-kind event), and `decompose_step(agent, step=, chunks=)` expands one step
  (by default `current_step(agent)`, i.e. `Agent.plan[Agent.plan_cursor]`) into a
  handful of concrete actions in `Agent.subplan`. Decomposition happens on demand,
  one step at a time, matching the paper's "expand only the next relevant piece"
  approach rather than eagerly expanding an entire day up front.
  `current_action(agent)` reads whichever cursor is live (`subplan_cursor` if a
  subplan exists, else the top-level step's `activity`); `advance_plan(agent)` walks
  `subplan_cursor` forward, falling through to `plan_cursor` once the subplan is
  exhausted (and clearing it, so the next `current_action`/`decompose_step` call
  re-expands the *new* current step rather than reusing a stale one).
- **Reacting** — `react(agent, observation)` is the paper's "should I continue or
  react" decision: given free text describing what the agent just observed (e.g. a
  neighbor showing up), it asks the agent's model to either respond with exactly
  `CONTINUE` or describe a one-sentence new immediate action, and returns `null` in
  the first case, the new action text in the second. It's a single LLM call rather
  than the paper's separate importance-gated trigger step, since SocialLang has no
  standing "world tick" loop of its own for a reaction to interrupt — a game script
  decides when to call `react()` at all (see `smallville.sl`'s `meet_neighbors`).
- **Dialogue generation** — `converse(agent_a, agent_b, topic=, max_turns=)` runs a
  real multi-turn exchange: each turn is a genuine `ask()`-shaped call against that
  turn's speaker (their own model, persona, and memory context via the same
  `_prompt_pieces` helper `ask()`/`ask_choice()` use), alternating speakers, and
  appending each line as a `dialogue`-kind event visible to both participants (so it
  becomes part of each agent's own memory stream, exactly like the paper's
  conversations do). The exchange stops early at a line that looks like a goodbye
  (`_looks_like_farewell` — a small keyword heuristic, not another LLM call) or
  after `max_turns` exchanges either way.

`games/smallville.sl` wires all of this together — four agents with distinct
personas, a spatial `world {}` of homes and shared social spots, a 12-hour "day"
loop that plans, decomposes, moves, reacts, converses, and threshold-reflects each
hour. See its header comment for how each mechanism maps onto the game logic.

`games/smallville_mafia.sl` then demonstrates the point of building it this way:
Mafia as a *different* game layered on top of the same engine, not copy-pasted
into its own flat loop. Every agent — mafia included — gets a persona, plans a day,
lives three "hours" of it (decompose/move/react/converse/reflect, identical to
`smallville.sl`), and only *then* does the hidden-role layer run: a `Night` phase
(mafia secretly gather at a `Den`, vote a kill, `whisper()`ed so the victim never
sees it coming — structurally the same shape as `village.sl`'s own `Night`) and a
`DayVote` phase where the town accuses someone. The thing that's actually
different from `village.sl`/`mafia.sl` is what each vote is grounded in: there,
one scripted `ask()` ("what do you want to say to the group?") stands in for a
day of suspicion; here, `generative(10)` memory retrieval pulls from whatever a
real day of `converse()` transcripts, `react()` decisions, and `maybe_reflect()`
insights actually put in the event log. The Mafia-specific code on top of the
Smallville engine is genuinely thin — one extra phase, one extra vote phase, and
the same team-count `win_condition` `village.sl` already used — which is the
composability this architecture was for.

### Real embeddings and LLM-rated importance

The two paper components originally stood in for by cheap deterministic heuristics
(above) now have real implementations, both opt-in via `sociallang run` flags so
existing runs stay free and deterministic unless asked otherwise:

- **Relevance** (`--embeddings {lexical,hash,openai,google}`, default `hash`) —
  `sociallang/providers/embeddings.py` defines an `EmbeddingProvider` duck-type
  (`embed(texts) -> list[vector | None]`) with three implementations: `lexical`
  keeps the old Jaccard-overlap behavior (no vectors at all); `hash` is a real,
  offline, deterministic bag-of-words feature-hashing embedding (stable md5-hashed
  token buckets, L2-normalized) — a genuine point in a vector space, not token
  overlap, needing no API key, so it's the default; `openai`/`google` call a real
  embedding API (OpenAI's `/embeddings`, Google's `batchEmbedContents`) and fall
  back to `hash` if the matching API key is missing. `memory.retrieve()` takes an
  optional `embedder` and, when given one, swaps `lexical_relevance` for embedding
  cosine similarity — each event's embedding is computed once and cached on the
  `Event` object (`e.embedding`) since `retrieve()` runs on every `ask()` over a
  growing, mostly-unchanged event list.
- **Importance** (`--llm-importance`, `--importance-model <roster key>`) —
  `memory.llm_importance(text, provider)` asks a model (duck-typed to
  `ChatProvider.complete`) to rate an event 1-10, parses the number out of its
  reply, and normalizes to 0..1; it falls back to `heuristic_importance` if no
  provider is configured or the reply has no parseable number. `Interpreter`
  takes an optional `importance_provider` and uses it in `_append_event` instead
  of the heuristic when set.

Both are plugged in the same way the module doc always said they would be: without
changing `retrieve()`'s recency+importance+relevance formula shape.

## What's implemented vs. not

**Implemented:** lexer, parser, tree-walking interpreter; all three memory patterns
above plus fully custom in-language ones; real embedding-based relevance and
LLM-rated importance for `generative(k)` memory (see above); the full Generative
Agents architecture beyond memory/reflection — persona, recursive plan
decomposition, reacting, and dialogue generation (see "Generative Agents
architecture" above); an optional `world { }` block for procedural spatial layout,
spatial builtins, and concurrent bulk-`ask` builtins for scaling a population into
the thousands (see "World and scale" above); `sociallang/cli.py` (`run`, `schema`,
`visualize`, `check`); an HTML replay visualizer (`sociallang/visualize.py`) with a
public/private event timeline and agent roster; JSON schema export
(`sociallang/schema_export.py`) — "export models and model patterns" for external
tooling, i.e. a game's roles and memory patterns as plain JSON without parsing
SocialLang. Seven working example games (`mafia.sl`, `trust_game.sl`, `city.sl`,
`village.sl`, `outbreak.sl`, `smallville.sl`, `smallville_mafia.sl`). 75 tests
covering the lexer, parser, interpreter semantics (including a deterministic
vote-tally/eliminate test, a memory-windowing test, deterministic world
generation, spatial builtins, bulk-ask concurrency/ordering, and the live
WebSocket bridge end to end), the native memory-retrieval math, the embedding
providers, schema export, the Generative Agents builtins
(persona/plan/react/converse/maybe_reflect, `test_generative_agents.py`), an
end-to-end `smallville.sl` run, and an end-to-end `smallville_mafia.sl` run
(confirming the Mafia layer's night-kill/day-vote actually fire on top of a real
day of Smallville-engine agent life).

A live event-streaming bridge (`sociallang/engine/live.py`, `sociallang run --live`)
lets an external viewer watch a run as it happens instead of only reading the final
JSON — see its module docstring for the WebSocket message schema. `unity/` has a
Unity client that consumes it (agents as points on a 2D map, click one for its recent
events) plus a replay mode for a saved JSON file; see `unity/README.md` for setup and
its one honest limitation (replay only has final agent positions, not full movement,
since a saved run doesn't persist a snapshot per round). That client compiles clean
against a stubbed Unity API (see the README) but hasn't been opened in a real Editor
yet — treat "does it actually run" as open until someone does that.

`web/index.html` is a separate, hand-maintained JavaScript port of the lexer/parser/
interpreter/memory-retrieval logic (not the live bridge or Unity's renderer) — a
self-contained browser IDE with all seven example games embedded (Generative Agents
builtins included — persona/plan/react/converse/maybe_reflect all have JS
equivalents), a mock LLM provider standing in for real ones (a public page can't
hold API keys), and a tutorial panel. It's a full reimplementation, not a thin
wrapper around the Python code, so a future language change needs to be ported
there by hand too; see `web/README.md`.

**Not implemented / open questions:**

- No static type checking or line-number-aware error recovery beyond "first parse error
  stops the whole file" — fine for a small language authored by one person, would need
  work for a wider audience.
- No persistent numeric resources beyond ad-hoc `let` variables (e.g. an economy/currency
  system spanning rounds) — would currently have to be hand-rolled per game via
  dict/list state threaded through `loop`.
- `visualize`'s HTML is intentionally plain (no charting, no filtering UI) — a fine
  target for the `dataviz`/`artifact-design` treatment later if this needs to be shown
  to someone rather than just read as a debug trace.
- World generation is a single scatter pass (no terrain, no roads/connectivity, no
  region-level structure) — fine for "thousands of agents need somewhere to be," not a
  general procedural-map generator.
- `ask_all`/`ask_choice_all` parallelize within one call, but a phase that calls `ask()`
  in a loop instead (the old per-agent style, still fully supported) still serializes —
  scaling a game's LLM tier requires actually using the bulk builtins.
- `nearby(agent, radius)` is a plain O(agents) linear scan, not a spatial index — fine
  at the scale the shipped example games use it at (outbreak.sl's 620 agents, tens of
  milliseconds), but a game calling it every round over a thousands-of-agents
  population doing frequent proximity queries would want real grid-bucketed spatial
  indexing instead.
- `location { capacity: n }` is parsed, stored, and threaded through the interpreter,
  the saved JSON, and the live bridge's world message, but nothing currently *enforces*
  it — `spawn_agents_at`/`move_to` will place any number of agents at a location
  regardless of its declared capacity. It's available as plain data a game can read
  itself (`count(agents_at(loc)) < loc.capacity`) and act on, not an automatic
  constraint the builtins apply for you.
- `react()`/`converse()` are single-call simplifications of the paper's fuller
  mechanisms: the paper gates reacting behind its own importance-scored triggering
  step before deciding whether to interrupt, and its dialogue continuation is
  itself retrieval-augmented per turn (each reply retrieves relevant memories
  fresh) rather than one open-ended exchange. Here, a game script decides *when*
  to call `react()` at all (see `smallville.sl`'s `meet_neighbors`), and each
  `converse()` turn already goes through the normal memory-retrieval path via
  `_prompt_pieces`/`buildContextFor` — just without a dedicated "what's relevant to
  this specific reply" re-query on top of that.
