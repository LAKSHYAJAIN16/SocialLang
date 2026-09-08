from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass


@dataclass
class ProviderResponse:
    text: str
    prompt_tokens: int = 0
    completion_tokens: int = 0
    cost_usd: float = 0.0  # exact cost if the provider reports one (e.g. OpenRouter), else 0.0
    error: str | None = None


class ChatProvider(ABC):
    """Stateless single-turn completion interface every provider adapter implements.

    Each game turn is issued as one fresh (system_prompt, user_prompt) pair rather
    than a maintained multi-turn history, so every provider's differing message
    format only has to be handled in one place per adapter.
    """

    def __init__(self, model_id: str, api_key: str | None = None):
        self.model_id = model_id
        self.api_key = api_key

    @abstractmethod
    def complete(
        self,
        system_prompt: str,
        user_prompt: str,
        temperature: float = 0.9,
        max_tokens: int = 500,
        timeout: int = 60,
    ) -> ProviderResponse:
        ...
