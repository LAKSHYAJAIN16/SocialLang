import os

from sociallang.lang.parser import parse

GAMES_DIR = os.path.join(os.path.dirname(__file__), "..", "games")


def _load(name: str) -> str:
    with open(os.path.join(GAMES_DIR, name), encoding="utf-8") as f:
        return f.read()


def test_parses_mafia_sl_end_to_end():
    sim = parse(_load("mafia.sl"))
    assert sim.name == "Mafia"
    assert sim.agents_min == 6 and sim.agents_max == 10
    role_names = {r.name for r in sim.roles}
    assert role_names == {"Mafia", "Detective", "Doctor", "Villager"}
    phase_names = {p.name for p in sim.phases}
    assert phase_names == {"Night", "Day"}
    assert sim.win_condition is not None
    assert sim.loop is not None


def test_parses_trust_game_sl_end_to_end():
    sim = parse(_load("trust_game.sl"))
    assert sim.name == "TrustGame"
    assert len(sim.fns) == 1
    assert sim.fns[0].name == "other"


def test_role_remainder_count_parses_as_none():
    sim = parse(_load("mafia.sl"))
    villager = next(r for r in sim.roles if r.name == "Villager")
    assert villager.count is None
    mafia = next(r for r in sim.roles if r.name == "Mafia")
    assert mafia.count == 2


def test_custom_memory_decl_parses_params_and_body():
    sim = parse(_load("mafia.sl"))
    assert len(sim.memory_decls) == 1
    decl = sim.memory_decls[0]
    assert decl.name == "recent"
    assert decl.params == ["n"]
    assert len(decl.body.statements) == 1
