from __future__ import annotations

import json

import requests

from .base import ChatProvider, ProviderResponse
from .retry import RETRYABLE_STATUS, with_backoff

API_ROOT = "https://generativelanguage.googleapis.com/v1beta/models"


class GoogleProvider(ChatProvider):
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

        url = f"{API_ROOT}/{self.model_id}:generateContent?key={self.api_key}"
        body = {
            "system_instruction": {"parts": [{"text": system_prompt}]},
            "contents": [{"role": "user", "parts": [{"text": user_prompt}]}],
            "generationConfig": {
                "temperature": temperature,
                "maxOutputTokens": max_tokens,
            },
        }

        def call() -> requests.Response:
            resp = requests.post(url, json=body, timeout=timeout)
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
        try:
            candidate = data["candidates"][0]
            parts = candidate.get("content", {}).get("parts", [])
            text = "".join(p.get("text", "") for p in parts)
        except (KeyError, IndexError, TypeError):
            return ProviderResponse(text="", error=f"unexpected_response_shape: {str(data)[:300]}")

        usage = data.get("usageMetadata", {})
        return ProviderResponse(
            text=text,
            prompt_tokens=usage.get("promptTokenCount", 0),
            completion_tokens=usage.get("candidatesTokenCount", 0),
        )
