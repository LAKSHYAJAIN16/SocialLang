import json

import requests

from sociallang.providers.openai_compat_provider import OpenAICompatProvider

# Built from chr() codepoints (not literal characters or \u escape source text)
# because both were observed to get silently mangled somewhere in this
# environment's own tool-call pipeline before ever reaching this file --
# chr() is pure ASCII digits, so it's immune to that.
EM_DASH_TEXT = "already" + chr(8212) + "let" + chr(8217) + "s hear more"
REPLACEMENT_CHAR = chr(0xFFFD)


class _FakeResponse:
    """Minimal stand-in for requests.Response: .status_code and .content are what
    our provider code reads for the actual response (never .text/.json() there --
    those go through requests' own encoding guess, which is what corrupted
    multi-byte characters like em dashes into U+FFFD in production). .text and
    .raise_for_status() exist only to support the retry-classification path,
    which does legitimately need to inspect status/body before a decode happens.
    """

    def __init__(self, status_code: int, content: bytes):
        self.status_code = status_code
        self.content = content

    @property
    def text(self) -> str:
        return self.content.decode("utf-8", errors="replace")

    def raise_for_status(self) -> None:
        if self.status_code >= 400:
            raise requests.HTTPError(f"{self.status_code} error", response=self)


def test_complete_decodes_utf8_body_correctly(monkeypatch):
    # ensure_ascii=False on both dumps calls matters: real API responses put raw
    # multi-byte UTF-8 characters on the wire, not pre-escaped \uXXXX ASCII text.
    # Using the (accidental) default ensure_ascii=True here would pre-escape the
    # em dash before it ever becomes a byte, making this test unable to exercise
    # the corruption path at all.
    inner_content = json.dumps({"thought": "x", "message": EM_DASH_TEXT}, ensure_ascii=False)
    payload = {
        "choices": [{"message": {"content": inner_content}}],
        "usage": {"prompt_tokens": 1, "completion_tokens": 1},
    }
    body_bytes = json.dumps(payload, ensure_ascii=False).encode("utf-8")

    def fake_post(url, headers=None, json=None, timeout=None):
        return _FakeResponse(200, body_bytes)

    monkeypatch.setattr(requests, "post", fake_post)

    provider = OpenAICompatProvider("some-model", "fake-key", "https://example.com/v1")
    resp = provider.complete("system", "user")

    assert resp.error is None
    assert EM_DASH_TEXT in resp.text
    assert REPLACEMENT_CHAR not in resp.text  # no replacement-character corruption


def test_complete_flags_a_completely_empty_message_as_an_error(monkeypatch):
    # Observed in production with GLM-4.6 via OpenRouter: HTTP 200, well-formed
    # response shape, but message.content is "" -- most likely its entire max_tokens
    # budget was consumed by invisible reasoning tokens. This should surface as an
    # error rather than a silent "successful" empty response.
    payload = {"choices": [{"message": {"content": ""}}], "usage": {"prompt_tokens": 500, "completion_tokens": 500}}
    body_bytes = json.dumps(payload).encode("utf-8")

    def fake_post(url, headers=None, json=None, timeout=None):
        return _FakeResponse(200, body_bytes)

    monkeypatch.setattr(requests, "post", fake_post)

    provider = OpenAICompatProvider("some-model", "fake-key", "https://example.com/v1")
    resp = provider.complete("system", "user")

    assert resp.error == "empty_completion"
    assert resp.text == ""


def test_complete_retries_past_an_openrouter_provider_routing_hiccup(monkeypatch):
    # Observed in production: OpenRouter occasionally routes a request to an
    # upstream that can't actually serve it -- HTTP 400 "does not support endpoint:
    # completions" -- then succeeds seconds later once routed elsewhere. That's a
    # routing hiccup, not a bad request, so it should get retried rather than
    # surfaced as an immediate failure.
    calls = []

    def fake_post(url, headers=None, json=None, timeout=None):  # noqa: A002 - matches requests.post's real signature
        calls.append(1)
        if len(calls) == 1:
            body = (
                b'{"error": {"message": '
                b'"model: some/model does not support endpoint: completions", "code": 400}}'
            )
            return _FakeResponse(400, body)
        import json as json_module  # local import: the `json` param above shadows the module

        payload = {"choices": [{"message": {"content": "ok"}}], "usage": {}}
        return _FakeResponse(200, json_module.dumps(payload).encode("utf-8"))

    monkeypatch.setattr(requests, "post", fake_post)
    import time as time_module

    monkeypatch.setattr(time_module, "sleep", lambda *_: None)

    provider = OpenAICompatProvider("some-model", "fake-key", "https://openrouter.ai/api/v1")
    resp = provider.complete("system", "user")

    assert len(calls) == 2  # retried once past the routing hiccup, then succeeded
    assert resp.error is None
    assert resp.text == "ok"


