import json

import requests

from sociallang.providers.embeddings import (
    GoogleEmbeddingProvider,
    HashEmbeddingProvider,
    OpenAICompatEmbeddingProvider,
    cosine_similarity,
)


class _FakeResponse:
    def __init__(self, status_code: int, content: bytes):
        self.status_code = status_code
        self.content = content

    @property
    def text(self) -> str:
        return self.content.decode("utf-8", errors="replace")

    def raise_for_status(self) -> None:
        if self.status_code >= 400:
            raise requests.HTTPError(f"{self.status_code} error", response=self)


def test_cosine_similarity_of_identical_vectors_is_one():
    assert abs(cosine_similarity([1.0, 2.0, 3.0], [1.0, 2.0, 3.0]) - 1.0) < 1e-9


def test_cosine_similarity_of_orthogonal_vectors_is_zero():
    assert cosine_similarity([1.0, 0.0], [0.0, 1.0]) == 0.0


def test_cosine_similarity_handles_zero_vector_without_dividing_by_zero():
    assert cosine_similarity([0.0, 0.0], [1.0, 2.0]) == 0.0


def test_hash_embedding_is_deterministic_for_the_same_text():
    provider = HashEmbeddingProvider(dims=64)
    a = provider.embed(["the mafia voted to eliminate P3"])[0]
    b = provider.embed(["the mafia voted to eliminate P3"])[0]
    assert a == b


def test_hash_embedding_rates_near_duplicate_text_more_similar_than_unrelated_text():
    provider = HashEmbeddingProvider(dims=64)
    query, close, far = provider.embed(
        ["who is the mafia", "P3 might be mafia based on their vote", "nice weather today"]
    )
    assert cosine_similarity(query, close) > cosine_similarity(query, far)


def test_hash_embedding_never_returns_none():
    provider = HashEmbeddingProvider()
    assert all(v is not None for v in provider.embed(["a", "", "some longer sentence here"]))


def test_openai_compat_embedding_parses_response_sorted_by_index(monkeypatch):
    payload = {
        "data": [
            {"index": 1, "embedding": [4.0, 5.0]},
            {"index": 0, "embedding": [1.0, 2.0]},
        ]
    }
    body_bytes = json.dumps(payload).encode("utf-8")

    def fake_post(url, headers=None, json=None, timeout=None):
        assert url == "https://example.com/v1/embeddings"
        assert json["input"] == ["a", "b"]
        return _FakeResponse(200, body_bytes)

    monkeypatch.setattr(requests, "post", fake_post)

    provider = OpenAICompatEmbeddingProvider("some-embed-model", "fake-key", "https://example.com/v1")
    vectors = provider.embed(["a", "b"])

    assert vectors == [[1.0, 2.0], [4.0, 5.0]]


def test_openai_compat_embedding_returns_all_none_without_an_api_key():
    provider = OpenAICompatEmbeddingProvider("some-embed-model", None, "https://example.com/v1")
    assert provider.embed(["a", "b"]) == [None, None]


def test_openai_compat_embedding_returns_all_none_on_http_error(monkeypatch):
    def fake_post(url, headers=None, json=None, timeout=None):
        return _FakeResponse(400, b'{"error": "bad request"}')

    monkeypatch.setattr(requests, "post", fake_post)

    provider = OpenAICompatEmbeddingProvider("some-embed-model", "fake-key", "https://example.com/v1")
    assert provider.embed(["a", "b"]) == [None, None]


def test_google_embedding_parses_batch_response(monkeypatch):
    payload = {"embeddings": [{"values": [1.0, 2.0]}, {"values": [3.0, 4.0]}]}
    body_bytes = json.dumps(payload).encode("utf-8")

    def fake_post(url, json=None, timeout=None):
        assert "batchEmbedContents" in url
        assert len(json["requests"]) == 2
        return _FakeResponse(200, body_bytes)

    monkeypatch.setattr(requests, "post", fake_post)

    provider = GoogleEmbeddingProvider("text-embedding-004", "fake-key")
    vectors = provider.embed(["a", "b"])

    assert vectors == [[1.0, 2.0], [3.0, 4.0]]


def test_google_embedding_returns_all_none_without_an_api_key():
    provider = GoogleEmbeddingProvider("text-embedding-004", None)
    assert provider.embed(["a", "b"]) == [None, None]
