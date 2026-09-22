#include "engine/memory.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

namespace sl {

namespace {

const std::unordered_set<std::string_view> kImportantWords = {
    "eliminate", "eliminated", "kill", "killed", "vote", "voted", "accuse", "accused",
    "win", "won", "lose", "lost", "betray", "betrayed", "suspicious", "suspect", "mafia",
    "investigate", "investigated", "protect", "protected", "die", "died", "lied", "lying",
    "trust", "distrust", "reveal", "revealed",
};

const std::unordered_set<std::string_view> kStopwords = {
    "a", "an", "the", "is", "are", "was", "were", "be", "been", "being", "to", "of", "in",
    "on", "at", "for", "and", "or", "but", "not", "this", "that", "it", "as", "by", "with",
    "who", "what", "when", "where", "why", "how", "do", "does", "did", "you", "your", "i",
};

// Lowercased runs of [a-z'] -- the reference's `re.findall(r"[a-z']+", text.lower())`.
template <typename Fn>
void forEachWord(std::string_view text, Fn fn) {
  std::string word;
  auto flush = [&] {
    if (!word.empty()) {
      fn(word);
      word.clear();
    }
  };
  for (char c : text) {
    char lc = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    if ((lc >= 'a' && lc <= 'z') || lc == '\'') word += lc;
    else flush();
  }
  flush();
}

std::unordered_set<std::string> contentWords(std::string_view text) {
  std::unordered_set<std::string> out;
  forEachWord(text, [&](const std::string& w) {
    if (!kStopwords.count(w)) out.insert(w);
  });
  return out;
}

}  // namespace

double heuristicImportance(std::string_view text) {
  int words = 0, hits = 0;
  forEachWord(text, [&](const std::string& w) {
    ++words;
    if (kImportantWords.count(w)) ++hits;
  });
  if (words == 0) return 0.0;
  double density = static_cast<double>(hits) / words;
  double lengthFactor = std::min(words / 40.0, 1.0);
  return std::clamp(0.3 * lengthFactor + 0.7 * std::min(density * 5, 1.0), 0.0, 1.0);
}

RelevanceQuery::RelevanceQuery(std::string_view query) : words(contentWords(query)) {}

double RelevanceQuery::score(std::string_view text) const {
  auto t = contentWords(text);
  if (words.empty() || t.empty()) return 0.0;
  size_t inter = 0;
  for (auto& w : words)
    if (t.count(w)) ++inter;
  size_t uni = words.size() + t.size() - inter;
  return static_cast<double>(inter) / static_cast<double>(uni);
}

double recencyScore(int eventSeq, int nowSeq) { return std::pow(0.995, std::max(nowSeq - eventSeq, 0)); }

}  // namespace sl
