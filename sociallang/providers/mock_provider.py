from __future__ import annotations

import random
import re

from .base import ChatProvider, ProviderResponse

# Matches the "Respond with exactly one of: A, B, C" line ask_choice() appends to its
# prompt (see interpreter.py's _bi_ask_choice), so the mock can pick a legal option
# instead of free text that would fail every game's option-matching.
_OPTIONS_RE = re.compile(r"Respond with exactly one of:\s*(.+)")

_FREE_TEXT_POOL = [
    "I'm not sure yet, let's see how this plays out.",
    "Something about this doesn't add up to me.",
    "I'll go along with the group for now.",
    "Let's hear more before anyone decides anything.",
]


class MockProvider(ChatProvider):
    """No API key required. Picks a legal option when the prompt names one (so
    ask_choice() always gets a parseable answer), otherwise returns a generic line --
    lets a SocialLang program run end-to-end as a random-play baseline.
    """

    def complete(
        self,
        system_prompt: str,
        user_prompt: str,
        temperature: float = 0.9,
        max_tokens: int = 500,
        timeout: int = 60,
    ) -> ProviderResponse:
        match = _OPTIONS_RE.search(user_prompt)
        if match:
            options = [o.strip() for o in match.group(1).split(",") if o.strip()]
            if options:
                return ProviderResponse(text=random.choice(options))
        return ProviderResponse(text=random.choice(_FREE_TEXT_POOL))
