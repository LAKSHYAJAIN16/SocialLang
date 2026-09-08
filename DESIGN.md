# SocialLang design doc

## Why this repo exists

[MafiaSim](https://github.com/LAKSHYAJAIN16/LLM-mafia) runs one specific game (Mafia) with the
rules hardcoded into Python classes: `Role` is a 4-value enum, `engine.py` knows exactly what a
night phase and a day phase are, `prompts.py` has Mafia-specific sections baked into the prompt
builder. That's the right call for a single-game research project, but it means adding a second
game means rewriting the engine.

SocialLang is a declarative schema for *defining* a social/game setting, plus a generic engine
that runs any game satisfying that schema by driving LLM agents through it — instead of one
engine per game. MafiaSim's LLM plumbing (`providers/`: the `ChatProvider` abstraction,
`ModelSpec`/roster loading, cost tracking, retry logic) is completely game-agnostic already and
is ported into this repo verbatim (`sociallang/providers/`). What's new here is everything that
was previously hardcoded per-game: roles, phases, actions, visibility, win conditions.

**Scope decision (per discussion):** this is a *config layer*, not a new parser/grammar. A
"SocialLang program" is a YAML document validated against a schema — no new syntax to learn, no
lexer/interpreter to build and debug. "Real DSL with its own grammar" was considered and rejected
for now: it's a much bigger build (parser, static analysis, error messages with line numbers) for
expressiveness this project doesn't need yet. If the YAML schema turns out to be too limited
(e.g. win conditions that need real expressiveness, not just a small predicate language), a real
grammar is a natural v2 — this doc's model doesn't change, just the surface syntax.

## Core abstraction

Every game definition has five parts:

1. **Roles** — who can be assigned to a seat: a name, a team, what actions the role can take and
   in which phases, and what it can privately see.
2. **Phases** — the ordered (possibly cyclic) sequence of stages a game moves through, each
   naming which roles act, which action types are legal, and whether actions within the phase
   resolve simultaneously or in sequence.
3. **Actions** — the vocabulary of things a seat can do: speak publicly, vote, send a private
   message to a subset of seats, or invoke a named role ability against a target.
4. **Visibility** — who learns what, and when: this is what makes a hidden-role game hidden (mafia
   see each other; town doesn't see roles at all) as opposed to a fully-observed game (everyone
   sees every action immediately).
5. **Win conditions** — a per-team predicate over game state (alive counts, elapsed rounds,
   accumulated scores, ...), checked after every phase resolves.

The generic engine's job is: load a game definition, deal roles to seats, and loop over phases,
at each step building a prompt for whichever seat(s) must act (parameterized by that role's
declared visibility — this is the direct generalization of what `mafia_sim/game/prompts.py`
already does by hand for Mafia specifically), collecting and validating their action against the
phase's legal action list (generalizing `mafia_sim/game/parsing.py`), applying resolution rules,
and checking win conditions.

## Schema reference

```yaml
name: string                       # game identifier
teams: [string, ...]               # every team a role can belong to

roles:
  <role_name>:
    team: string                   # must be one of `teams`
    count: int | "remainder"       # exact seats, or "fill whatever's left"
    sees:                          # what this role privately knows, beyond public state
      - own_role                   # always implicit, listed for clarity
      - teammates                  # other seats sharing this role's team (e.g. mafia see mafia)
      - <ability_name>_results     # e.g. "investigate_results" for a detective-like role
    actions:                       # ability names this role may invoke (beyond phase defaults)
      - <action_name>

phases:
  - name: string
    order: sequential | simultaneous   # do actors within this phase see each other's actions
                                        # as they happen (sequential) or only after all are in
                                        # (simultaneous)?
    actors: all | <team_name> | <role_name>   # who is prompted to act this phase
    legal_actions: [speak, vote, whisper, use_ability, pass]
    resolution:                    # only meaningful when legal_actions includes use_ability
                                    # or vote; priority order for simultaneous same-phase effects
      - <action_name or role_name>
    repeats_until: <win_conditions_checked> | <n_rounds>

win_conditions:
  - team: string
    when: <predicate>              # small expression language, see below

# predicate language: comparisons over built-in state vars, joined with and/or
#   alive(<team_or_role>)          -> count of living seats on that team/role
#   eliminated(<team_or_role>)     -> count removed from play
#   round                          -> current phase-cycle count
#   score(<team_or_role>)          -> accumulated numeric score, for scoring-based games
# e.g.:  "alive(mafia) >= alive(town)"
#        "eliminated(mafia) == 0 and round > 10"
```

This covers what MafiaSim's engine currently does by hand. It deliberately does *not* yet cover:
persistent per-seat numeric resources beyond `score` (e.g. an economy/currency), actions with
continuous/structured parameters beyond "pick a target seat" (e.g. "offer a trade of X for Y"),
or conditional role abilities that change mid-game. Those are the concrete cases where "config
layer" would start to strain and a real grammar would earn its cost — worth watching for once a
second or third game is built, not solving speculatively now.

