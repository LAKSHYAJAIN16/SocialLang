#include "app/sl_language.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "engine/ast.h"
#include "engine/builtins.h"
#include "engine/parser.h"
#include "ville/spec.h"

namespace app {

namespace {

// The language's own keywords (engine/lexer.cpp) -- highlighted as keywords.
const char* kGameKeywords[] = {"sim", "agents", "memory", "role", "fn", "phase", "win_condition", "loop", "world",
                               "let", "if", "else", "while", "for", "in", "return", "run", "break",
                               "true", "false", "null", "and", "or", "not"};

// Names the game language defines without a declaration: role fields,
// memory patterns, agent fields, and the variables phases can read.
const char* kGameNames[] = {"agents", "author", "capacity", "cause", "chunks", "count", "death_cause", "dialogue",
                            "events", "full_history", "generative", "goal", "height", "id", "importance", "kind",
                            "location", "max_tokens", "max_turns", "model", "none", "note", "persona", "plan",
                            "query", "radius", "recent", "reflection", "remainder", "round", "seat", "sees", "seq",
                            "steps", "subplan", "tag", "team", "teammates", "temperature", "text", "threshold",
                            "topic", "type", "width", "x", "y"};

// Block words of environment and behavior files -- highlighted as declarations.
const char* kEnvBlocks[] = {"import", "remove", "environment", "style", "type", "building", "room", "bedroom", "resident", "relationship",
                            "event", "news", "generate", "one_per", "routine"};
const char* kBehaviorBlocks[] = {"import", "remove", "behavior", "rules", "routine", "everyday", "free_time", "activity", "emoji",
                                 "importance", "default", "at", "or", "wake", "sleep", "social"};

// Field names (the word before ':').
const char* kEnvFields[] = {"behavior", "start", "start_hour", "days", "seed", "floor", "wall", "size", "bedrooms",
                            "objects", "type", "age", "traits", "background", "currently", "routine", "home",
                            "bedroom", "work", "wakes", "sleeps", "sociability", "note", "closeness", "host",
                            "text", "at", "invite", "day", "hours", "activity", "from", "residents", "first_names",
                            "last_names", "weight", "works_at", "ages", "small", "regular", "large", "yes", "no"};
const char* kBehaviorFields[] = {"chattiness", "time_between_chats", "conversation_length", "strangers_talk",
                                 "news_eagerness", "invite_acceptance", "vision", "attention", "reflect_after"};
// Building types and the placeholders activity text can use.
const char* kKinds[] = {"home", "cafe", "pub", "store", "market", "park", "college", "dorm", "office", "work"};

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Edit distance, counting an adjacent swap ("rotuine") as one edit.
size_t distance(const std::string& a, const std::string& b) {
  size_t n = a.size(), m = b.size();
  std::vector<std::vector<size_t>> d(n + 1, std::vector<size_t>(m + 1));
  for (size_t i = 0; i <= n; ++i) d[i][0] = i;
  for (size_t j = 0; j <= m; ++j) d[0][j] = j;
  for (size_t i = 1; i <= n; ++i)
    for (size_t j = 1; j <= m; ++j) {
      d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
    }
  return d[n][m];
}

// Every identifier outside strings and comments, with its position.
struct Word {
  size_t line, col;
  std::string text;
  bool lineStart;   // first word on its line
  bool beforeColon; // "key:"
};

std::vector<Word> scanWords(const std::string& text) {
  std::vector<Word> out;
  size_t line = 0, col = 0;
  bool first = true;
  for (size_t i = 0; i < text.size();) {
    char c = text[i];
    if (c == '\n') {
      ++line, col = 0, first = true, ++i;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n') ++i;
      continue;
    }
    if (c == '"') {
      ++i, ++col;
      while (i < text.size() && text[i] != '"' && text[i] != '\n') {
        if (text[i] == '\\' && i + 1 < text.size()) ++i, ++col;
        // count characters, not UTF-8 bytes
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++col;
        ++i;
      }
      if (i < text.size() && text[i] == '"') ++i, ++col;
      first = false;
      continue;
    }
    if (identStart(c)) {
      size_t j = i;
      while (j < text.size() && identChar(text[j])) ++j;
      Word w{line, col, text.substr(i, j - i), first, false};
      size_t k = j;
      while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
      w.beforeColon = k < text.size() && text[k] == ':';
      out.push_back(std::move(w));
      col += j - i;
      i = j;
      first = false;
      continue;
    }
    if (c != ' ' && c != '\t' && c != '\r') first = false;
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++col;
    ++i;
  }
  return out;
}

}  // namespace

const TextEditor::Language* slLanguage() {
  static TextEditor::Language lang = [] {
    TextEditor::Language l = *TextEditor::Language::C();  // C-style tokens: // comments, "strings", numbers
    l.name = "SocialLang";
    l.preprocess = 0;
    l.commentStart = "/*";
    l.commentEnd = "*/";
    l.hasSingleQuotedStrings = false;
    l.hasDoubleQuotedStrings = true;
    l.keywords.clear();
    l.declarations.clear();
    l.identifiers.clear();
    for (auto* k : kGameKeywords) l.keywords.insert(k);
    for (auto* k : kEnvBlocks) l.declarations.insert(k);
    for (auto* k : kBehaviorBlocks) l.declarations.insert(k);
    for (auto name : sl::kBuiltinNames) l.identifiers.insert(std::string(name));
    for (auto* k : kEnvFields) l.identifiers.insert(k);
    for (auto* k : kBehaviorFields) l.identifiers.insert(k);
    return l;
  }();
  return &lang;
}

TextEditor::Palette slPalette(bool dark) {
  using C = TextEditor::Color;
  TextEditor::Palette p = dark ? TextEditor::GetDarkPalette() : TextEditor::GetLightPalette();
  auto set = [&](C c, ImU32 v) { p[static_cast<size_t>(c)] = v; };
  if (dark) {
    set(C::background, IM_COL32(30, 30, 30, 255));
    set(C::text, IM_COL32(212, 212, 212, 255));
    set(C::keyword, IM_COL32(197, 134, 192, 255));       // let, if, fn ...
    set(C::declaration, IM_COL32(86, 156, 214, 255));    // environment, building, rules ...
    set(C::knownIdentifier, IM_COL32(156, 220, 254, 255));  // fields and builtins
    set(C::identifier, IM_COL32(220, 220, 170, 255));
    set(C::string, IM_COL32(206, 145, 120, 255));
    set(C::number, IM_COL32(181, 206, 168, 255));
    set(C::comment, IM_COL32(106, 153, 85, 255));
    set(C::punctuation, IM_COL32(180, 180, 180, 255));
  } else {
    set(C::background, IM_COL32(255, 255, 255, 255));
    set(C::text, IM_COL32(30, 30, 30, 255));
    set(C::keyword, IM_COL32(175, 0, 219, 255));
    set(C::declaration, IM_COL32(0, 0, 255, 255));
    set(C::knownIdentifier, IM_COL32(0, 16, 128, 255));
    set(C::identifier, IM_COL32(121, 94, 38, 255));
    set(C::string, IM_COL32(163, 21, 21, 255));
    set(C::number, IM_COL32(9, 134, 88, 255));
    set(C::comment, IM_COL32(0, 128, 0, 255));
    set(C::punctuation, IM_COL32(60, 60, 60, 255));
  }
  return p;
}

const std::vector<std::string>& slVocabulary(const std::string& kind) {
  static std::vector<std::string> env, behavior, game;
  static bool built = false;
  if (!built) {
    built = true;
    for (auto* k : kEnvBlocks) env.push_back(k);
    for (auto* k : kEnvFields) env.push_back(k);
    for (auto* k : kKinds) env.push_back(k);
    for (auto* k : kBehaviorBlocks) behavior.push_back(k);
    for (auto* k : kBehaviorFields) behavior.push_back(k);
    for (auto* k : kKinds) behavior.push_back(k);
    for (auto* k : kGameKeywords) game.push_back(k);
    for (auto name : sl::kBuiltinNames) game.push_back(std::string(name));
    for (auto* k : kGameNames) game.push_back(k);
    for (auto* v : {&env, &behavior, &game}) {
      std::sort(v->begin(), v->end());
      v->erase(std::unique(v->begin(), v->end()), v->end());
    }
  }
  return kind == "environment" ? env : kind == "behavior" ? behavior : game;
}

std::vector<std::string> slSuggestions(const std::string& kind, const std::string& text, const std::string& prefix) {
  std::set<std::string> pool(slVocabulary(kind).begin(), slVocabulary(kind).end());
  for (auto& w : scanWords(text))
    if (w.text.size() > 2) pool.insert(w.text);
  std::vector<std::pair<int, std::string>> scored;
  for (auto& w : pool) {
    if (w == prefix) continue;
    if (w.rfind(prefix, 0) == 0) scored.push_back({0, w});                       // prefix match
    else if (prefix.size() >= 3 && w.find(prefix) != std::string::npos) scored.push_back({1, w});  // contains
    else if (prefix.size() >= 3 && distance(prefix, w.substr(0, std::min(w.size(), prefix.size()))) <= 1)
      scored.push_back({2, w});  // a typo in what's typed so far
  }
  std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) {
    return a.first != b.first ? a.first < b.first : a.second.size() != b.second.size() ? a.second.size() < b.second.size() : a.second < b.second;
  });
  std::vector<std::string> out;
  for (auto& [_, w] : scored) {
    out.push_back(w);
    if (out.size() >= 12) break;
  }
  return out;
}

