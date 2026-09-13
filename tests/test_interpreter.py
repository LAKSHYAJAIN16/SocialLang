import os

from sociallang.lang.interpreter import SLRuntimeError, run_source
from sociallang.providers.base import ChatProvider, ProviderResponse

GAMES_DIR = os.path.join(os.path.dirname(__file__), "..", "games")


def _load(name: str) -> str:
    with open(os.path.join(GAMES_DIR, name), encoding="utf-8") as f:
        return f.read()


class FixedProvider(ChatProvider):
    """Always answers with the same fixed text -- for tests that need a deterministic
    agent decision rather than the mock provider's randomized-but-legal choice.
    """

    def __init__(self, text: str):
        super().__init__(model_id="fixed", api_key=None)
        self.text = text
        self.calls: list[tuple[str, str]] = []

    def complete(self, system_prompt, user_prompt, temperature=0.9, max_tokens=500, timeout=60):
        self.calls.append((system_prompt, user_prompt))
        return ProviderResponse(text=self.text)


def test_trust_game_runs_to_completion_and_reports_round_count():
    provider_a = FixedProvider("cooperate")
    provider_b = FixedProvider("defect")
    roster = {"a": (None, provider_a), "b": (None, provider_b)}
    result = run_source(_load("trust_game.sl"), roster, seed=1, max_rounds=25)
    assert result["winner"] == "complete"
    assert result["rounds"] == 21  # win_condition fires once round > 20


def test_recent_memory_pattern_windows_to_the_last_n_events():
    provider = FixedProvider("ok")
    roster = {"solo": (None, provider)}
    source = """
    sim MemTest {
      agents: 1
      role Solo { team: "solo" memory: recent(2) sees: none count: 1 }
      memory recent(n) { return last(events, n) }
      phase Speak {
        broadcast("event one")
        broadcast("event two")
        broadcast("event three")
        for a in alive() {
          ask(a, "what do you remember?")
        }
      }
      win_condition { return "done" }
      loop { run Speak return check_win() }
    }
    """
    run_source(source, roster, seed=0, max_rounds=3)
    last_system, last_user = provider.calls[-1]
    assert "event three" in last_user
    assert "event two" in last_user
    assert "event one" not in last_user  # windowed out by recent(2)


def test_tally_and_eliminate_pick_the_majority_vote_target():
    provider = FixedProvider("P1")
    roster = {f"m{i}": (None, FixedProvider("P1")) for i in range(3)}
    source = """
    sim VoteTest {
      agents: 3
      role Player { team: "player" memory: full_history sees: none count: remainder }
      phase Vote {
        let votes = {}
        for a in alive() {
          votes[a] = ask_choice(a, "who?", alive())
        }
        let target = tally(votes)
        eliminate(target, "voted out")
      }
      win_condition { return "done" }
      loop { run Vote return check_win() }
    }
    """
    result = run_source(source, roster, seed=0, max_rounds=3)
    eliminated = [a for a in result["agents"] if not a["alive"]]
    assert len(eliminated) == 1
    assert eliminated[0]["seat"] == "P1"
    assert eliminated[0]["death_cause"] == "voted out"


def test_assigning_an_undeclared_variable_raises_runtime_error():
    roster = {"a": (None, FixedProvider("ok"))}
    source = """
    sim Bad {
      agents: 1
      role Solo { team: "solo" memory: full_history sees: none count: 1 }
      phase P { undeclared_var = 1 }
      win_condition { return "done" }
      loop { run P return check_win() }
    }
    """
    try:
        run_source(source, roster, seed=0, max_rounds=1)
        assert False, "expected SLRuntimeError"
    except SLRuntimeError as exc:
        assert "undeclared_var" in str(exc)


def test_mafia_sl_runs_to_completion_with_mock_provider():
    from sociallang.providers.mock_provider import MockProvider

    roster = {f"mock-{i}": (None, MockProvider("mock", None)) for i in range(8)}
    result = run_source(_load("mafia.sl"), roster, seed=3, max_rounds=30)
    assert result["winner"] in ("town", "mafia")
    assert result["rounds"] >= 1


def test_village_sl_runs_to_completion_with_mock_provider():
    from sociallang.providers.mock_provider import MockProvider

    roster = {f"mock-{i}": (None, MockProvider("mock", None)) for i in range(8)}
    result = run_source(_load("village.sl"), roster, seed=2, max_rounds=30)
    assert result["winner"] in ("town", "wolf")
    assert result["rounds"] >= 1
    # spawn_agents_at ran during Settle, so every agent should have a position by now.
    assert all(a["x"] is not None and a["y"] is not None for a in result["agents"])


