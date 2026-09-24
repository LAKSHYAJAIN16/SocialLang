# SocialLang

> Simulate a town of LLM agents -- and whatever they get up to -- from two small files.

Everything runs inside one setting: **smallville**, a town of residents who
wake up, plan their day, walk to work, run into each other, talk, remember,
pass news along, and reflect. A town is a folder, `games/NAME/`, holding two
`.sl` files, and anything else you want to happen is played out inside that town.

| File in `games/NAME/` | What it says |
|---|---|
| `NAME.env.sl` (environment) | The world: building types, buildings, residents, relationships, events and news, and how many extra residents to generate. |
| `NAME.behavior.sl` (behavior) | How residents behave: social rules, daily routines, meals and free time, how each activity breaks into timed tasks, emoji and importance. |

## Start from the library

`games/lib/` holds the defaults. A file starts from them with one line and
only says what's different:

```
// games/mytown/mytown.env.sl
import smallville

environment MyTown {
  days: 1
  building "Hobbs Cafe" { floor: "#E8C07A" }     // merges into the library's cafe
  resident "Ava Novak" {                          // a new resident
    age: 29
    traits: "curious, warm, a little restless"
    background: "Ava Novak just moved to town and works at Hobbs Cafe."
    routine: cafe_owner
    home: "Moreno family's house"
    work: "Hobbs Cafe"
  }
  remove building "Johnson Park"                  // drops one of the library's
  generate { residents: 500 }                     // and 500 more, generated
}
```

```
// games/mytown/mytown.behavior.sl
import smallville

behavior MyTown {
  rules { chattiness: 1.5 }                       // the whole town
  rules student { strangers_talk: false }         // everyone on the student routine
  rules "Klaus Mueller" { vision: 8 }             // one resident
}
```

Fields replace the library's, a node with the same kind and name (`building
"Hobbs Cafe"`, `routine student`) merges into it, and `remove kind "name"`
drops one. Settings are scoped, and the most specific one wins: `style { }` <
`type cafe { }` < `building "Hobbs Cafe" { }` for how buildings look, and
`rules { }` < `rules student { }` < `rules "Name" { }` for how people act.

A town's environment file uses the behavior file in its own folder, so
neither file has to name the other. `games/smallville/` is the library as is
(25 residents, including a Valentine's Day party that spreads by word of
mouth). `games/riverside/` grows it to 1,000 generated residents.

## The app

`cpp/` is **SocialSandbox**, a native C++ editor laid out like Unity's: the
town in the World view, a Town tree of places and residents, an Inspector,
a Rules panel, the Scenarios panel (one folder per town, plus the library), and
Play / Pause / Step. Script tabs highlight `.sl` syntax, suggest words as you
type (Ctrl+Space), and flag typos like `rotuine` with a one-click fix. Editing
a building, a resident, or the rules in the app writes back into the files,
and a file that imports a library is saved as only what it changes.

Residents think with your own models: paste API keys in Model Settings, or
point it at a local LLM (Ollama, LM Studio). With no model, an offline persona
model follows the behavior file, fast enough for thousands of residents.
Build and details: [cpp/README.md](cpp/README.md).

```
cpp\build.bat                                   :: SocialSandbox.exe + sl_run.exe
cpp\build\SocialSandbox.exe                     :: opens smallville
cpp\build\sl_run.exe --town games\riverside       :: headless run
```

## The original `sim` language

Before towns, SocialLang was a language for one-off LLM-agent social games.
The Python interpreter, the browser IDE, and the tests still use it, and its
example scripts stay in `games/*.sl`. The native app shows only environment and
behavior files.

### A complete example

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

### Language basics

A program is one `sim <Name> { ... }` block: `agents` sets the population, `role` blocks define seat kinds (team, memory pattern, visibility, how many), an optional `world` block scatters spatial locations, and `phase`/`fn`/`win_condition`/`loop` are the actual game logic in ordinary imperative syntax (`if`, `while`, `for`, functions, lists, dicts). Roster assignment lives outside the language on purpose -- a `sim` describes shape, not which model fills which seat; `sociallang run` loads `config/models.yaml` and assigns one model per anonymized seat (`P1`, `P2`, ...).

Three memory patterns ship built in (`full_history`, `recent(n)`, and `generative(k)` -- memory-stream retrieval by recency, importance, and relevance), plus `reflect`/`maybe_reflect` for synthesizing higher-level insights back into memory. Built-in functions cover asking agents things (`ask`, `ask_choice`, and bulk `ask_all`/`ask_choice_all` for scale), publishing events (`broadcast`, `whisper`), querying and moving agents (`alive`, `eliminate`, spatial placement), and running the win condition. `games/smallville.sl` implements a full generative-agent loop (persona, planning, reacting, dialogue, reflection) on top of the same builtins, and `games/smallville_mafia.sl` layers Mafia on top of that. Scaling to thousands of agents needs no special syntax -- `role { count: 5000 }` plus ordinary `fn` calls does it; `games/city.sl` runs a full game with 5,050 agents in about a second.

Full grammar, the complete built-in function reference, and design rationale live in [DESIGN.md](DESIGN.md).

### Other ways to run it

- **Browser, no install**: `web/index.html` is a self-contained IDE (lexer/parser/interpreter ported to JS), runs all seven example games against a mock provider client-side. See [web/README.md](web/README.md).
- **SocialSandbox**: `sandbox/` renders a run as a night-sky chart -- agents flare with activity, conversations draw fading lines, reflections become new stars. Ships as a website (`npm run dev`) or Electron app (`npm run electron:dist`); reuses the same JS engine as the web IDE. See [sandbox/README.md](sandbox/README.md).
- **Unity, live**: `python -m sociallang.cli run games/village.sl --mock-only --live` streams events over a local WebSocket (`sociallang/engine/live.py`); `unity/SocialLangViewer` renders agents/locations on a 2D map or replays a saved `results/*.json`. See [unity/README.md](unity/README.md).

### Setup

```
pip install -r requirements.txt -r requirements-dev.txt
cp .env.example .env   # fill in your API keys
```

### Running a game

```
python -m sociallang.cli check games/mafia.sl      # parse and report its shape
python -m sociallang.cli run games/mafia.sl --mock-only
python -m sociallang.cli schema games/mafia.sl      # export roles/memory patterns as JSON
python -m sociallang.cli visualize results/Mafia_0000_....json   # regenerate the HTML replay
```

`--mock-only` needs no API key. Drop it (with `.env` + `config/models.yaml` filled in) to play with real LLMs. Useful `run` flags: `--games N`, `--seed N`, `--vendor`, `--embeddings`/`--llm-importance`.

### Example games

| Game | Agents | What it demonstrates |
|---|---|---|
| `games/mafia.sl` | 6-10 | Hidden roles, night/day phases, custom memory pattern |
| `games/trust_game.sl` | 2 | No hidden roles -- repeated cooperate/defect |
| `games/village.sl` | 6-10 | Hidden roles + `world {}` -- location gates who hears wolves' night chatter |
| `games/city.sl` | 5,050 | Large-population + spatial wandering + concurrent LLM tier |
| `games/outbreak.sl` | 620 | Spread via `nearby()`/`eliminate()`, LLM policy vote changes the outcome |
| `games/smallville.sl` | 4 | Full generative-agent loop (plan, react, talk, reflect) |
| `games/smallville_mafia.sl` | 6-8 | Mafia layered on the Smallville engine, suspicion grounded in real dialogue |

## Tests

```
python -m pytest
```