## Worked example 1: Mafia, expressed in SocialLang

This is what `mafia_sim/game/roles.py` + `engine.py` currently hardcode, rewritten as data:

```yaml
name: mafia
teams: [mafia, town]

roles:
  mafia:
    team: mafia
    count: 2
    sees: [own_role, teammates]
    actions: [kill]
  detective:
    team: town
    count: 1
    sees: [own_role, investigate_results]
    actions: [investigate]
  doctor:
    team: town
    count: 1
    sees: [own_role]
    actions: [protect]
  villager:
    team: town
    count: remainder
    sees: [own_role]
    actions: []

phases:
  - name: night
    order: simultaneous
    actors: all               # each acting role only sees the prompt matching its own actions
    legal_actions: [use_ability, whisper]   # whisper == mafia's private night chat
    resolution: [protect, kill, investigate]   # doctor's save must land before the kill resolves
  - name: day
    order: sequential
    actors: all
    legal_actions: [speak, vote]
    repeats_until: win_conditions_checked

win_conditions:
  - team: mafia
    when: "alive(mafia) >= alive(town)"
  - team: town
    when: "alive(mafia) == 0"
```

`games/mafia.yaml` in this repo is exactly this.

## Worked example 2: an iterated trust game, to stress-test generality

Mafia is a hidden-role, team-elimination game. To check the schema isn't just "Mafia with the
names changed," here's a structurally different game: no hidden roles, no elimination, a fixed
population that repeatedly chooses to cooperate or defect, winning by accumulated score rather
than team survival — closer to an iterated Prisoner's Dilemma / public-goods game with
reputation, still legible as a "social setting" simulated by LLM agents:

```yaml
name: trust_game
teams: [player]                    # everyone's on the same "team" -- there's no hidden alignment

roles:
  player:
    team: player
    count: remainder
    sees: [own_role, public_history]   # everyone sees every past round's outcomes -- no
                                        # hidden information at all, unlike Mafia
    actions: [cooperate, defect]

phases:
  - name: round
    order: simultaneous               # both players choose blind to each other's current pick
    actors: all
    legal_actions: [use_ability]      # cooperate/defect are use_ability invocations, not votes
    resolution: [cooperate, defect]   # order doesn't matter here, both apply at once
    repeats_until: 20_rounds

win_conditions:
  - team: player
    when: "round > 20"                # game just ends; each player's own score is the outcome,
                                       # there's no team to declare a winner -- ranking happens
                                       # outside win_conditions, from each seat's final score
```

`games/trust_game.yaml` in this repo is exactly this. Notice what had to flex to fit both games
in one schema: `teams` can be a single team (no real "sides"), `sees` can be "everything, always"
instead of role-gated, and a win condition can be a simple round-count instead of a
team-elimination check. Nothing in the *shape* of the schema had to change — that's the signal
the abstraction is at roughly the right level, not too narrow.

## What's ported vs. new

**Ported from MafiaSim as-is (`sociallang/providers/`):** `ChatProvider` abstraction, all four
provider adapters (Anthropic/Google/OpenAI-compatible/mock), retry logic, `ModelSpec` + roster
loading + vendor filtering. None of this is Mafia-specific; it was already a clean seam.

**New, not yet built:** the schema validator, the generic phase-loop engine (generalizing
`game/engine.py`), the generic prompt builder (generalizing `game/prompts.py`'s per-role
visibility logic to read `sees:` from the schema instead of having it hand-written per role), the
generic action parser (generalizing `game/parsing.py`), and results logging generalized to not
assume Mafia's specific log shapes (`public_log`/`mafia_log`/`vote_log` become one generic
per-visibility-channel event log).

## Open questions before building the engine

- Predicate language for `win_conditions.when`: hand-roll a tiny parser for the handful of
  operators above, or is that already "the DSL" in disguise and worth just committing to?
- Does `resolution:` priority need per-action parameters (e.g. "doctor can't protect the same
  seat two rounds running"), or is a strict priority order enough for the games actually planned?
- Multi-round phases (`repeats_until: n_rounds` in the trust game) vs. condition-gated phases
  (`repeats_until: win_conditions_checked` in Mafia's day phase) — same field, two different
  meanings; worth splitting into two fields once a third game clarifies the actual pattern space.

## Next step

Confirm this schema shape covers what you have in mind, then: (1) port Mafia itself onto this
schema as the first real SocialLang game (validates the abstraction against a game that
actually has to work, not just a sketch), (2) build the generic engine, (3) build a second real
game (the trust game above, or something else) to catch anywhere the schema was accidentally
Mafia-shaped.