def test_outbreak_sl_runs_to_completion_with_mock_provider():
    import time

    from sociallang.providers.mock_provider import MockProvider

    roster = {"mock-random": (None, MockProvider("mock", None))}
    t0 = time.perf_counter()
    result = run_source(_load("outbreak.sl"), roster, seed=0, max_rounds=10)
    elapsed = time.perf_counter() - t0

    assert result["winner"] in ("contained", "outbreak")
    assert len(result["agents"]) == 620
    assert elapsed < 20.0


def test_city_sl_runs_a_large_spatial_population_quickly_with_mock_provider():
    import time

    from sociallang.providers.mock_provider import MockProvider

    roster = {"mock-random": (None, MockProvider("mock", None))}
    t0 = time.perf_counter()
    result = run_source(_load("city.sl"), roster, seed=0, max_rounds=5)
    elapsed = time.perf_counter() - t0

    assert result["winner"] == "done"
    assert result["rounds"] == 3
    assert len(result["agents"]) == 5050
    # 5050 agents (5000 scripted + 50 concurrent-LLM-tier) over 3 rounds is the
    # regression guard for the O(agents x events)->O(agent's own visible count)
    # visibility-index fix and the thread-pooled ask_choice_all -- this used to be
    # quadratic in event-log size and serial per LLM call.
    assert elapsed < 15.0


def test_smallville_sl_runs_to_completion_with_mock_provider():
    """End-to-end smoke test for the full Generative Agents pipeline (persona,
    make_plan/decompose_step, react/converse, maybe_reflect) wired together in one
    game -- see games/smallville.sl and DESIGN.md's "Generative Agents architecture"
    section. Doesn't assert on mock-generated text content (the mock provider is a
    random-but-legal baseline, not a coherent narrator -- see providers/mock_provider.py),
    only that every new builtin fires at least once and the run completes cleanly.
    """
    from sociallang.providers.mock_provider import MockProvider

    roster = {f"mock-{i}": (None, MockProvider("mock", None)) for i in range(4)}
    result = run_source(_load("smallville.sl"), roster, seed=1, max_rounds=15)

    assert result["winner"] == "day_complete"
    assert len(result["agents"]) == 4

    from sociallang.lang.interpreter import Interpreter, assign_agents
    from sociallang.lang.parser import parse
    import random

    sim = parse(_load("smallville.sl"))
    agents, roles_by_name = assign_agents(sim, roster, random.Random(1))
    interp = Interpreter(sim, agents, roles_by_name, seed=1)
    interp.run(max_rounds=15)

    kinds = {e.kind for e in interp.events}
    assert "plan" in kinds  # make_plan wrote at least one plan to memory
    assert "dialogue" in kinds  # react() led to at least one converse()
    assert all(a.persona for a in interp.agents)  # settle_in()'s set_persona ran for everyone


def test_generative_memory_pattern_uses_the_configured_embedder():
    from sociallang.providers.embeddings import HashEmbeddingProvider

    provider = FixedProvider("ok")
    roster = {"solo": (None, provider)}
    source = """
    sim GenTest {
      agents: 1
      role Solo { team: "solo" memory: generative(2) sees: none count: 1 }
      phase Speak {
        broadcast("the mafia voted to eliminate P3")
        broadcast("nice weather today")
        for a in alive() {
          ask(a, "who is the mafia?")
        }
      }
      win_condition { return "done" }
      loop { run Speak return check_win() }
    }
    """
    run_source(source, roster, seed=0, max_rounds=1, embedder=HashEmbeddingProvider())
    last_system, last_user = provider.calls[-1]
    assert "the mafia voted to eliminate P3" in last_user  # more relevant than the small-talk event


def test_llm_importance_provider_is_used_to_rate_written_events():
    class ImportanceRatingProvider(ChatProvider):
        def __init__(self):
            super().__init__(model_id="judge", api_key=None)
            self.rated: list[str] = []

        def complete(self, system_prompt, user_prompt, temperature=0.9, max_tokens=500, timeout=60):
            self.rated.append(user_prompt)
            return ProviderResponse(text="7")

    actor = FixedProvider("ok")
    judge = ImportanceRatingProvider()
    roster = {"solo": (None, actor)}
    source = """
    sim ImpTest {
      agents: 1
      role Solo { team: "solo" memory: full_history sees: none count: 1 }
      phase Speak { broadcast("something happened") }
      win_condition { return "done" }
      loop { run Speak return check_win() }
    }
    """
    from sociallang.lang.interpreter import Interpreter, assign_agents
    from sociallang.lang.parser import parse
    import random

    sim = parse(source)
    agents, roles_by_name = assign_agents(sim, roster, random.Random(0))
    interp = Interpreter(sim, agents, roles_by_name, seed=0, importance_provider=judge)
    interp.run(max_rounds=1)

    assert any("something happened" in p for p in judge.rated)
    assert interp.events[0].importance == 0.7