std::vector<SlTypo> findTypos(const std::string& text, const std::string& kind) {
  const auto& vocab = slVocabulary(kind);
  std::unordered_set<std::string> known(vocab.begin(), vocab.end());
  auto words = scanWords(text);
  // Names this file defines (types, routines, groups, variables) are fine,
  // and so is anything used more than once -- it's probably deliberate.
  std::unordered_map<std::string, int> uses;
  for (auto& w : words) ++uses[w.text];
  for (size_t i = 0; i + 1 < words.size(); ++i)
    if (words[i].text == "type" || words[i].text == "routine" || words[i].text == "rules" || words[i].text == "let" ||
        words[i].text == "fn" || words[i].text == "role" || words[i].text == "phase")
      if (words[i + 1].line == words[i].line) known.insert(words[i + 1].text);  // "routine student {"

  std::vector<SlTypo> out;
  bool structured = kind == "environment" || kind == "behavior";
  for (auto& w : words) {
    if (known.count(w.text) || w.text.size() < 3) continue;
    // In environment / behavior files, the words that must be known are the
    // block words that start a line and the field names before a colon.
    if (structured && !w.lineStart && !w.beforeColon) continue;
    if (!structured && uses[w.text] > 1) continue;
    std::string best;
    size_t bestD = 99;
    for (auto& v : vocab) {
      size_t d = distance(w.text, v);
      if (d < bestD) bestD = d, best = v;
    }
    size_t allowed = w.text.size() >= 6 ? 2 : 1;
    if (bestD == 0 || bestD > allowed) continue;
    out.push_back({w.line, w.col, w.text.size(), w.text, best});
  }
  return out;
}

bool checkSource(const std::string& text, const std::string& kind, const std::string& dir, std::string& message, int& line) {
  message.clear();
  line = 0;
  try {
    if (kind == "environment") ville::parseEnvironment(text, dir);
    else if (kind == "behavior") ville::parseBehavior(text, dir);
    else sl::parseProgram(text);
    return true;
  } catch (const std::exception& e) {
    message = e.what();
    size_t p = message.find("line ");
    if (p != std::string::npos) line = std::atoi(message.c_str() + p + 5);
    return false;
  }
}

}  // namespace app
