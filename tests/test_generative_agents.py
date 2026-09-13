"""Tests for the Generative Agents (Park et al. 2023, "Generative Agents: Interactive
Simulacra of Human Behavior") machinery beyond the memory stream itself: persona,
recursive plan decomposition, reacting, dialogue generation, and threshold-triggered
("hierarchical") reflection. See DESIGN.md's "Generative Agents architecture" section.

Interpreters here are built directly (Agent objects constructed by hand, not via
assign_agents' seeded-but-shuffled roster assignment) so each test controls exactly
which ChatProvider is attached to which agent -- these builtins are exercised as
Python methods through `interp.builtins[name](args, kwargs)`, the same dict every
`.sl` Call node dispatches through, so this is still testing the real dispatch path.
"""

import random

from sociallang.lang.interpreter import Agent, Interpreter
from sociallang.lang.parser import parse
from sociallang.providers.base import ChatProvider, ProviderResponse

SIM_SOURCE = """
sim GenAgentTest {
  agents: 2
  role Player { team: "player" memory: full_history sees: none count: remainder }
  win_condition { return "done" }
  loop { return check_win() }
}
"""


class FixedProvider(ChatProvider):
    """Always answers with the same fixed text; records every call for inspection."""

    def __init__(self, text: str):
        super().__init__(model_id="fixed", api_key=None)
        self.text = text
        self.calls: list[tuple[str, str]] = []

    def complete(self, system_prompt, user_prompt, temperature=0.9, max_tokens=500, timeout=60):
        self.calls.append((system_prompt, user_prompt))
        return ProviderResponse(text=self.text)


class ScriptedProvider(ChatProvider):
    """Returns each of `responses` in order, one per complete() call."""

    def __init__(self, responses: list[str]):
        super().__init__(model_id="scripted", api_key=None)
        self._responses = list(responses)
        self.calls: list[tuple[str, str]] = []

    def complete(self, system_prompt, user_prompt, temperature=0.9, max_tokens=500, timeout=60):
        self.calls.append((system_prompt, user_prompt))
        assert self._responses, "ScriptedProvider ran out of scripted responses"
        return ProviderResponse(text=self._responses.pop(0))


def _one_agent_interp(provider: ChatProvider):
    sim = parse(SIM_SOURCE)
    roles_by_name = {r.name: r for r in sim.roles}
    agent = Agent(seat="P1", role_name="Player", team="player", model_key="a", provider=provider)
    interp = Interpreter(sim, [agent], roles_by_name, seed=0)
    return interp, agent


def _two_agent_interp(provider_a: ChatProvider, provider_b: ChatProvider):
    sim = parse(SIM_SOURCE)
    roles_by_name = {r.name: r for r in sim.roles}
    a = Agent(seat="P1", role_name="Player", team="player", model_key="a", provider=provider_a)
    b = Agent(seat="P2", role_name="Player", team="player", model_key="b", provider=provider_b)
    interp = Interpreter(sim, [a, b], roles_by_name, seed=0)
    return interp, (a, b)


def test_set_persona_is_folded_into_the_agent_identity_prompt():
    provider = FixedProvider("ok")
    interp, agent = _one_agent_interp(provider)
    interp.builtins["set_persona"]([agent, "A grumpy baker who distrusts strangers."], {})
    interp.builtins["ask"]([agent, "How are you?"], {})
    system_prompt, _ = provider.calls[-1]
    assert "grumpy baker" in system_prompt


def test_make_plan_parses_time_activity_lines_into_agent_plan():
    provider = FixedProvider("7am: wake up\n9am: go to the bakery\nnoon: eat lunch")
    interp, agent = _one_agent_interp(provider)
    plan = interp.builtins["make_plan"]([agent, "Plan your day."], {"steps": 3})
    assert plan == agent.plan
    assert [s["time"] for s in plan] == ["7am", "9am", "noon"]
    assert [s["activity"] for s in plan] == ["wake up", "go to the bakery", "eat lunch"]
    assert agent.plan_cursor == 0
    assert interp.events[-1].kind == "plan"
    assert "go to the bakery" in interp.events[-1].text


