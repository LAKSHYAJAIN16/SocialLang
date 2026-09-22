#include "engine/lexer.h"

#include <array>
#include <string_view>
#include <unordered_set>

#include "engine/ast.h"

namespace sl {

namespace {

const std::unordered_set<std::string_view> kKeywords = {
    "sim", "agents", "memory", "role", "fn", "phase", "win_condition", "loop", "world",
    "let", "if", "else", "while", "for", "in", "return", "run", "break",
    "true", "false", "null", "and", "or", "not",
};

// Two-character symbols first, so ".." wins over ".".
constexpr std::array<std::string_view, 22> kSymbols = {
    "..", "==", "!=", "<=", ">=", "{", "}", "(", ")", "[", "]",
    ",", ":", ".", "+", "-", "*", "/", "%", "<", ">", "=",
};

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

}  // namespace

std::vector<Token> tokenize(const std::string& src) {
  std::vector<Token> tokens;
  tokens.reserve(src.size() / 3);
  size_t i = 0, n = src.size();
  int line = 1;

  while (i < n) {
    char ch = src[i];
    if (ch == '\n') { ++line; ++i; continue; }
    if (ch == ' ' || ch == '\t' || ch == '\r') { ++i; continue; }
    if (ch == '/' && i + 1 < n && src[i + 1] == '/') {
      while (i < n && src[i] != '\n') ++i;
      continue;
    }
    if (ch == '"') {
      size_t j = i + 1;
      std::string buf;
      while (j < n && src[j] != '"') {
        if (src[j] == '\\' && j + 1 < n) {
          char esc = src[j + 1];
          buf += esc == 'n' ? '\n' : esc == 't' ? '\t' : esc;
          j += 2;
          continue;
        }
        if (src[j] == '\n') ++line;
        buf += src[j++];
      }
      if (j >= n) throw ParseError("line " + std::to_string(line) + ": unterminated string");
      tokens.push_back({TK::String, std::move(buf), line});
      i = j + 1;
      continue;
    }
    if (isDigit(ch)) {
      size_t j = i;
      while (j < n && isDigit(src[j])) ++j;
      if (j + 1 < n && src[j] == '.' && isDigit(src[j + 1])) {
        ++j;
        while (j < n && isDigit(src[j])) ++j;
      }
      tokens.push_back({TK::Number, src.substr(i, j - i), line});
      i = j;
      continue;
    }
    if (isAlpha(ch) || ch == '_') {
      size_t j = i;
      while (j < n && (isAlpha(src[j]) || isDigit(src[j]) || src[j] == '_')) ++j;
      std::string word = src.substr(i, j - i);
      TK kind = kKeywords.count(word) ? TK::Keyword : TK::Ident;
      tokens.push_back({kind, std::move(word), line});
      i = j;
      continue;
    }
    bool matched = false;
    for (auto sym : kSymbols) {
      if (src.compare(i, sym.size(), sym) == 0) {
        tokens.push_back({TK::Symbol, std::string(sym), line});
        i += sym.size();
        matched = true;
        break;
      }
    }
    if (matched) continue;
    throw ParseError("line " + std::to_string(line) + ": unexpected character '" + std::string(1, ch) + "'");
  }
  tokens.push_back({TK::End, "", line});
  return tokens;
}

}  // namespace sl
