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
(`reflect()` in interpreter.py), but swaps the two paper components that require extra
LLM calls or an embedding index for cheap deterministic stand-ins: importance is a
lexical heuristic instead of an LLM-rated score, and relevance is token-overlap
(Jaccard) instead of embedding similarity. Both are the natural place to plug in a
real embedding provider and an LLM importance-rating call later without changing the
retrieval formula's shape.
"""

from __future__ import annotations

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


def retrieve(events: list, query: str, now_seq: int, k: int) -> list:
    """Top-k events by recency + importance + relevance (equal-weighted sum, matching
    the paper's default), returned back in chronological order so the prompt reads as
    a coherent timeline rather than a relevance-ranked jumble.
    """
    scored = []
    for e in events:
        score = recency_score(e.seq, now_seq) + e.importance + lexical_relevance(query, e.text)
        scored.append((score, e))
    scored.sort(key=lambda pair: pair[0], reverse=True)
    top = [e for _, e in scored[: max(int(k), 0)]]
    top.sort(key=lambda e: e.seq)
    return top