def test_decompose_step_and_advance_plan_walk_the_recursive_plan_cursor():
    interp, agent = _one_agent_interp(FixedProvider("9am: write the research report"))
    interp.builtins["make_plan"]([agent, "Plan"], {"steps": 1})
    assert interp.builtins["current_step"]([agent], {})["activity"] == "write the research report"
    assert interp.builtins["current_action"]([agent], {}) == "write the research report"

    # Swap in a provider that returns the fine-grained breakdown for decompose_step.
    agent.provider = FixedProvider("open the laptop\ndraft the intro\nwrite the results section")
    subplan = interp.builtins["decompose_step"]([agent], {"chunks": 3})
    assert subplan == ["open the laptop", "draft the intro", "write the results section"]
    assert interp.builtins["current_action"]([agent], {}) == "open the laptop"

    interp.builtins["advance_plan"]([agent], {})
    assert interp.builtins["current_action"]([agent], {}) == "draft the intro"
    interp.builtins["advance_plan"]([agent], {})
    assert interp.builtins["current_action"]([agent], {}) == "write the results section"

    # Subplan exhausted -- the next advance falls through to the top-level cursor,
    # which can't move further since there's only one top-level step.
    interp.builtins["advance_plan"]([agent], {})
    assert agent.subplan == []
    assert agent.plan_cursor == 0


def test_react_returns_none_on_continue_and_the_new_action_otherwise():
    interp, agent = _one_agent_interp(FixedProvider("CONTINUE."))
    assert interp.builtins["react"]([agent, "P2 waves at you."], {}) is None

    interp, agent = _one_agent_interp(FixedProvider("Walk over and say hello to them."))
    result = interp.builtins["react"]([agent, "P2 waves at you."], {})
    assert result == "Walk over and say hello to them."
    assert any("observed: P2 waves" in e.text for e in interp.events)


def test_converse_alternates_turns_and_stops_on_farewell():
    provider_a = ScriptedProvider(["Hey, nice weather today.", "Talk to you later!"])
    provider_b = ScriptedProvider(["Sure is. See you around."])
    interp, (a, b) = _two_agent_interp(provider_a, provider_b)

    transcript = interp.builtins["converse"]([a, b], {"max_turns": 4})

    assert transcript == [
        "P1: Hey, nice weather today.",
        "P2: Sure is. See you around.",
    ]
    dialogue_events = [e for e in interp.events if e.kind == "dialogue"]
    assert len(dialogue_events) == 2
    assert dialogue_events[0].visible_to == {"P1", "P2"}
    assert dialogue_events[1].visible_to == {"P1", "P2"}
    # b's farewell ended the exchange before a's second scripted line was needed.
    assert provider_a._responses == ["Talk to you later!"]


def test_converse_stops_at_max_turns_when_nobody_says_goodbye():
    provider_a = ScriptedProvider(["Nice day.", "Indeed."])
    provider_b = ScriptedProvider(["Sure is.", "Agreed."])
    interp, (a, b) = _two_agent_interp(provider_a, provider_b)

    transcript = interp.builtins["converse"]([a, b], {"max_turns": 2})

    assert len(transcript) == 4  # 2 max_turns * 2 speakers, none of them said goodbye


def test_maybe_reflect_only_fires_once_importance_threshold_is_crossed():
    interp, agent = _one_agent_interp(FixedProvider("ignored"))
    interp._append_event(kind="note", text="minor thing", author=agent.seat, visible_to={agent.seat}, importance=0.1)

    assert interp.builtins["maybe_reflect"]([agent], {"threshold": 1.0}) == []

    interp._append_event(
        kind="note", text="something big happened", author=agent.seat, visible_to={agent.seat}, importance=1.5,
    )
    reflect_provider = FixedProvider("Reflection insight one\nReflection insight two")
    agent.provider = reflect_provider
    insights = interp.builtins["maybe_reflect"]([agent], {"threshold": 1.0})

    assert insights == ["Reflection insight one", "Reflection insight two"]
    assert any(e.kind == "reflection" and e.importance == 0.9 for e in interp.events)
    # last_reflect_seq advanced past both prior events, so they can't re-trigger a
    # second reflection even against a much lower threshold.
    assert interp.builtins["maybe_reflect"]([agent], {"threshold": 0.01}) == []
