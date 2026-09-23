#include "ville/config.h"

#include <cmath>
#include <cstdio>

#include "engine/ast.h"
#include "engine/lexer.h"

namespace ville {

using sl::TK;
using sl::Token;

std::string CValue::text() const {
  if (kind == Num) return fmtNum(n);
  return s;
}

const CValue* CNode::field(const std::string& key) const {
  for (auto& [k, v] : fields)
    if (k == key) return &v;
  return nullptr;
}

std::string CNode::str(const std::string& key, const std::string& fallback) const {
  const CValue* v = field(key);
  return v ? v->text() : fallback;
}

double CNode::num(const std::string& key, double fallback) const {
  const CValue* v = field(key);
  return v && v->kind == CValue::Num ? v->n : fallback;
}

std::string CNode::arg(size_t i, const std::string& fallback) const {
  return i < args.size() ? args[i].text() : fallback;
}

namespace {

class Parser {
 public:
  explicit Parser(std::vector<Token> t) : toks_(std::move(t)) {}

  CNode parseRoot() {
    CNode n = parseNode();
    if (peek().kind != TK::End) fail("unexpected '" + peek().value + "' after the closing '}'");
    return n;
  }

 private:
  std::vector<Token> toks_;
  size_t pos_ = 0;

  const Token& peek(size_t k = 0) const { return toks_[std::min(pos_ + k, toks_.size() - 1)]; }
  const Token& next() {
    const Token& t = toks_[pos_];
    if (t.kind != TK::End) ++pos_;
    return t;
  }
  bool isSym(const Token& t, const char* s) const { return t.kind == TK::Symbol && t.value == s; }
  [[noreturn]] void fail(const std::string& msg) const {
    throw sl::ParseError("line " + std::to_string(peek().line) + ": " + msg);
  }
  bool isWordTok(const Token& t) const { return t.kind == TK::Ident || t.kind == TK::Keyword; }

  CValue value() {
    const Token& t = next();
    CValue v;
    v.line = t.line;
    if (t.kind == TK::String) {
      v.kind = CValue::Str;
      v.s = t.value;
    } else if (t.kind == TK::Number || isSym(t, "-")) {
      double sign = 1;
      std::string numText = t.value;
      if (isSym(t, "-")) {
        sign = -1;
        numText = next().value;
      }
      v.kind = CValue::Num;
      v.n = sign * std::stod(numText);
      // 9..12 is a range; 13..sleep is a number, "..", and a word (routines).
      if (isSym(peek(), "..") && peek(1).kind == TK::Number) {
        next();
        v.kind = CValue::Range;
        v.n2 = std::stod(next().value);
      }
    } else if (isWordTok(t)) {
      v.kind = CValue::Word;
      v.s = t.value;
    } else if (isSym(t, "[")) {
      v.kind = CValue::List;
      while (!isSym(peek(), "]")) {
        if (peek().kind == TK::End) fail("unterminated list");
        v.items.push_back(value());
        if (isSym(peek(), ",")) next();
      }
      next();
    } else if (t.kind == TK::Symbol) {
      v.kind = CValue::Symbol;
      v.s = t.value;
    } else {
      fail("unexpected end of file");
    }
    return v;
  }

  CNode parseNode() {
    CNode n;
    const Token& head = next();
    if (!isWordTok(head)) fail("expected a block name, got '" + head.value + "'");
    n.kind = head.value;
    n.line = head.line;
    while (!isSym(peek(), "{")) {
      if (peek().kind == TK::End || peek().line != n.line) break;  // a block with no body
      n.args.push_back(value());
    }
    if (!isSym(peek(), "{")) return n;
    next();
    while (!isSym(peek(), "}")) {
      if (peek().kind == TK::End) fail("missing '}' for " + n.kind + " opened on line " + std::to_string(n.line));
      const Token& t = peek();
      // field -- `key: value`
      if (isWordTok(t) && isSym(peek(1), ":")) {
        std::string key = next().value;
        next();
        n.fields.emplace_back(key, value());
        continue;
      }
      // nested node -- a word whose line contains '{'
      bool hasBrace = false;
      for (size_t k = 0; peek(k).kind != TK::End && peek(k).line == t.line; ++k) {
        if (isSym(peek(k), "{")) {
          hasBrace = true;
          break;
        }
        if (isSym(peek(k), "}")) break;
      }
      if (isWordTok(t) && hasBrace) {
        n.children.push_back(parseNode());
        continue;
      }
      // line entry -- every token on this line
      std::vector<CValue> entry;
      int line = t.line;
      while (peek().kind != TK::End && peek().line == line && !isSym(peek(), "}")) entry.push_back(value());
      n.lines.push_back(std::move(entry));
    }
    next();
    return n;
  }
};

}  // namespace

CNode parseConfig(const std::string& source) { return Parser(sl::tokenize(source)).parseRoot(); }

std::string quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') {
      out += "\\n";
      continue;
    }
    out += c;
  }
  return out + "\"";
}

std::string fmtNum(double n) {
  if (n == std::floor(n) && std::fabs(n) < 1e15) return std::to_string(static_cast<long long>(n));
  char buf[32];
  std::snprintf(buf, sizeof buf, "%g", n);
  return buf;
}

}  // namespace ville
