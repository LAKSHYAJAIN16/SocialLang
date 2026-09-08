import os

from sociallang.lang.parser import parse
from sociallang.schema_export import export_schema

GAMES_DIR = os.path.join(os.path.dirname(__file__), "..", "games")


def _load(name: str) -> str:
    with open(os.path.join(GAMES_DIR, name), encoding="utf-8") as f:
        return f.read()


def test_export_schema_lists_roles_and_memory_patterns_for_mafia():
    sim = parse(_load("mafia.sl"))
    schema = export_schema(sim)

    assert schema["name"] == "Mafia"
    assert schema["agents"] == {"min": 6, "max": 10}
    role_names = {r["name"] for r in schema["roles"]}
    assert role_names == {"Mafia", "Detective", "Doctor", "Villager"}

    villager = next(r for r in schema["roles"] if r["name"] == "Villager")
    assert villager["count"] == "remainder"

    mafia_role = next(r for r in schema["roles"] if r["name"] == "Mafia")
    assert mafia_role["memory"] == {"pattern": "recent", "args": [20.0]}


def test_custom_memory_pattern_shadows_the_native_kind_of_the_same_name():
    sim = parse(_load("mafia.sl"))
    schema = export_schema(sim)
    recent_entries = [p for p in schema["memory_patterns"] if p["name"] == "recent"]
    assert len(recent_entries) == 1
    assert recent_entries[0]["custom"] is True
