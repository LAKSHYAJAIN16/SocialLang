# SocialLang

A declarative schema for defining social-deduction / multi-agent social games, plus (planned) a
generic engine that runs any game satisfying that schema by driving LLM agents through it.

Spun out of [MafiaSim](https://github.com/LAKSHYAJAIN16/LLM-mafia), which runs one specific game
(Mafia) with the rules hardcoded in Python. `sociallang/providers/` is MafiaSim's LLM provider
plumbing (Anthropic/Google/OpenAI-compatible/mock adapters, roster loading, cost tracking, retry
logic) ported over as-is — it was already game-agnostic. Everything under `games/` and the
schema itself is new.

See [DESIGN.md](DESIGN.md) for the schema, the reasoning behind it, and two worked examples
(Mafia, and a structurally different iterated trust game) showing it isn't just "Mafia with the
names changed."

**Status:** design doc + two example game definitions exist. The generic engine that actually
runs a `games/*.yaml` definition hasn't been built yet — that's the next step once the schema
shape is confirmed.

## Setup

```
pip install -r requirements.txt -r requirements-dev.txt
cp .env.example .env   # fill in whichever provider API keys you have
```
