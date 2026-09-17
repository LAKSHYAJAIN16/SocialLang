# SocialLang

I built SocialLang because I kept wanting to try new social games with LLM agents (Mafia, trust games, whatever) and kept rewriting the same Python scaffolding every time. So instead I built an actual language for it — its own lexer, parser, and tree-walking interpreter, not just a config schema. A `.sl` file declares a population, the roles they can hold, the phases the game moves through, and the rules of each phase as real code: variables, loops, conditionals, functions. No host language needed to add a new game — just write a `.sl` file and run it.

It can also describe a *spatial* world (a `world { }` block that procedurally generates locations) and scale a population into the thousands, and you can watch a run live in a Unity viewer instead of only reading it back as JSON afterward.

It's built on top of [MafiaSim](https://github.com/LAKSHYAJAIN16/LLM-mafia)'s LLM provider layer, so it inherits that project's multi-vendor model roster.

## A complete example

This is `games/trust_game.sl` verbatim — no hidden roles, just two agents repeatedly choosing to cooperate or defect:

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

`python -m sociallang.cli run games/trust_game.sl --mock-only` parses, populates, and plays this to completion — no Python written for this specific game.

## How the language actually works

A program is one `sim <Name> { ... }` block, containing (in any order):

- **`agents: <n>`** or **`agents: <lo>..<hi>`** — population size, fixed or a range redrawn each run.
- **`role <Name> { team: "..", memory: <pattern>(<args>), sees: teammates|none, count: <n>|remainder }`** — a kind of seat an agent can fill. `sees: teammates` tells an agent who else shares its role (Mafia knowing their fellow Mafia, say); `memory` picks what slice of the event log that role sees when prompted. At most one role can use `count: remainder` — it soaks up whatever's left of the population.
- **`world { width: <n>, height: <n>, location <Type> { tag: "..", capacity: <n>, count: <n> } ... }`** — optional. Declares kinds of places and how many, and the interpreter scatters that many concrete instances across the grid at sim start (seeded, so it's reproducible under `--seed`). Agents get `x`/`y`/`location` once placed via `spawn_agents_at`/`move_to`. This is how a game gets spatial layout without anyone hand-placing coordinates — see `games/village.sl` and `games/city.sl`.
- **`memory <name>(<params>) { <statements> }`** — a custom memory pattern, itself written in SocialLang.
- **`fn <name>(<params>) { <statements> }`** — an ordinary function.
- **`phase <name> { <statements> }`** — a named block of game logic, invoked with `run <name>`.
- **`win_condition { <statements> }`** — evaluated on demand via `check_win()`. `return <value>` reports a winner; falling off the end (or bare `return`) means no winner yet.
- **`loop { <statements> }`** — the main loop, re-run once per round (up to a safety cap via `--max-rounds`). `run <phase>` invokes a phase inline; `return <value>` anywhere in the loop or a phase it calls ends the sim with that winner.

Inside any block you get `let`, plain assignment, `if`/`else if`/`else`, `while`, `for x in <expr>`, `return`, `break`, `run <phase>`, and bare expression calls. Expressions cover numbers, strings, `true`/`false`/`null`, list and dict literals, `and`/`or`/`not`, comparisons, arithmetic, `.attr` access, `[index]`, and function calls with positional or keyword args.

### Roster and identity live outside the language on purpose

A `sim` describes the game's *shape* — seats, roles, what happens each phase. It doesn't say which LLMs fill those seats; that's a run-time decision. `sociallang run` loads a roster (`config/models.yaml`) and assigns one model per seat (`P1`, `P2`, ... anonymized, so nothing in a prompt leaks an agent's own identity or anyone else's).

### Memory patterns

Every role names a memory pattern that controls what slice of the shared event log its agents see each time they're prompted. Three are native; anything else gets looked up among the program's own `memory name(...) { }` blocks, which run as ordinary SocialLang functions receiving `events` (everything visible to this agent), `query` (the current prompt text), and whatever params the role passed.

| Pattern | Behavior |
|---|---|
| `full_history` | Every visible event, unfiltered. |
| `recent(n)` | The last `n` visible events. |
| `generative(k)` | Top-`k` retrieval by recency + importance + relevance, modeled on the memory-stream retrieval in Park et al. 2023, *"Generative Agents: Interactive Simulacra of Human Behavior."* Real embedding-based relevance and LLM-rated importance are available via `sociallang run --embeddings` / `--llm-importance`. |

`reflect(agent)` is the paper's other big mechanism — periodically synthesizing higher-level insights from an agent's memory and writing them back as new, high-importance memories. `maybe_reflect(agent, threshold=)` is the paper's actual trigger for that: it fires only once the summed importance of an agent's memories since its last reflection crosses `threshold`, instead of some script picking a fixed cadence.

### I basically implemented the Generative Agents paper on top of this

Beyond memory retrieval and reflection, the rest of Park et al. 2023's architecture — persona, planning, reacting, dialogue generation — is in here too. `games/smallville.sl` runs that whole loop end to end; `games/smallville_mafia.sl` then builds a genuinely different game on top of it, layering Mafia's hidden roles and elimination votes over the same living town, so a day's suspicion comes from actual lived dialogue instead of one scripted prompt. See both files' comments and [DESIGN.md](DESIGN.md) for how each piece maps to the paper and where it still cuts corners.

| Mechanism | Builtin(s) |
|---|---|
| Persona | `set_persona(agent, text)` folds a backstory into every prompt that agent sees. |
| Planning | `make_plan(agent, goal, steps=)` sketches a broad-strokes schedule and stores it on the agent; `decompose_step(agent, step=, chunks=)` recursively expands the *current* step into concrete actions, on demand rather than the whole day up front; `current_step`/`current_action`/`advance_plan` read and walk the resulting plan. |
| Reacting | `react(agent, observation)` asks whether an observation (a neighbor showing up, say) interrupts the current plan — returns `null` to continue, or a new one-sentence action. |
| Dialogue | `converse(agent_a, agent_b, topic=, max_turns=)` runs a real multi-turn exchange between the two agents' own models (each a genuine `ask()`-style call with that agent's own persona and memory), stopping at a goodbye-shaped line or `max_turns`. |

