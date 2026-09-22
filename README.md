# SocialLang

> A domain-specific language for LLM-agent social games -- Mafia, trust games, whatever -- so you never rewrite the scaffolding again.

It's a tree-walking interpreter for a small language whose whole job is describing LLM-agent social games -- Mafia, trust games, whatever comes next -- without rewriting Python scaffolding for each one. A `.sl` file declares a population, the roles they hold, the phases the game moves through, and each phase's rules as real code: variables, loops, conditionals, functions. No host language needed for a new game, just a `.sl` file. It also handles spatial worlds, scales to thousands of agents, and can stream a run live into a Unity viewer. Built on [MafiaSim](https://github.com/LAKSHYAJAIN16/LLM-mafia)'s LLM provider layer.

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

A program is one `sim <Name> { ... }` block: `agents` sets the population, `role` blocks define seat kinds (team, memory pattern, visibility, how many), an optional `world` block scatters spatial locations, and `phase`/`fn`/`win_condition`/`loop` are the actual game logic in ordinary imperative syntax (`if`, `while`, `for`, functions, lists, dicts). Roster assignment lives outside the language on purpose -- a `sim` describes shape, not which model fills which seat; `sociallang run` loads `config/models.yaml` and assigns one model per anonymized seat (`P1`, `P2`, ...).

Three memory patterns ship built in (`full_history`, `recent(n)`, and `generative(k)` -- Park et al. 2023 memory-stream retrieval), plus `reflect`/`maybe_reflect` for synthesizing higher-level insights back into memory. Built-in functions cover asking agents things (`ask`, `ask_choice`, and bulk `ask_all`/`ask_choice_all` for scale), publishing events (`broadcast`, `whisper`), querying and moving agents (`alive`, `eliminate`, spatial placement), and running the win condition. `games/smallville.sl` implements the full Park et al. 2023 Generative Agents architecture (persona, planning, reacting, dialogue, reflection) on top of the same builtins, and `games/smallville_mafia.sl` layers Mafia on top of that. Scaling to thousands of agents needs no special syntax -- `role { count: 5000 }` plus ordinary `fn` calls does it; `games/city.sl` runs a full game with 5,050 agents in about a second.

Full grammar, the complete built-in function reference, and design rationale live in [DESIGN.md](DESIGN.md).

## Other ways to run it

- **Browser, no install**: `web/index.html` is a self-contained IDE (lexer/parser/interpreter ported to JS), runs all seven example games against a mock provider client-side. See [web/README.md](web/README.md).
- **SocialSandbox**: `sandbox/` renders a run as a night-sky chart -- agents flare with activity, conversations draw fading lines, reflections become new stars. Ships as a website (`npm run dev`) or Electron app (`npm run electron:dist`); reuses the same JS engine as the web IDE. See [sandbox/README.md](sandbox/README.md).
- **SocialSandbox (native, `cpp/`)**: a C++ rewrite of the sandbox laid out like the Unity editor (Hierarchy, Scene, Inspector, Project, Console; Play/Pause/Step), with a native interpreter up to ~100x faster than the JS one on the mock, CPU-parallel bulk asks, and real models via your own API keys or a local LLM (Ollama, LM Studio). See [cpp/README.md](cpp/README.md).
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
