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