### Built-in functions

| Function | Does |
|---|---|
| `ask(agent, prompt, temperature=, max_tokens=)` | Calls the agent's model, returns its raw text reply |
| `ask_choice(agent, prompt, options)` | Same, constrained to one item of `options` |
| `ask_all(agents, prompt, ...)` / `ask_choice_all(agents, prompt, options)` | Bulk versions — fire every agent's LLM call concurrently instead of serially, returning `{agent: result}`. This is how a game's LLM-driven tier scales past a handful of agents. |
| `broadcast(text)` / `broadcast(agent, text)` | Publishes an event every agent can see |
| `whisper(agents, text)` | Publishes an event only the given agents can see |
| `remember(agent, text)` | Adds a private note to one agent's own memory |
| `reflect(agent)` / `maybe_reflect(agent, threshold=)` | Synthesizes 1-3 insights from an agent's retrieved memory and stores them back as high-importance memories; `maybe_reflect` only does so once accumulated importance crosses `threshold` |
| `set_persona(agent, text)` | Sets a free-text backstory folded into every prompt that agent sees |
| `make_plan(agent, goal, steps=)` | Sketches a broad-strokes daily plan and stores it on the agent |
| `current_step(agent)` / `decompose_step(agent, step=, chunks=)` / `current_action(agent)` / `advance_plan(agent)` | Read, recursively expand, and walk an agent's plan |
| `react(agent, observation)` | Decides whether an observation interrupts the current plan (`null`) or replaces it (returned text) |
| `converse(agent_a, agent_b, topic=, max_turns=)` | Runs a generated multi-turn dialogue between two agents, returns the transcript |
| `alive()` / `all_agents()` | Currently-alive agents / every agent regardless of status |
| `with_role(name)` | Agents (alive or not) holding a role |
| `team_of(agent)` | That agent's role's team string |
| `eliminate(agent, cause=)` | Marks an agent dead and broadcasts it |
| `tally(votes)` | Given a dict of agent to target, the majority target (ties broken randomly) |
| `locations()` / `locations_by_tag(tag)` | Procedurally-placed `Location`s from `world {}`, optionally filtered |
| `spawn_agents_at(agents, tag=)` / `move_to(agent, loc)` | Place or move agents spatially |
| `location_of(agent)` / `agents_at(loc)` / `nearby(agent, radius)` | Spatial queries |
| `count(x)`, `last(list, n)`, `random_choice(list)`, `str(x)`, `print(x)` | Utility |
| `check_win()` | Runs `win_condition` once, returns its result (or `null`) |

