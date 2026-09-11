import time

from sociallang.lang.interpreter import SLRuntimeError, run_source
from sociallang.lang.parser import parse
from sociallang.providers.base import ChatProvider, ProviderResponse


class FixedProvider(ChatProvider):
    def __init__(self, text: str):
        super().__init__(model_id="fixed", api_key=None)
        self.text = text

    def complete(self, system_prompt, user_prompt, temperature=0.9, max_tokens=500, timeout=60):
        return ProviderResponse(text=self.text)


class SlowProvider(ChatProvider):
    """Sleeps a fixed amount per call -- used to prove ask_all/ask_choice_all
    dispatch concurrently rather than serially.
    """

    def __init__(self, text: str, delay: float = 0.1):
        super().__init__(model_id="slow", api_key=None)
        self.text = text
        self.delay = delay

    def complete(self, system_prompt, user_prompt, temperature=0.9, max_tokens=500, timeout=60):
        time.sleep(self.delay)
        return ProviderResponse(text=self.text)


WORLD_SOURCE = """
sim WorldTest {
  agents: 1
  role Solo { team: "t" memory: full_history sees: none count: 1 }
  world {
    width: 50
    height: 50
    location Cafe { tag: "social", capacity: 10, count: 3 }
    location House { tag: "private", count: 5 }
  }
  win_condition { return "done" }
  loop { return check_win() }
}
"""


def test_world_block_parses_size_and_location_types():
    sim = parse(WORLD_SOURCE)
    assert sim.world is not None
    assert sim.world.width == 50 and sim.world.height == 50
    by_name = {lt.name: lt for lt in sim.world.location_types}
    assert by_name["Cafe"].tag == "social"
    assert by_name["Cafe"].capacity == 10
    assert by_name["Cafe"].count == 3
    assert by_name["House"].tag == "private"
    assert by_name["House"].capacity is None
    assert by_name["House"].count == 5


def test_a_sim_with_no_world_block_leaves_it_none():
    sim = parse("""
    sim NoWorld {
      agents: 1
      role Solo { team: "t" memory: full_history sees: none count: 1 }
      win_condition { return "done" }
      loop { return check_win() }
    }
    """)
    assert sim.world is None


def test_world_setup_scatters_the_declared_count_of_each_location_type_deterministically():
    from sociallang.lang.interpreter import Interpreter, assign_agents

    sim = parse(WORLD_SOURCE)
    roster = {"solo": (None, FixedProvider("ok"))}

    import random
    agents1, roles1 = assign_agents(sim, roster, random.Random(0))
    interp1 = Interpreter(sim, agents1, roles1, seed=0)
    agents2, roles2 = assign_agents(sim, roster, random.Random(0))
    interp2 = Interpreter(sim, agents2, roles2, seed=0)

    assert len(interp1.world_locations) == 8  # 3 Cafe + 5 House
    cafes = [loc for loc in interp1.world_locations.values() if loc.type_name == "Cafe"]
    assert len(cafes) == 3
    assert all(0 <= loc.x <= 50 and 0 <= loc.y <= 50 for loc in interp1.world_locations.values())
    # Same seed -> identical placement (positions come from the shared seeded rng).
    pos1 = {loc_id: (loc.x, loc.y) for loc_id, loc in interp1.world_locations.items()}
    pos2 = {loc_id: (loc.x, loc.y) for loc_id, loc in interp2.world_locations.items()}
    assert pos1 == pos2


def test_spatial_builtins_spawn_move_and_query_agents():
    source = """
    sim SpatialTest {
      agents: 3
      role P { team: "t" memory: full_history sees: none count: 3 }
      world {
        width: 10
        height: 10
        location Spot { tag: "x", count: 4 }
      }
      win_condition { return "done" }
      loop {
        spawn_agents_at(alive())
        let a0 = alive()[0]
        let loc = location_of(a0)
        print(loc.tag)
        let near = nearby(a0, 100)
        print(count(near))
        return check_win()
      }
    }
    """
    roster = {"a": (None, FixedProvider("ok"))}
    result = run_source(source, roster, seed=0, max_rounds=1)
    prints = [e["text"] for e in result["log"] if e["kind"] == "print"]
    assert prints[0] == "x"
    assert prints[1] == "2"  # other 2 agents, within a radius covering the whole 10x10 grid


