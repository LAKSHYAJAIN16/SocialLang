from sociallang.lang.interpreter import Event
from sociallang.lang.memory import heuristic_importance, lexical_relevance, recency_score, retrieve


def test_heuristic_importance_rates_game_stakes_text_higher_than_small_talk():
    high = heuristic_importance("I think we should vote to eliminate the suspicious mafia player.")
    low = heuristic_importance("nice weather today")
    assert high > low


def test_lexical_relevance_is_zero_for_disjoint_vocabulary():
    assert lexical_relevance("who is the mafia", "the weather is nice today") == 0.0


def test_lexical_relevance_is_higher_for_overlapping_vocabulary():
    a = lexical_relevance("who is the mafia", "P3 might be mafia based on their vote")
    b = lexical_relevance("who is the mafia", "the weather is nice today")
    assert a > b


def test_recency_score_decays_with_age():
    fresh = recency_score(event_seq=100, now_seq=100)
    old = recency_score(event_seq=0, now_seq=100)
    assert fresh > old


def test_retrieve_returns_top_k_in_chronological_order():
    events = [
        Event(seq=1, round=1, kind="broadcast", text="P2 voted for P3", author="P2", visible_to=None, importance=0.8),
        Event(seq=2, round=1, kind="broadcast", text="nice weather", author="P1", visible_to=None, importance=0.0),
        Event(seq=3, round=1, kind="broadcast", text="P4 is acting suspicious, might be mafia", author="P3", visible_to=None, importance=0.9),
    ]
    top = retrieve(events, query="who is mafia", now_seq=3, k=2)
    assert [e.seq for e in top] == [1, 3]  # both higher-scoring than the small-talk event, back in seq order