### Scaling a population without new syntax

There's no special "scripted vs. LLM" syntax — `role { count: 5000 }` and ordinary function calls already handle it. `games/city.sl` and `games/outbreak.sl` show the pattern: most agents get moved/updated each round by a plain `fn` with zero LLM calls, while a small role gets asked something every round via `ask_choice_all`. All 5,050 agents in `city.sl` run a full game in about a second.

Full language reference, grammar, and design rationale (including what I haven't gotten to yet) live in [DESIGN.md](DESIGN.md).

## Try it in the browser, no install

`web/index.html` is a self-contained IDE — I ported the lexer, parser, and interpreter to JavaScript so it runs entirely client-side. Open it directly (or `python -m http.server 8000 --directory web`), pick from all seven example games, and run them against a mock LLM provider. See [web/README.md](web/README.md) for what's different from the real Python implementation (no real API calls, no concurrency, no spatial rendering — that's what the CLI and Unity viewer below are for).

## Watch a simulation happen, not just read its log

`sandbox/` is a dedicated visual simulator, not another code IDE — pick a game (`smallville_mafia.sl`/`smallville.sl` lead the picker) and watch it happen on a night-sky chart: agents render as points of light that flare with recent activity, a conversation draws a fading line between the two agents involved, and a reflection surfaces as a newly cataloged star. It reuses the exact JS engine `web/index.html` ships (see `sandbox/scripts/sync-engine.mjs`), so it's not a separate reimplementation. Ships as both a website (`npm run dev` inside `sandbox/`) and a desktop app via Electron (`npm run electron:dist`). See [sandbox/README.md](sandbox/README.md) and this repo's `PRODUCT.md` for the full brief.

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

`--mock-only` runs against a random-but-legal mock model, no API key needed. Drop it (with a filled-in `.env` and `config/models.yaml`) to play with real LLMs. Useful `run` flags: `--games N` (independent runs), `--seed N` (reproducible), `--vendor` (restrict the roster), `--embeddings`/`--llm-importance` (real relevance/importance for `generative(k)` memory).

## Watching a run live in Unity

```
python -m sociallang.cli run games/village.sl --mock-only --live
```

This streams every event over a local WebSocket bridge (`sociallang/engine/live.py`) instead of only writing JSON/HTML at the end. `unity/SocialLangViewer` is a Unity project that connects to it — agents render as points on a 2D map, locations from `world {}` render as markers, click an agent to see its recent events — or it can replay a saved `results/*.json` file instead. See [unity/README.md](unity/README.md) for setup and current status.

## Example games

| Game | Agents | What it demonstrates |
|---|---|---|
| `games/mafia.sl` | 6-10 | Hidden roles, night/day phases, custom memory pattern |
| `games/trust_game.sl` | 2 | No hidden roles — just repeated cooperate/defect rounds |
| `games/village.sl` | 6-10 | Hidden roles combined with a `world {}` — location gates who hears wolves' night chatter |
| `games/city.sl` | 5,050 | Large-population + spatial wandering + a concurrent LLM tier (`ask_choice_all`) |
| `games/outbreak.sl` | 620 | Spread driven by `nearby()`/`eliminate()`, with an LLM tier's policy vote measurably changing the outcome |
| `games/smallville.sl` | 4 | The full Generative Agents loop: persona, recursive planning, reacting, dialogue, threshold-triggered reflection |
| `games/smallville_mafia.sl` | 6-8 | Mafia as a ruleset layered on top of the Smallville engine — hidden roles vote using suspicion grounded in a whole day's real dialogue, not a single scripted prompt |

## Tests

```
python -m pytest
```
