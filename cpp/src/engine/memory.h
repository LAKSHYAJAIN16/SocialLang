// Memory-stream scoring (recency, importance, relevance), ported from
// sociallang/lang/memory.py: recency (exponential decay by event sequence) +
// importance (a keyword-density heuristic, or LLM-rated) + relevance (lexical
// Jaccard overlap with the query).
#pragma once

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace sl {

double heuristicImportance(std::string_view text);

// Scores `candidates` (event indices) and returns the top k, in sequence order.
// `textOf` / `importanceOf` / `seqOf` read the interpreter's event table.
template <typename TextFn, typename ImpFn>
std::vector<int> retrieveMemory(const std::vector<int>& candidates, std::string_view query, int nowSeq, int k,
                                TextFn textOf, ImpFn importanceOf);

// Lexical relevance (Jaccard overlap of content words) against one query,
// with the query's word set built once rather than per scored event.
struct RelevanceQuery {
  explicit RelevanceQuery(std::string_view query);
  double score(std::string_view text) const;
  std::unordered_set<std::string> words;
};
double recencyScore(int eventSeq, int nowSeq);

}  // namespace sl

#include <algorithm>

namespace sl {

template <typename TextFn, typename ImpFn>
std::vector<int> retrieveMemory(const std::vector<int>& candidates, std::string_view query, int nowSeq, int k,
                                TextFn textOf, ImpFn importanceOf) {
  std::vector<std::pair<double, int>> scored;
  scored.reserve(candidates.size());
  RelevanceQuery rq(query);
  bool hasQuery = !rq.words.empty();
  for (int idx : candidates) {
    double s = recencyScore(idx + 1, nowSeq) + importanceOf(idx);
    if (hasQuery) s += rq.score(textOf(idx));
    scored.emplace_back(s, idx);
  }
  size_t take = std::min(static_cast<size_t>(std::max(k, 0)), scored.size());
  std::partial_sort(scored.begin(), scored.begin() + take, scored.end(),
                    [](auto& a, auto& b) { return a.first > b.first; });
  std::vector<int> top;
  top.reserve(take);
  for (size_t i = 0; i < take; ++i) top.push_back(scored[i].second);
  std::sort(top.begin(), top.end());
  return top;
}

}  // namespace sl
