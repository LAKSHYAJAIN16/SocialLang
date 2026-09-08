from __future__ import annotations

import json

import requests

from .base import ChatProvider, ProviderResponse
from .retry import RETRYABLE_STATUS, with_backoff

API_URL = "https://api.anthropic.com/v1/messages"
ANTHROPIC_VERSION = "2023-06-01"


class AnthropicProvider(ChatProvider):
    def complete(
        self,
        system_prompt: str,
        user_prompt: str,
        temperature: float = 0.9,
        max_tokens: int = 500,
        timeout: int = 60,
    ) -> ProviderResponse:
        if not self.api_key:
            return ProviderResponse(text="", error="missing_api_key")

        headers = {
            "x-api-key": self.api_key,
            "anthropic-version": ANTHROPIC_VERSION,
            "content-type": "application/json",
        }
        body = {
            "model": self.model_id,
            "system": system_prompt,
            "messages": [{"role": "user", "content": user_prompt}],
            "temperature": temperature,
            "max_tokens": max_tokens,
        }

        def call() -> requests.Response:
            resp = requests.post(API_URL, headers=headers, json=body, timeout=timeout)
            if resp.status_code in RETRYABLE_STATUS:
                resp.raise_for_status()
            return resp

        try:
            resp = with_backoff(call)
        except Exception as exc:  # noqa: BLE001
            return ProviderResponse(text="", error=f"request_failed: {exc}")

        # Decode raw bytes as UTF-8 explicitly rather than relying on requests'
        # encoding auto-detection, which has been observed to mis-guess the
        # encoding for some responses and silently corrupt multi-byte characters
        # (e.g. em dashes) into U+FFFD replacement characters.
        body_text = resp.content.decode("utf-8", errors="replace")

        if resp.status_code != 200:
            return ProviderResponse(text="", error=f"http_{resp.status_code}: {body_text[:300]}")

        try:
            data = json.loads(body_text)
        except json.JSONDecodeError as exc:
            return ProviderResponse(text="", error=f"invalid_json_response: {exc}")
        text = "".join(
            block.get("text", "") for block in data.get("content", []) if block.get("type") == "text"
        )
        usage = data.get("usage", {})
        return ProviderResponse(
            text=text,
            prompt_tokens=usage.get("input_tokens", 0),
            completion_tokens=usage.get("output_tokens", 0),
        )
