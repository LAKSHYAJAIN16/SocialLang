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

The full worked examples are `games/mafia.sl` (a complete Mafia implementation) and
`games/trust_game.sl` (a structurally different game — no hidden roles, no elimination,
just repeated cooperate/defect rounds with a round-count win condition) — proof the
language isn't accidentally Mafia-shaped.

### Grammar

Top-level: a program is exactly one `sim <Name> { ... }` block containing, in any order:

- `agents: <n>` or `agents: <lo>..<hi>` — population size (fixed or a range, redrawn each run)
- `role <Name> { team: "..", memory: <pattern>(<args>), sees: teammates|none, count: <n>|remainder }`
  — at most one role may use `count: remainder`
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
function calls with positional and `name=value` keyword arguments. `+` concatenates two
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
| `with_role(name)` | List of agents (alive or not) holding that role |
| `team_of(agent)` | That agent's role's team string |
| `eliminate(agent, cause=)` | Marks an agent dead and broadcasts it |
| `tally(votes)` | Given a dict of agent → target, returns the majority target (ties broken randomly) |
| `count(x)`, `last(list, n)`, `random_choice(list)`, `str(x)`, `print(x)` | Utility |
| `check_win()` | Runs `win_condition` once, returns its result (or `null`) |

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
importance `0.9`. A script decides when to call it (e.g. once per day-phase) — this is a
single-level simplification of the paper's hierarchical reflection tree, not a full
port.

## What's implemented vs. not

**Implemented:** lexer, parser, tree-walking interpreter; all three memory patterns
above plus fully custom in-language ones; `sociallang/cli.py` (`run`, `schema`,
`visualize`, `check`); an HTML replay visualizer (`sociallang/visualize.py`) with a
public/private event timeline and agent roster; JSON schema export
(`sociallang/schema_export.py`) — "export models and model patterns" for external
tooling, i.e. a game's roles and memory patterns as plain JSON without parsing
SocialLang. Two working example games. 28 tests covering the lexer, parser, interpreter
semantics (including a deterministic vote-tally/eliminate test and a memory-windowing
test), the native memory-retrieval math, and schema export.

**Not implemented / open questions:**

- Real embeddings for `generative`'s relevance term (currently lexical overlap) and
  LLM-rated importance (currently heuristic) — flagged above as the natural upgrade path.
- No static type checking or line-number-aware error recovery beyond "first parse error
  stops the whole file" — fine for a small language authored by one person, would need
  work for a wider audience.
- No persistent numeric resources beyond ad-hoc `let` variables (e.g. an economy/currency
  system spanning rounds) — would currently have to be hand-rolled per game via
  dict/list state threaded through `loop`.
- `visualize`'s HTML is intentionally plain (no charting, no filtering UI) — a fine
  target for the `dataviz`/`artifact-design` treatment later if this needs to be shown
  to someone rather than just read as a debug trace.
