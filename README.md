# SocialLang

> A domain-specific language for LLM-agent social games -- Mafia, trust games, whatever -- so you never rewrite the scaffolding again.

I kept wanting to try new social games with LLM agents and kept rewriting the same Python scaffolding every time, so I built an actual language for it: its own lexer, parser, and tree-walking interpreter. A `.sl` file declares a population, the roles they hold, the phases the game moves through, and each phase's rules as real code -- variables, loops, conditionals, functions. No host language needed for a new game, just a `.sl` file. It also handles spatial worlds, scales to thousands of agents, and can stream a run live into a Unity viewer. Built on [MafiaSim](https://github.com/LAKSHYAJAIN16/LLM-mafia)'s LLM provider layer.

## A complete example

`games/trust_game.sl`, verbatim -- two agents repeatedly choosing to cooperate or defect, no hidden roles:

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
      broadcast(p.seat + " played " + choices[p] + ", " + opponent.seat + " played " + choices[opponent] + ".")
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

`python -m sociallang.cli run games/trust_game.sl --mock-only` parses, populates, and plays this to completion.

## Language basics

A program is one `sim <Name> { ... }` block:
- `agents: <n>` or `<lo>..<hi>` -- population size
- `role <Name> { team, memory: <pattern>, sees: teammates|none, count: <n>|remainder }` -- a seat kind; at most one role can use `count: remainder`
- `world { width, height, location <Type> { tag, capacity, count } ... }` -- optional, procedurally scatters that many locations across a grid (seeded, reproducible under `--seed`); agents get `x`/`y`/`location` via `spawn_agents_at`/`move_to`
- `memory <name>(<params>) { ... }` -- a custom memory pattern, itself SocialLang
- `fn`, `phase`, `win_condition { ... }` (checked via `check_win()`), `loop { ... }` (the main loop, `run <phase>` invokes a phase)

Statements: `let`, assignment, `if`/`else if`/`else`, `while`, `for x in <expr>`, `return`, `break`, `run <phase>`. Expressions: numbers, strings, booleans, lists/dicts, `and`/`or`/`not`, comparisons, arithmetic, `.attr`, `[index]`, positional/keyword calls.

Roster assignment lives outside the language on purpose -- a `sim` describes shape, not which model fills which seat. `sociallang run` loads `config/models.yaml` and assigns one model per anonymized seat (`P1`, `P2`, ...).

### Memory patterns

| Pattern | Behavior |
|---|---|
| `full_history` | Every visible event, unfiltered |
| `recent(n)` | Last `n` visible events |
| `generative(k)` | Top-`k` by recency + importance + relevance (Park et al. 2023 memory-stream retrieval); real embeddings/LLM-importance via `--embeddings`/`--llm-importance` |

`reflect(agent)` synthesizes higher-level insights from memory and writes them back as high-importance memories; `maybe_reflect(agent, threshold=)` fires only once accumulated importance crosses `threshold`.

### Generative Agents, implemented on top of this

`games/smallville.sl` runs the full Park et al. 2023 architecture (persona, planning, reacting, dialogue, reflection); `games/smallville_mafia.sl` layers Mafia's hidden roles and votes on top of the same living town. See [DESIGN.md](DESIGN.md) for the paper mapping.

| Mechanism | Builtin(s) |
|---|---|
| Persona | `set_persona(agent, text)` |
| Planning | `make_plan(agent, goal, steps=)`, `decompose_step(agent, step=, chunks=)`, `current_step`/`current_action`/`advance_plan` |
| Reacting | `react(agent, observation)` -- `null` to continue, or a new action |
| Dialogue | `converse(agent_a, agent_b, topic=, max_turns=)` -- real multi-turn exchange between the two agents' own models |

### Built-in functions

| Function | Does |
|---|---|
| `ask(agent, prompt, ...)` / `ask_choice(agent, prompt, options)` | Calls the agent's model, free text or constrained to one option |
| `ask_all(...)` / `ask_choice_all(...)` | Bulk versions, fired concurrently -- how a game scales past a handful of agents |
| `broadcast(text)` / `whisper(agents, text)` | Publish an event to everyone / to just the given agents |
| `remember(agent, text)` | Adds a private memory |
| `alive()` / `all_agents()` / `with_role(name)` / `team_of(agent)` | Query agents |
| `eliminate(agent, cause=)` | Marks an agent dead and broadcasts it |
| `tally(votes)` | Majority target from a dict of agent to target |
| `locations()` / `locations_by_tag(tag)` / `spawn_agents_at()` / `move_to()` / `location_of()` / `agents_at()` / `nearby()` | Spatial world queries and placement |
| `count`, `last`, `random_choice`, `str`, `print` | Utility |
| `check_win()` | Runs `win_condition`, returns its result or `null` |

### Scaling without new syntax

No special "scripted vs. LLM" mode -- `role { count: 5000 }` plus ordinary `fn` calls handles it. `games/city.sl` and `games/outbreak.sl` move most agents with plain functions and only ask a small role something via `ask_choice_all` each round; all 5,050 agents in `city.sl` run a full game in about a second.

Full grammar and design rationale: [DESIGN.md](DESIGN.md).

## Other ways to run it

- **Browser, no install**: `web/index.html` is a self-contained IDE (lexer/parser/interpreter ported to JS), runs all seven example games against a mock provider client-side. See [web/README.md](web/README.md).
- **Visual sandbox**: `sandbox/` renders a run as a night-sky chart -- agents flare with activity, conversations draw fading lines, reflections become new stars. Ships as a website (`npm run dev`) or Electron app (`npm run electron:dist`); reuses the same JS engine as the web IDE. See [sandbox/README.md](sandbox/README.md).
- **Unity, live**: `python -m sociallang.cli run games/village.sl --mock-only --live` streams events over a local WebSocket (`sociallang/engine/live.py`); `unity/SocialLangViewer` renders agents/locations on a 2D map or replays a saved `results/*.json`. See [unity/README.md](unity/README.md).

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

`--mock-only` needs no API key. Drop it (with `.env` + `config/models.yaml` filled in) to play with real LLMs. Useful `run` flags: `--games N`, `--seed N`, `--vendor`, `--embeddings`/`--llm-importance`.

## Example games

| Game | Agents | What it demonstrates |
|---|---|---|
| `games/mafia.sl` | 6-10 | Hidden roles, night/day phases, custom memory pattern |
| `games/trust_game.sl` | 2 | No hidden roles -- repeated cooperate/defect |
| `games/village.sl` | 6-10 | Hidden roles + `world {}` -- location gates who hears wolves' night chatter |
| `games/city.sl` | 5,050 | Large-population + spatial wandering + concurrent LLM tier |
| `games/outbreak.sl` | 620 | Spread via `nearby()`/`eliminate()`, LLM policy vote changes the outcome |
| `games/smallville.sl` | 4 | Full Generative Agents loop |
| `games/smallville_mafia.sl` | 6-8 | Mafia layered on the Smallville engine, suspicion grounded in real dialogue |

## Tests

```
python -m pytest
```
