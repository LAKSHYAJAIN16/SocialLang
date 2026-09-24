#include "ville/config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

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
    // `import name` lines before the block
    std::vector<std::string> imports;
    while (peek().kind == TK::Ident && peek().value == "import") {
      int line = next().line;
      if (peek().line != line || (peek().kind != TK::String && !isWordTok(peek()))) fail("expected a library name after 'import'");
      imports.push_back(next().value);
    }
    CNode n = parseNode();
    n.imports = std::move(imports);
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

CNode parseConfig(const std::string& source) {
  // Editors like Notepad start UTF-8 files with a byte-order mark.
  bool bom = source.size() >= 3 && source.compare(0, 3, "\xEF\xBB\xBF") == 0;
  return Parser(sl::tokenize(bom ? source.substr(3) : source)).parseRoot();
}

namespace {

std::string valueText(const CValue& v) {
  switch (v.kind) {
    case CValue::Str: return quote(v.s);
    case CValue::Num: return fmtNum(v.n);
    case CValue::Range: return fmtNum(v.n) + ".." + fmtNum(v.n2);
    case CValue::List: {
      std::string s = "[";
      for (size_t i = 0; i < v.items.size(); ++i) s += (i ? ", " : "") + valueText(v.items[i]);
      return s + "]";
    }
    default: return v.s;  // Word, Symbol
  }
}

std::string lineText(const std::vector<CValue>& vals) {
  std::string s;
  for (size_t i = 0; i < vals.size(); ++i) {
    // "wake+1..8" stays together; everything else is space-separated.
    bool glue = i > 0 && (vals[i].kind == CValue::Symbol || vals[i - 1].kind == CValue::Symbol);
    s += (i && !glue ? " " : "") + valueText(vals[i]);
  }
  return s;
}

bool isRemove(const std::vector<CValue>& line) { return !line.empty() && line[0].kind == CValue::Word && line[0].s == "remove"; }

// Line entries whose node is a sequence (a day's routine, an activity's
// steps) replace wholesale; others (emoji, importance, free time) are keyed.
bool linesAreSequence(const CNode& n) { return n.kind == "routine" || n.kind == "activity" || n.kind == "everyday"; }

std::string lineKey(const std::vector<CValue>& line) { return line.empty() ? "" : valueText(line[0]); }

bool sameValue(const CValue& a, const CValue& b) { return valueText(a) == valueText(b); }

void writeNode(std::ostringstream& o, const CNode& n, const std::string& ind) {
  o << ind << n.kind;
  for (auto& a : n.args) o << " " << valueText(a);
  bool empty = n.fields.empty() && n.children.empty() && n.lines.empty();
  // Small leaf nodes fit on one line.
  bool inlineBody = n.children.empty() && n.fields.size() + n.lines.size() <= 2 && !empty;
  std::string oneLine;
  if (inlineBody) {
    for (auto& [k, v] : n.fields) oneLine += "  " + k + ": " + valueText(v);
    for (auto& l : n.lines) oneLine += "  " + lineText(l);
    inlineBody = oneLine.size() + ind.size() + n.kind.size() < 110;
  }
  if (inlineBody) {
    o << " {" << oneLine.substr(1) << " }\n";
    return;
  }
  o << " {\n";
  for (auto& [k, v] : n.fields) o << ind << "  " << k << ": " << valueText(v) << "\n";
  for (auto& l : n.lines) o << ind << "  " << lineText(l) << "\n";
  for (auto& c : n.children) writeNode(o, c, ind + "  ");
  o << ind << "}\n";
}

}  // namespace

std::string nodeKey(const CNode& n) {
  std::string k = n.kind;
  for (auto& a : n.args) k += " " + valueText(a);
  return k;
}

void mergeConfig(CNode& base, const CNode& over) {
  if (!over.args.empty()) base.args = over.args;
  for (auto& [k, v] : over.fields) {
    auto it = std::find_if(base.fields.begin(), base.fields.end(), [&](auto& f) { return f.first == k; });
    if (it != base.fields.end()) it->second = v;
    else base.fields.emplace_back(k, v);
  }
  // Removals first: `remove building "Johnson Park"`.
  for (auto& l : over.lines) {
    if (!isRemove(l)) continue;
    std::vector<CValue> rest(l.begin() + 1, l.end());
    std::string key;
    for (size_t i = 0; i < rest.size(); ++i) key += (i ? " " : "") + valueText(rest[i]);
    std::erase_if(base.children, [&](const CNode& c) { return nodeKey(c) == key; });
  }
  std::vector<std::vector<CValue>> lines;
  for (auto& l : over.lines)
    if (!isRemove(l)) lines.push_back(l);
  if (!lines.empty()) {
    if (linesAreSequence(base) || linesAreSequence(over)) {
      base.lines = lines;
    } else {
      for (auto& l : lines) {
        auto it = std::find_if(base.lines.begin(), base.lines.end(), [&](auto& b) { return lineKey(b) == lineKey(l); });
        if (it != base.lines.end()) *it = l;
        else base.lines.push_back(l);
      }
    }
  }
  for (auto& c : over.children) {
    auto it = std::find_if(base.children.begin(), base.children.end(), [&](const CNode& b) { return nodeKey(b) == nodeKey(c); });
    if (it != base.children.end()) mergeConfig(*it, c);
    else base.children.push_back(c);
  }
}

CNode diffConfig(const CNode& base, const CNode& full) {
  CNode d;
  d.kind = full.kind;
  d.args = full.args;
  d.imports = full.imports;
  for (auto& [k, v] : full.fields) {
    const CValue* b = base.field(k);
    if (!b || !sameValue(*b, v)) d.fields.emplace_back(k, v);
  }
  bool linesDiffer = base.lines.size() != full.lines.size();
  for (size_t i = 0; !linesDiffer && i < full.lines.size(); ++i) linesDiffer = lineText(base.lines[i]) != lineText(full.lines[i]);
  if (linesDiffer) {
    if (linesAreSequence(full)) {
      d.lines = full.lines;
    } else {
      for (auto& l : full.lines) {
        auto it = std::find_if(base.lines.begin(), base.lines.end(), [&](auto& b) { return lineKey(b) == lineKey(l); });
        if (it == base.lines.end() || lineText(*it) != lineText(l)) d.lines.push_back(l);
      }
    }
  }
  for (auto& c : full.children) {
    auto it = std::find_if(base.children.begin(), base.children.end(), [&](const CNode& b) { return nodeKey(b) == nodeKey(c); });
    if (it == base.children.end()) {
      d.children.push_back(c);
      continue;
    }
    CNode sub = diffConfig(*it, c);
    if (!sub.fields.empty() || !sub.lines.empty() || !sub.children.empty()) d.children.push_back(sub);
  }
  for (auto& b : base.children) {
    bool kept = std::any_of(full.children.begin(), full.children.end(), [&](const CNode& c) { return nodeKey(c) == nodeKey(b); });
    if (kept) continue;
    std::vector<CValue> rm;
    CValue w;
    w.kind = CValue::Word;
    w.s = "remove";
    rm.push_back(w);
    w.s = b.kind;
    rm.push_back(w);
    for (auto& a : b.args) rm.push_back(a);
    d.lines.push_back(rm);
  }
  return d;
}

std::string writeConfig(const CNode& n) {
  std::ostringstream o;
  for (auto& i : n.imports) o << "import " << i << "\n";
  writeNode(o, n, "");
  return o.str();
}

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