def test_move_to_updates_agent_location_and_agents_at_reflects_it():
    source = """
    sim MoveTest {
      agents: 2
      role P { team: "t" memory: full_history sees: none count: 2 }
      world {
        width: 5
        height: 5
        location Home { tag: "h", count: 1 }
        location Away { tag: "a", count: 1 }
      }
      win_condition { return "done" }
      loop {
        spawn_agents_at(alive(), "h")
        let target = locations_by_tag("a")[0]
        move_to(alive()[0], target)
        print(count(agents_at(target)))
        return check_win()
      }
    }
    """
    roster = {"a": (None, FixedProvider("ok"))}
    result = run_source(source, roster, seed=0, max_rounds=1)
    prints = [e["text"] for e in result["log"] if e["kind"] == "print"]
    assert prints[0] == "1"


def test_ask_choice_all_fires_calls_concurrently_not_serially():
    source = """
    sim BulkAsk {
      agents: 8
      role P { team: "t" memory: full_history sees: none count: 8 }
      win_condition { return "done" }
      loop {
        let picks = ask_choice_all(alive(), "pick one", ["a", "b"])
        print(count(picks))
        return check_win()
      }
    }
    """
    delay = 0.12
    roster = {f"m{i}": (None, SlowProvider("a", delay=delay)) for i in range(8)}
    t0 = time.perf_counter()
    result = run_source(source, roster, seed=0, max_rounds=1)
    elapsed = time.perf_counter() - t0
    prints = [e["text"] for e in result["log"] if e["kind"] == "print"]
    assert prints[0] == "8"
    # Serial would take ~8 * delay (~0.96s); concurrent should stay well under half that.
    assert elapsed < delay * 4


def test_ask_choice_all_results_are_applied_in_deterministic_agent_order():
    from sociallang.lang.interpreter import Interpreter, assign_agents
    import random

    source = """
    sim OrderTest {
      agents: 4
      role P { team: "t" memory: full_history sees: none count: 4 }
      win_condition { return "done" }
      loop { return check_win() }
    }
    """
    roster = {f"m{i}": (None, FixedProvider("a")) for i in range(4)}
    sim = parse(source)
    agents, roles = assign_agents(sim, roster, random.Random(0))
    interp = Interpreter(sim, agents, roles, seed=0)
    picks = interp.builtins["ask_choice_all"]([list(agents), "pick", ["a", "b"]], {})
    # Every ask event for this batch was appended in the same order as `agents`.
    ask_authors = [e.author for e in interp.events if e.kind == "ask"]
    assert ask_authors == [a.seat for a in agents]
    assert set(picks.keys()) == set(agents)


def test_ask_choice_all_with_max_workers_zero_still_runs_instead_of_crashing():
    # ThreadPoolExecutor(max_workers=0) raises ValueError -- max_workers is clamped
    # to at least 1 before being passed through, regardless of what a script asks for.
    source = """
    sim ZeroWorkers {
      agents: 3
      role P { team: "t" memory: full_history sees: none count: 3 }
      win_condition { return "done" }
      loop {
        let picks = ask_choice_all(alive(), "pick one", ["a", "b"], max_workers=0)
        print(count(picks))
        return check_win()
      }
    }
    """
    roster = {f"m{i}": (None, FixedProvider("a")) for i in range(3)}
    result = run_source(source, roster, seed=0, max_rounds=1)
    prints = [e["text"] for e in result["log"] if e["kind"] == "print"]
    assert prints[0] == "3"


def test_duplicate_location_type_names_raise_instead_of_silently_colliding():
    source = """
    sim DupWorld {
      agents: 1
      role Solo { team: "t" memory: full_history sees: none count: 1 }
      world {
        width: 10
        height: 10
        location Spot { tag: "a", count: 2 }
        location Spot { tag: "b", count: 3 }
      }
      win_condition { return "done" }
      loop { return check_win() }
    }
    """
    roster = {"a": (None, FixedProvider("ok"))}
    try:
        run_source(source, roster, seed=0, max_rounds=1)
        assert False, "expected SLRuntimeError for duplicate location type name"
    except SLRuntimeError as exc:
        assert "duplicate location type" in str(exc)
        assert "Spot" in str(exc)
