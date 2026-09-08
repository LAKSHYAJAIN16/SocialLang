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
