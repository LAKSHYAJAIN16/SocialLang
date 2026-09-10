"""Native memory-retrieval strategies, offered alongside user-defined in-language
`memory name(params) { ... }` blocks (see interpreter.py, which calls whichever kind
a role names with the same (events, query, *params) signature either way).

`generative` is a simplified version of the memory-stream retrieval from Park et al.
2023, "Generative Agents: Interactive Simulacra of Human Behavior" (Stanford) --
https://arxiv.org/abs/2304.03442. The paper scores each memory by recency (exponential
decay) + importance (an LLM-rated 1-10 score assigned at write time) + relevance
(embedding cosine similarity to the current query), min-max normalizes each term, and
retrieves the top-k by weighted sum for use in the agent's next prompt; it also layers
a periodic "reflection" step that synthesizes higher-level insights from a batch of
recent memories, itself stored back into the memory stream as a distinctively
high-importance new entry.

This module keeps that three-factor retrieval shape and the reflection mechanism
(`reflect()` in interpreter.py). `heuristic_importance`/`lexical_relevance` remain
the zero-config deterministic stand-ins (still the default); `llm_importance` and
passing an `embedder` to `retrieve()` are the real upgrades DESIGN.md flagged --
an LLM-rated 1-10 importance score and real embedding cosine similarity (see
sociallang/providers/embeddings.py), both opt-in via CLI flags in cli.py so
existing runs stay free and deterministic unless asked otherwise.
"""

from __future__ import annotations

import math
import re

_IMPORTANT_WORDS = {
    "eliminate", "eliminated", "kill", "killed", "vote", "voted", "accuse", "accused",
    "win", "won", "lose", "lost", "betray", "betrayed", "suspicious", "suspect", "mafia",
    "investigate", "investigated", "protect", "protected", "die", "died", "lied", "lying",
    "trust", "distrust", "reveal", "revealed",
}

_WORD_RE = re.compile(r"[a-zA-Z']+")

# Filtered out of relevance scoring only (not importance) -- otherwise near-universal
# words like "the"/"is" dominate the Jaccard overlap and swamp any topical signal.
_STOPWORDS = {
    "a", "an", "the", "is", "are", "was", "were", "be", "been", "being", "to", "of", "in",
    "on", "at", "for", "and", "or", "but", "not", "this", "that", "it", "as", "by", "with",
    "who", "what", "when", "where", "why", "how", "do", "does", "did", "you", "your", "i",
}


def heuristic_importance(text: str) -> float:
    """0..1 stand-in for the paper's LLM-rated 1-10 importance score: a blend of how
    much of the text is drawn from a small set of game-stakes words, and raw length
    (a longer, more deliberated statement is weakly more likely to matter later).
    """
    words = _WORD_RE.findall(text.lower())
    if not words:
        return 0.0
    hits = sum(1 for w in words if w in _IMPORTANT_WORDS)
    density = hits / len(words)
    length_factor = min(len(words) / 40.0, 1.0)
    return max(0.0, min(1.0, 0.3 * length_factor + 0.7 * min(density * 5, 1.0)))


def lexical_relevance(query: str, text: str) -> float:
    """Jaccard token overlap, standing in for the paper's embedding cosine similarity
    between the current query and each candidate memory.
    """
    q = set(_WORD_RE.findall(query.lower())) - _STOPWORDS
    t = set(_WORD_RE.findall(text.lower())) - _STOPWORDS
    if not q or not t:
        return 0.0
    return len(q & t) / len(q | t)


def recency_score(event_seq: int, now_seq: int, decay: float = 0.995) -> float:
    age = max(now_seq - event_seq, 0)
    return decay**age


_NUMBER_RE = re.compile(r"\d+(?:\.\d+)?")

_IMPORTANCE_SYSTEM_PROMPT = (
    "You rate how important a single event is to remember for future decisions in a "
    "social strategy game, on a scale from 1 (trivial small talk) to 10 (pivotal, e.g. "
    "an elimination, a betrayal, or a vote result). Respond with only the number."
)


def llm_importance(text: str, provider) -> float:
    """0..1 importance score from an LLM rating call, matching the paper's write-time
    importance assignment. `provider` is duck-typed to the same
    `complete(system_prompt, user_prompt, temperature=, max_tokens=) -> response.text`
    shape as ChatProvider (see sociallang/providers/base.py) -- no import needed here,
    consistent with how interpreter.py already calls `agent.provider.complete(...)`
    without depending on any concrete provider class.

    Falls back to `heuristic_importance` if no provider is given, the call errors, or
    its reply doesn't contain a parseable number -- callers never need to handle a
    failure case themselves.
    """
    if provider is None:
        return heuristic_importance(text)
    resp = provider.complete(
        _IMPORTANCE_SYSTEM_PROMPT, f"Event: {text}", temperature=0.0, max_tokens=5,
    )
    match = _NUMBER_RE.search(resp.text or "")
    if match is None:
        return heuristic_importance(text)
    return max(0.0, min(1.0, float(match.group()) / 10.0))


def _embedding_relevances(events: list, query: str, embedder) -> dict[int, float]:
    """Relevance term via real embedding cosine similarity instead of Jaccard token
    overlap. Each event's embedding is computed once and cached on the event object
    (`e.embedding`) since `retrieve()` is called repeatedly over a growing, mostly
    unchanged event list -- without caching, a real API-backed embedder would redo
    O(events) embedding calls on every single ask() over the course of a game.
    Falls back to lexical_relevance wholesale if the embedder can't produce a query
    vector at all (e.g. an API failure), so a flaky embedding provider degrades
    gracefully rather than silently returning zero relevance for every candidate.
    """
    uncached = [e for e in events if e.embedding is None]
    query_vec, *fresh_vecs = embedder.embed([query] + [e.text for e in uncached])
    if query_vec is None:
        return {id(e): lexical_relevance(query, e.text) for e in events}
    for e, vec in zip(uncached, fresh_vecs):
        if vec is not None:
            e.embedding = vec

    relevances = {}
    for e in events:
        relevances[id(e)] = (
            (cosine_similarity(query_vec, e.embedding) + 1.0) / 2.0
            if e.embedding is not None
            else lexical_relevance(query, e.text)
        )
    return relevances


def cosine_similarity(a: list[float], b: list[float]) -> float:
    if not a or not b or len(a) != len(b):
        return 0.0
    dot = sum(x * y for x, y in zip(a, b))
    norm_a = math.sqrt(sum(x * x for x in a))
    norm_b = math.sqrt(sum(y * y for y in b))
    if norm_a == 0.0 or norm_b == 0.0:
        return 0.0
    return dot / (norm_a * norm_b)


def retrieve(events: list, query: str, now_seq: int, k: int, embedder=None) -> list:
    """Top-k events by recency + importance + relevance (equal-weighted sum, matching
    the paper's default), returned back in chronological order so the prompt reads as
    a coherent timeline rather than a relevance-ranked jumble.

    `embedder` (see sociallang/providers/embeddings.py's EmbeddingProvider) swaps the
    relevance term from lexical Jaccard overlap to real embedding cosine similarity;
    omit it (the default) to keep the old zero-config lexical behavior.
    """
    relevances = _embedding_relevances(events, query, embedder) if embedder is not None else None
    scored = []
    for e in events:
        relevance = relevances[id(e)] if relevances is not None else lexical_relevance(query, e.text)
        score = recency_score(e.seq, now_seq) + e.importance + relevance
        scored.append((score, e))
    scored.sort(key=lambda pair: pair[0], reverse=True)
    top = [e for _, e in scored[: max(int(k), 0)]]
    top.sort(key=lambda e: e.seq)
    return top
