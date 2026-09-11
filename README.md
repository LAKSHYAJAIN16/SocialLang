# SocialLang

SocialLang is a real programming language — its own lexer, parser, and tree-walking
interpreter, not a config schema — for defining social games and running them with
LLM agents. A `.sl` file declares a population, the roles they can hold, the phases
the game moves through, and the rules of each phase as actual code: variables, loops,
conditionals, functions. There's no host language required to add a new game; write a
`.sl` file and run it.

It can also describe a *spatial* world (a `world { }` block that procedurally
generates locations) and scale a population into the thousands, and a run can be
watched live in a Unity viewer instead of only read back as JSON afterward.

Built on [MafiaSim](https://github.com/LAKSHYAJAIN16/LLM-mafia)'s LLM provider layer.

## A complete example

This is `games/trust_game.sl` verbatim — no hidden roles, just two agents repeatedly
choosing to cooperate or defect:

```
sim TrustGame {
  agents: 2

  role Player {
    team: "player"
    memory: full_history
    sees: none
    count: remainder
  }

  fn other(a, players) {
    for p in players {
      if p.seat != a.seat {
        return p
      }
    }
    return null
  }

  phase Round {
    let players = alive()
    let choices = {}
    for p in players {
      choices[p] = ask_choice(p, "Cooperate or defect this round?", ["cooperate", "defect"])
    }
    for p in players {
      let opponent = other(p, players)
      let mine = choices[p]
      let theirs = choices[opponent]
      if mine != theirs {
        broadcast(p.seat + " played " + mine + " while " + opponent.seat + " played " + theirs + ".")
      } else {
        broadcast(p.seat + " and " + opponent.seat + " both played " + mine + ".")
      }
    }
  }

  win_condition {
    if round > 20 {
      return "complete"
    }
  }

  loop {
    run Round
    let winner = check_win()
    if winner != null {
      return winner
    }
  }
}
```

`python -m sociallang.cli run games/trust_game.sl --mock-only` parses, populates,
and plays this to completion, with no Python code written for this specific game.

## How the language works

A program is exactly one `sim <Name> { ... }` block containing, in any order:

- **`agents: <n>`** or **`agents: <lo>..<hi>`** — population size, fixed or a range
  redrawn each run.
- **`role <Name> { team: "..", memory: <pattern>(<args>), sees: teammates|none, count: <n>|remainder }`**
  — a kind of seat an agent can fill. `sees: teammates` tells an agent who else shares
  its role (e.g. Mafia knowing their fellow Mafia); `memory` picks what slice of the
  event log that role's agents see when prompted (below). At most one role may use
  `count: remainder` — it absorbs whatever's left of the drawn population.
- **`world { width: <n>, height: <n>, location <Type> { tag: "..", capacity: <n>, count: <n> } ... }`**
  — optional. Declares *kinds* of places and how many of each should exist; the
  interpreter procedurally scatters that many concrete instances across the grid at
  sim start (seeded, so it's reproducible under `--seed`). Agents get `x`/`y`/`location`
  fields once placed via `spawn_agents_at`/`move_to`. This is how a game gets a spatial
  layout without anyone hand-placing coordinates — see `games/village.sl` and
  `games/city.sl`.
- **`memory <name>(<params>) { <statements> }`** — a custom memory pattern, itself
  written in SocialLang (see below).
- **`fn <name>(<params>) { <statements> }`** — an ordinary function.
- **`phase <name> { <statements> }`** — a named block of game logic, invoked with `run <name>`.
- **`win_condition { <statements> }`** — evaluated on demand via the `check_win()`
  builtin. `return <value>` reports a winner; falling off the end (or bare `return`)
  means "no winner yet."
- **`loop { <statements> }`** — the simulation's main loop, re-executed once per round
  (up to a safety cap passed as `--max-rounds`). `run <phase>` invokes a phase inline;
  `return <value>` anywhere in the loop (or a phase it calls) ends the sim with that
  value as the winner.

Inside any block: `let`, plain assignment (`x = ..`, `d[k] = ..`), `if`/`else if`/`else`,
`while`, `for x in <expr>`, `return`, `break`, `run <phase>`, and bare expression
calls. Expressions cover numbers, strings, `true`/`false`/`null`, list `[...]` and
dict `{k: v}` literals, `and`/`or`/`not`, comparisons, arithmetic, `.attr` access,
`[index]`, and function calls with positional and `name=value` keyword arguments.

### Roster and identity aren't part of the language

A `sim` describes game *shape* — how many seats, what roles, what happens each phase.
It doesn't say which LLMs fill those seats; that's a run-time concern. `sociallang run`
loads a roster (`config/models.yaml`) and assigns one model per seat (`P1`, `P2`, ...
— anonymized seat names, so the prompt an agent sees never leaks its own identity or
another agent's).

### Memory patterns

Every role names a memory pattern, controlling what slice of the shared event log
that role's agents see each time they're prompted. Three are native; anything else is
looked up among the program's own `memory name(...) { }` blocks, which run as ordinary
SocialLang functions receiving `events` (everything visible to this agent) and `query`
(the current prompt text), plus whatever params the role passed.

| Pattern | Behavior |
|---|---|
| `full_history` | Every visible event, unfiltered. |
| `recent(n)` | The last `n` visible events. |
| `generative(k)` | Top-`k` retrieval by recency + importance + relevance, modeled on the memory-stream retrieval in Park et al. 2023, *"Generative Agents: Interactive Simulacra of Human Behavior."* Real embedding-based relevance and LLM-rated importance are available via `sociallang run --embeddings` / `--llm-importance`. |

`reflect(agent)` (below) is the paper's other major mechanism: periodically
synthesizing higher-level insights from an agent's memory and writing them back as
new, high-importance memories.

### Built-in functions

| Function | Does |
|---|---|
| `ask(agent, prompt, temperature=, max_tokens=)` | Calls the agent's model, returns its raw text reply |
| `ask_choice(agent, prompt, options)` | Same, constrained to match one item of `options` |
| `ask_all(agents, prompt, ...)` / `ask_choice_all(agents, prompt, options)` | Bulk versions — fire every agent's LLM call concurrently instead of serially, returning `{agent: result}`. This is how a game's LLM-driven tier scales past a handful of agents. |
| `broadcast(text)` / `broadcast(agent, text)` | Publishes an event every agent can see |
| `whisper(agents, text)` | Publishes an event only the given agents can see |
| `remember(agent, text)` | Adds a private note to one agent's own memory |
| `reflect(agent)` | Synthesizes 1-3 insights from an agent's retrieved memory and stores them back as high-importance memories |
| `alive()` / `all_agents()` | Currently-alive agents / every agent regardless of status |
| `with_role(name)` | Agents (alive or not) holding a role |
| `team_of(agent)` | That agent's role's team string |
| `eliminate(agent, cause=)` | Marks an agent dead and broadcasts it |
| `tally(votes)` | Given a dict of agent → target, the majority target (ties broken randomly) |
| `locations()` / `locations_by_tag(tag)` | Procedurally-placed `Location`s from `world {}`, optionally filtered |
| `spawn_agents_at(agents, tag=)` / `move_to(agent, loc)` | Place or move agents spatially |
| `location_of(agent)` / `agents_at(loc)` / `nearby(agent, radius)` | Spatial queries |
| `count(x)`, `last(list, n)`, `random_choice(list)`, `str(x)`, `print(x)` | Utility |
| `check_win()` | Runs `win_condition` once, returns its result (or `null`) |

### Scaling a population without new syntax

There's no special "scripted vs. LLM" syntax — `role { count: 5000 }` and ordinary
function calls already suffice. `games/city.sl` and `games/outbreak.sl` show the
pattern: most agents are moved/updated each round by a plain `fn` with zero LLM
calls, while a small role is asked something every round via `ask_choice_all`. All
5,050 agents in `city.sl` run a full game in about a second.

Full language reference, grammar details, and design rationale (including what isn't
implemented yet) live in [DESIGN.md](DESIGN.md).

## Setup

```
pip install -r requirements.txt -r requirements-dev.txt
cp .env.example .env   # fill in your API keys
```

## Running a game

```
python -m sociallang.cli check games/mafia.sl      # parse and report its shape
python -m sociallang.cli run games/mafia.sl --mock-only
python -m sociallang.cli schema games/mafia.sl      # export roles/memory patterns as JSON
python -m sociallang.cli visualize results/Mafia_0000_....json   # regenerate the HTML replay
```

`--mock-only` runs against a random-but-legal mock model, no API key needed. Drop it
(with a filled-in `.env` and `config/models.yaml`) to play with real LLMs. Useful
`run` flags: `--games N` (independent runs), `--seed N` (reproducible), `--vendor`
(restrict the roster), `--embeddings`/`--llm-importance` (real relevance/importance
for `generative(k)` memory — see above).

## Watching a run live (Unity)

```
python -m sociallang.cli run games/village.sl --mock-only --live
```

streams every event over a local WebSocket bridge (`sociallang/engine/live.py`)
instead of only writing JSON/HTML at the end. `unity/SocialLangViewer` is a Unity
project that connects to it — agents render as points on a 2D map, locations from
`world {}` render as markers, click an agent to see its recent events — or replays a
saved `results/*.json` file. See [unity/README.md](unity/README.md) for setup and
current status.

## Example games

| Game | Agents | What it demonstrates |
|---|---|---|
| `games/mafia.sl` | 6-10 | Hidden roles, night/day phases, custom memory pattern |
| `games/trust_game.sl` | 2 | No hidden roles — just repeated cooperate/defect rounds |
| `games/village.sl` | 6-10 | Hidden roles combined with a `world {}` — location gates who hears wolves' night chatter |
| `games/city.sl` | 5,050 | Large-population + spatial wandering + a concurrent LLM tier (`ask_choice_all`) |
| `games/outbreak.sl` | 620 | Spread driven by `nearby()`/`eliminate()`, with an LLM tier's policy vote measurably changing the outcome |

## Tests

```
python -m pytest
```
