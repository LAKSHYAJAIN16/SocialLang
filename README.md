# SocialLang

A programming language for defining social games — roles, phases, custom rules, custom
agent memory — that AI agents then play. See [DESIGN.md](DESIGN.md) for the language.

Built on [MafiaSim](https://github.com/LAKSHYAJAIN16/LLM-mafia)'s LLM provider layer.

## Setup

```
pip install -r requirements.txt -r requirements-dev.txt
cp .env.example .env   # fill in your API keys
```

## Try it

```
python -m sociallang.cli check games/mafia.sl
python -m sociallang.cli run games/mafia.sl --mock-only
python -m sociallang.cli schema games/mafia.sl
```

Five example games in `games/`: `mafia.sl`/`village.sl` (hidden-role social deduction —
`village.sl` adds a procedurally generated `world {}`), `trust_game.sl` (no hidden
roles, just repeated cooperate/defect rounds), and `city.sl`/`outbreak.sl` (large
spatial populations — 5,000+ and 600+ agents respectively, mixing a mostly-scripted
population with a small LLM-driven tier called concurrently via `ask_choice_all`).

Add `--live` to `run` to stream the simulation over a local WebSocket bridge as it
plays, for an external live viewer instead of only reading the saved JSON/HTML
afterwards — see `sociallang/engine/live.py` for the message schema.
