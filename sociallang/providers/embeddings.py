"""Embedding providers for the `generative(k)` memory pattern's relevance term
(see sociallang/lang/memory.py). DESIGN.md flagged the pre-existing lexical
(Jaccard token overlap) relevance score as a stand-in for the paper's embedding
cosine similarity -- this module is that real embedding provider.

Three implementations:

- `HashEmbeddingProvider`: a deterministic, offline, no-API-key bag-of-words
  feature-hashing embedding. It's a genuine vector-space embedding (real cosine
  similarity, not token overlap), just built from hashed n-grams instead of a
  trained model -- the natural default for --mock-only runs and CI, and a
  reasonable default even for real runs when no embedding key is configured.
- `OpenAICompatEmbeddingProvider`: calls an OpenAI-style POST /embeddings
  endpoint (OpenAI itself, or any compatible host).
- `GoogleEmbeddingProvider`: calls Google's batchEmbedContents endpoint.

All three share the same duck-typed contract: `embed(texts) -> list[vector | None]`,
one entry per input text in order, `None` where that text's embedding could not
be produced (e.g. a request failure) -- mirroring how ChatProvider.complete()
reports a per-call error rather than raising.
"""

from __future__ import annotations

import hashlib
import json
import math
import re
from abc import ABC, abstractmethod

import requests

from .retry import RETRYABLE_STATUS, with_backoff

_WORD_RE = re.compile(r"[a-zA-Z']+")


class EmbeddingProvider(ABC):
    @abstractmethod
    def embed(self, texts: list[str]) -> list[list[float] | None]:
        """Returns one embedding vector per input text, in the same order. An
        entry is None if that text's embedding couldn't be computed.
        """


def cosine_similarity(a: list[float], b: list[float]) -> float:
    if not a or not b or len(a) != len(b):
        return 0.0
    dot = sum(x * y for x, y in zip(a, b))
    norm_a = math.sqrt(sum(x * x for x in a))
    norm_b = math.sqrt(sum(y * y for y in b))
    if norm_a == 0.0 or norm_b == 0.0:
        return 0.0
    return dot / (norm_a * norm_b)


class HashEmbeddingProvider(EmbeddingProvider):
    """Feature-hashing bag-of-words embedding: each token is hashed (stably, via
    md5 -- not Python's `hash()`, which is randomized per-process for strings)
    into one of `dims` buckets, incremented, and the resulting vector is
    L2-normalized. Cheap, deterministic, needs no network or model -- a real
    point in an embedding space rather than a token-overlap heuristic, even
    though it has no learned semantics.
    """

    def __init__(self, dims: int = 256):
        self.dims = dims

    def _vector(self, text: str) -> list[float]:
        vec = [0.0] * self.dims
        for word in _WORD_RE.findall(text.lower()):
            idx = int(hashlib.md5(word.encode("utf-8")).hexdigest(), 16) % self.dims
            vec[idx] += 1.0
        norm = math.sqrt(sum(v * v for v in vec))
        if norm == 0.0:
            return vec
        return [v / norm for v in vec]

    def embed(self, texts: list[str]) -> list[list[float] | None]:
        return [self._vector(t) for t in texts]


class OpenAICompatEmbeddingProvider(EmbeddingProvider):
    """Works with any provider exposing an OpenAI-style POST /embeddings endpoint."""

    def __init__(self, model_id: str, api_key: str | None, base_url: str):
        self.model_id = model_id
        self.api_key = api_key
        self.base_url = base_url.rstrip("/")

    def embed(self, texts: list[str]) -> list[list[float] | None]:
        if not self.api_key or not texts:
            return [None] * len(texts)

        headers = {"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"}
        body = {"model": self.model_id, "input": texts}

        def call() -> requests.Response:
            resp = requests.post(f"{self.base_url}/embeddings", headers=headers, json=body, timeout=60)
            if resp.status_code in RETRYABLE_STATUS:
                resp.raise_for_status()
            return resp

        try:
            resp = with_backoff(call)
        except Exception:  # noqa: BLE001
            return [None] * len(texts)

        if resp.status_code != 200:
            return [None] * len(texts)

        try:
            data = json.loads(resp.content.decode("utf-8", errors="replace"))
            entries = sorted(data["data"], key=lambda e: e["index"])
            vectors = [e["embedding"] for e in entries]
        except (KeyError, TypeError, ValueError, json.JSONDecodeError):
            return [None] * len(texts)

        if len(vectors) != len(texts):
            return [None] * len(texts)
        return vectors


class GoogleEmbeddingProvider(EmbeddingProvider):
    """Calls Google's batchEmbedContents endpoint (one HTTP call for the whole batch)."""

    API_ROOT = "https://generativelanguage.googleapis.com/v1beta/models"

    def __init__(self, model_id: str, api_key: str | None):
        self.model_id = model_id
        self.api_key = api_key

    def embed(self, texts: list[str]) -> list[list[float] | None]:
        if not self.api_key or not texts:
            return [None] * len(texts)

        url = f"{self.API_ROOT}/{self.model_id}:batchEmbedContents?key={self.api_key}"
        body = {
            "requests": [
                {"model": f"models/{self.model_id}", "content": {"parts": [{"text": t}]}} for t in texts
            ]
        }

        def call() -> requests.Response:
            resp = requests.post(url, json=body, timeout=60)
            if resp.status_code in RETRYABLE_STATUS:
                resp.raise_for_status()
            return resp

        try:
            resp = with_backoff(call)
        except Exception:  # noqa: BLE001
            return [None] * len(texts)

        if resp.status_code != 200:
            return [None] * len(texts)

        try:
            data = json.loads(resp.content.decode("utf-8", errors="replace"))
            vectors = [e["values"] for e in data["embeddings"]]
        except (KeyError, TypeError, ValueError, json.JSONDecodeError):
            return [None] * len(texts)

        if len(vectors) != len(texts):
            return [None] * len(texts)
        return vectors
