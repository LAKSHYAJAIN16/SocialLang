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


def test_parses_city_sl_end_to_end_with_a_world_block():
    sim = parse(_load("city.sl"))
    assert sim.name == "City"
    assert sim.world is not None
    assert sim.world.width == 200 and sim.world.height == 200
    location_names = {lt.name for lt in sim.world.location_types}
    assert location_names == {"Home", "Market", "Plaza"}
    role_counts = {r.name: r.count for r in sim.roles}
    assert role_counts == {"Citizen": 5000, "Journalist": 50}


def test_parses_village_sl_end_to_end():
    sim = parse(_load("village.sl"))
    assert sim.name == "Village"
    role_names = {r.name for r in sim.roles}
    assert role_names == {"Wolf", "Seer", "Villager"}
    assert sim.world is not None
    location_names = {lt.name for lt in sim.world.location_types}
    assert location_names == {"Plaza", "Den", "House"}
    phase_names = {p.name for p in sim.phases}
    assert phase_names == {"Settle", "Night", "Day"}


def test_parses_outbreak_sl_end_to_end():
    sim = parse(_load("outbreak.sl"))
    assert sim.name == "Outbreak"
    role_counts = {r.name: r.count for r in sim.roles}
    assert role_counts == {"Resident": 500, "PatientZero": 20, "Official": 100}
    assert sim.world is not None
    assert sim.world.width == 150 and sim.world.height == 150


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