def test_complete_marks_declared_cache_prefix_for_anthropic_via_openrouter(monkeypatch):
    # A cache_prefix is byte-identical on every call all game; Anthropic doesn't
    # auto-cache like most vendors, so it needs an explicit cache_control
    # breakpoint, passed through by OpenRouter when content is sent as blocks.
    captured = {}
    prefix = "You are playing a social game. Rules: ..."

    def fake_post(url, headers=None, json=None, timeout=None):  # noqa: A002
        captured["body"] = json
        payload = {"choices": [{"message": {"content": "ok"}}], "usage": {}}
        import json as json_module

        return _FakeResponse(200, json_module.dumps(payload).encode("utf-8"))

    monkeypatch.setattr(requests, "post", fake_post)

    provider = OpenAICompatProvider(
        "anthropic/claude-opus-5", "fake-key", "https://openrouter.ai/api/v1", cache_prefix=prefix
    )
    provider.complete(prefix + "You are Player1. Your secret role is: villager.", "user")

    content = captured["body"]["messages"][0]["content"]
    assert isinstance(content, list)
    assert content[0] == {"type": "text", "text": prefix, "cache_control": {"type": "ephemeral"}}
    assert content[1] == {"type": "text", "text": "You are Player1. Your secret role is: villager."}


def test_complete_leaves_system_content_flat_when_no_cache_prefix_declared(monkeypatch):
    captured = {}

    def fake_post(url, headers=None, json=None, timeout=None):  # noqa: A002
        captured["body"] = json
        payload = {"choices": [{"message": {"content": "ok"}}], "usage": {}}
        import json as json_module

        return _FakeResponse(200, json_module.dumps(payload).encode("utf-8"))

    monkeypatch.setattr(requests, "post", fake_post)

    provider = OpenAICompatProvider("anthropic/claude-opus-5", "fake-key", "https://openrouter.ai/api/v1")
    provider.complete("You are Player1. Your secret role is: villager.", "user")

    content = captured["body"]["messages"][0]["content"]
    assert isinstance(content, str)  # no cache_prefix declared -- unaffected


def test_complete_leaves_system_content_flat_for_non_anthropic_models(monkeypatch):
    captured = {}
    prefix = "You are playing a social game. Rules: ..."

    def fake_post(url, headers=None, json=None, timeout=None):  # noqa: A002
        captured["body"] = json
        payload = {"choices": [{"message": {"content": "ok"}}], "usage": {}}
        import json as json_module

        return _FakeResponse(200, json_module.dumps(payload).encode("utf-8"))

    monkeypatch.setattr(requests, "post", fake_post)

    provider = OpenAICompatProvider(
        "mistralai/mistral-large", "fake-key", "https://openrouter.ai/api/v1", cache_prefix=prefix
    )
    provider.complete(prefix + "You are Player1. Your secret role is: villager.", "user")

    content = captured["body"]["messages"][0]["content"]
    assert isinstance(content, str)  # unaffected -- other vendors either auto-cache or are unverified for blocks


def test_complete_does_not_retry_a_routing_hiccup_message_off_openrouter(monkeypatch):
    # The same 400 message from a non-OpenRouter endpoint is just a real client
    # error, not OpenRouter's provider-routing quirk -- don't paper over it.
    calls = []

    def fake_post(url, headers=None, json=None, timeout=None):
        calls.append(1)
        body = b'{"error": {"message": "does not support endpoint: completions", "code": 400}}'
        return _FakeResponse(400, body)

    monkeypatch.setattr(requests, "post", fake_post)

    provider = OpenAICompatProvider("some-model", "fake-key", "https://example.com/v1")
    resp = provider.complete("system", "user")

    assert len(calls) == 1  # no retry -- this isn't OpenRouter
    assert resp.error is not None and resp.error.startswith("http_400")
