# SocialSandbox (native)

A C++ rewrite of the SocialSandbox desktop app, laid out like the Unity editor
and built for simulation throughput: a native SocialLang interpreter, no
Electron, no Python, no browser engine. On this branch it sits alongside the
Electron version in `sandbox/`.

<img src="resources/icon.png" width="64" alt="SocialSandbox logo">

## Build

Needs Visual Studio 2022 with the C++ workload (CMake and Ninja ship with it).

```
cpp\build.bat          :: SocialSandbox.exe + sl_run.exe  ->  cpp\build\
cpp\build_engine.bat   :: engine + sl_run only (no editor)  ->  cpp\build-engine\
```

Both pick the native toolchain for the machine (arm64 or x64). Run the batch
files from PowerShell or cmd. Git Bash's environment breaks MSVC linking.

## The editor

| Panel | What it is |
|---|---|
| **Toolbar** | Game picker on the left. **Play / Pause / Step** at top center, plus run-to-end. Play-mode speed, **Models**, and theme on the right. |
| **Hierarchy** | The sim as a scene graph: World (locations grouped by type) and Agents. Searchable. |
| **Scene** | The world in 2D. Drag or right-drag to pan, scroll to zoom, **F** frames the selection. Agents turn team-colored once their role is revealed and show an ✕ when eliminated. Conversation lines fade over a few rounds. The timeline scrubs back through rounds. |
| **Inspector** | Components for the selection: an agent's Transform, Agent, Persona, Plan, and Recent Activity; a location's occupants; the sim's seed and provider stats; a script's source. |
| **Console** | The run's log, with per-kind toggles (Talk, Town, Reflect, Plans, Asks, Print, Errors) and search. Clicking a line selects its author. |
| **Project** | Every `.sl` in the games folder. Double-click one to load it into the Scene. Right-click and choose *Edit Script* to open it in a tab beside the Scene. |

Play mode works like Unity's. **Play** starts running rounds, and pressing it
again stops and resets to round 0. **Pause** holds you inside play mode.
**Step** advances one round. Shortcuts are Ctrl+P, Ctrl+Shift+P, and
Ctrl+Alt+P. A script that doesn't parse blocks Play with Unity's own message.

Run from a checkout, the Project panel edits the repo's `games/` directly.
The window layout is saved to `%APPDATA%\SocialSandbox\layout.ini`, and
*Window > Reset Layout* restores the default.

Launch flags (for scripted checks):
`--game city.sl --seed 7 --steps 3 --to-end --select P12 --light --settings`

## Models: your own keys, or local LLMs

Out of the box every agent is answered by a **mock** that picks randomly. It's
free and offline, but it isn't real behavior. Open **Models** in the toolbar to
change that:

- **Your API keys** for Anthropic, OpenAI, Google Gemini, xAI, or OpenRouter.
  Keys are encrypted with Windows DPAPI in `%APPDATA%\SocialSandbox\settings.json`,
  so only your Windows account on this PC can decrypt them. A blank key falls
  back to the same environment variables the Python CLI reads
  (`ANTHROPIC_API_KEY`, ...).
- **Local LLMs**: any OpenAI-compatible server, including Ollama
  (`http://localhost:11434/v1`), LM Studio (`http://localhost:1234/v1`), and
  llama.cpp's server. They need no key, and nothing leaves the machine.

Each enabled model is dealt to agents at random when a game loads, and every
row has a **Test** button. Calls, tokens, and errors are counted in the
status bar. HTTP goes through WinHTTP, so there's no curl or OpenSSL
dependency.

## Engine

`src/engine/` ports `sociallang/lang/` with the same grammar, builtins, memory
patterns, and Generative Agents mechanisms, laid out for speed:

- Agents, locations, and events are plain indices, not heap objects. A `Value`
  is 32 bytes.
- Identifiers, attributes, and kwargs are interned to integer symbols at parse
  time, and a link pass resolves every call and `run` once.
- `return` and `break` are returned codes, not exceptions. Scopes live on the
  stack.
- An agent's memory context (retrieve plus render every visible event) is
  built only when its provider will actually read it. The mock never does.
- `maybe_reflect`'s "importance since last reflection" is a running sum, not a
  rescan of history.

`sl_run` is the headless runner:

```
sl_run ..\games\city.sl                          one run, mock provider
sl_run ..\games\mafia.sl --seed 7 --log          every log line
sl_run ..\games\village.sl --models              the roster from Model Settings
sl_run ..\games\city.sl --runs 64 --jobs 8       64 seeds, 8 at a time
sl_run bench\bulk_memory.sl --mock-context       mock that builds every context
sl_run ... --serial                              reference path (no parallel context)
```

## Performance (measured, 8-core ARM64 laptop, mock provider)

Engine vs. the JS engine the Electron app runs (Node, ms per full run, 20 runs):

| Game | JS | C++ |
|---|---|---|
| smallville.sl (4 agents, 13 rounds) | 103 | 0.83 |
| smallville_mafia.sl | 46 | 0.71 |
| outbreak.sl (620 agents) | 12.3 | 4.8 |
| city.sl (5,050 agents) | 13.5 | 7.5 |

The big multipliers come mostly from building contexts lazily, not from C++ as
such. The JS engine renders every agent's memory into a prompt the mock then
ignores.

### Parallelism

A round's script is inherently sequential. Every `let`, move, and random draw
depends on the ones before it, and a seed has to replay the same run. So
parallelism lives where work is independent:

- **Bulk asks** (`ask_all`, `ask_choice_all`) build every agent's memory
  context data-parallel on a persistent worker pool, and fan real model calls
  out across up to *N* in-flight requests (Model Settings > Concurrent
  requests). Events are still appended in agent order, so logs are
  deterministic. On `bench/bulk_memory.sl` (400 agents, `generative(8)`
  memory) with `--mock-context`, it goes from **574 ms to 151 ms per run (3.8×)**.
  Serial and parallel logs are byte-identical, checked on bulk_memory,
  smallville_mafia, and city.
- **Independent runs**: `--runs N --jobs K` runs seeds concurrently. 256
  smallville_mafia seeds take **225 ms on 1 thread and 63 ms on 8 (3.6×)**. 64
  city seeds take 580 ms to 215 ms (2.7×). City stops scaling past two threads
  because per-run time climbs from 8 to 22 ms, which points to contention on
  the shared heap. That's the next thing to profile.
- The **editor** runs the simulation on its own worker thread and publishes
  compact per-round snapshots, so the UI holds 60 fps while a round waits on
  real models.

Not done yet: an event-driven scheduler (agents think only when something
happens), continuous batching against a local inference server, and GPU
retrieval. The measurements above say where they'd pay off.
