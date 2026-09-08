from __future__ import annotations

import json
import random
import re

from .base import ChatProvider, ProviderResponse

# Matches the "Alive players: PlayerA, PlayerB, ..." line the prompt builder emits,
# so the mock can pick a plausible-looking random target instead of returning garbage.
_ALIVE_RE = re.compile(r"Alive players:\s*(.+)")
_ACTION_KEYS = ("vote", "target", "save", "investigate")


class MockProvider(ChatProvider):
    """No API key required. Picks legal-looking random actions so the engine can be
    exercised end-to-end (and used as a random-play baseline row on the leaderboard).
    """

    def complete(
        self,
        system_prompt: str,
        user_prompt: str,
        temperature: float = 0.9,
        max_tokens: int = 500,
        timeout: int = 60,
    ) -> ProviderResponse:
        names: list[str] = []
        match = _ALIVE_RE.search(user_prompt)
        if match:
            names = [n.strip() for n in match.group(1).split(",") if n.strip()]

        pool = [
            "I'm not sure who to trust yet.",
            "Something feels off about the last vote.",
            "Let's hear more before deciding.",
            "I don't have a strong read yet.",
        ]
        payload: dict = {"thought": "random baseline move"}
        payload["messages"] = random.sample(pool, k=random.randint(1, 2))
        payload["message"] = payload["messages"][0]  # legacy single-message fallback path
        payload["speech"] = "Mock baseline defense: nothing concrete to add, just playing it safe."
        combined = system_prompt + user_prompt
        for key in _ACTION_KEYS:
            if f'"{key}"' in combined and names:
                payload[key] = random.choice(names)
        if '"summary"' in combined:
            payload["summary"] = "Mock digest: no strong consensus emerged."
        if '"action"' in combined:
            payload["action"] = random.choice(["speak", "think", "pass"])

        return ProviderResponse(text=json.dumps(payload))
