# SocialLang Unity viewer

A live viewer for a running (or previously saved) SocialLang simulation: agents
render as colored points on a 2D map, locations from the sim's `world {}` block
render as markers, and you can click an agent to see its recent events.

**I could not open this in the Unity Editor myself to verify it visually** — there's
no Editor available from here. Everything below is written and reasoned through
carefully (and the Python side it talks to is fully tested), but the Unity half needs
you to actually press Play and confirm it behaves as described. See the checklist at
the bottom.

## Design choice: nothing is a hand-authored `.unity` scene

The whole viewer assembles itself at runtime from `Assets/Scripts/Bootstrap.cs`
(`[RuntimeInitializeOnLoadMethod]`) — camera, renderers, network/replay readers, and
UI are all created in code the moment you press Play, in *any* scene, even a totally
empty one. This was a deliberate choice: a hand-written `.unity` scene file is exactly
the kind of thing that silently breaks without an Editor open to catch it (stale
GUIDs, a malformed YAML block, a missing component reference). C# either compiles or
it doesn't, so that's where the risk should live instead.

## Setup

1. Open Unity Hub → **Add** → point it at this `unity/SocialLangViewer` folder.
   - `ProjectSettings/ProjectVersion.txt` names `2022.3.50f1`. If you don't have that
     exact patch installed, Unity Hub will offer to open it with whatever 2022 LTS (or
     Unity 6) version you do have — that's fine, nothing here is version-specific
     beyond UGUI + ParticleSystem + Newtonsoft.Json, all long-stable APIs.
2. Let it import. On first open, Unity Package Manager will fetch
   `com.unity.nuget.newtonsoft-json` (listed in `Packages/manifest.json`) — needs a
   network connection once.
3. Open (or create) any scene, e.g. File → New Scene.
4. Press **Play**.

You should see, in order:
- A console log: `[SocialLangViewer] bootstrapped. Connecting to ws://localhost:8765 ...`
- A small panel top-left with a URL field + "Connect (live)" button, and a file-path
  field + "Load & play replay file" button.
- If nothing is listening on that URL yet, the status line reads "live: not
  connected" — that's expected, not a bug; start a live run (below) or load a replay.

## Watching a live run

In the SocialLang repo (a separate terminal from the Unity Editor):

```
python -m sociallang.cli run games/village.sl --mock-only --live
```

`--live` opens the WebSocket bridge and waits `--live-wait` seconds (3 by default) —
that's your window to hit **Play** in Unity (or click "Connect (live)" if the viewer
is already running) before the opening world/agents snapshot fires. Try `city.sl` or
`outbreak.sl` instead for the large-population demos (thousands of points moving each
round).

## Watching a saved run (replay)

```
python -m sociallang.cli run games/mafia.sl --mock-only
```

writes `results/Mafia_0000_<timestamp>.json`. Paste that path into the viewer's
replay field and click "Load & play replay file". Note the honest limitation: a saved
run only has *final* agent positions (the live stream is what carries movement over
time, not the JSON file), so replay shows the world layout and the full event feed
scrolling by at the right pace, with agents appearing at their end-of-run position —
not animating between locations. For full movement playback, use `--live`.

## Controls

- Left click: select the nearest agent (inspector panel, right side)
- Right or middle mouse drag: pan
- Scroll wheel: zoom

## Verification checklist (please run through this once)

- [ ] Project opens in Unity Hub without errors
- [ ] Newtonsoft.Json package resolves (Window → Package Manager shows it installed)
- [ ] Pressing Play with no live run/replay loaded shows the empty viewer + panel,
      no console errors
- [ ] `sociallang run games/village.sl --mock-only --live`, connect from Unity:
      location markers appear, agent points appear and move between rounds, event
      text shows up when you click an agent
- [ ] Loading a saved `results/*.json` via the replay field plays the event feed and
      shows final agent state
- [ ] `sociallang run games/city.sl --mock-only --live` (5,050 agents): frame rate
      stays usable — this is the actual point of the particle-based renderer instead
      of per-agent GameObjects

If any of these don't hold, that's real signal — tell me what broke and I'll fix the
C# (I can reason about and correct code; I just can't see the Editor to catch it
first).
