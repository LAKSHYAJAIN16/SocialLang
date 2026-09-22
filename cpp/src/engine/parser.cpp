// Recursive-descent parser, a line-for-line port of sociallang/lang/parser.py
// (same precedence ladder: or < and < not < comparison < additive <
// multiplicative < unary < postfix < primary), plus a link pass that resolves
// every call and `run` to an index.
#include "engine/parser.h"

#include <cmath>
#include <unordered_map>

#include "engine/builtins.h"
#include "engine/lexer.h"

namespace sl {

SymbolTable::SymbolTable() {
  static const char* kFixed[S_COUNT_] = {
      "seat", "role", "team", "alive", "model", "death_cause", "x", "y", "location",
      "persona", "plan", "subplan", "text", "author", "kind", "seq", "round",
      "importance", "id", "type", "tag", "capacity",
      "temperature", "max_tokens", "threshold", "goal", "steps", "chunks", "topic",
      "max_turns", "cause", "radius",
      "agents", "events", "query",
  };
  for (const char* name : kFixed) intern(name);
}

int SymbolTable::intern(const std::string& name) {
  auto it = ids_.find(name);
  if (it != ids_.end()) return it->second;
  int id = static_cast<int>(names_.size());
  names_.push_back(name);
  ids_.emplace(name, id);
  return id;
}

namespace {

using ExprP = std::unique_ptr<Expr>;
using StmtP = std::unique_ptr<Stmt>;

class Parser {
 public:
  Parser(std::vector<Token> tokens, Program& prog) : toks_(std::move(tokens)), prog_(prog) {}

  void parseProgram() {
    expect(TK::Keyword, "sim");
    prog_.name = expect(TK::Ident).value;
    expect(TK::Symbol, "{");
    while (!check(TK::Symbol, "}")) {
      if (isKw("agents")) {
        advance();
        expect(TK::Symbol, ":");
        int lo = intLit();
        int hi = lo;
        if (match(TK::Symbol, "..")) hi = intLit();
        prog_.agentsMin = lo;
        prog_.agentsMax = hi;
      } else if (isKw("memory")) {
        prog_.memoryDecls.push_back(parseFnLike(/*requireParens=*/false));
      } else if (isKw("role")) {
        prog_.roles.push_back(parseRole());
      } else if (isKw("fn")) {
        prog_.fns.push_back(parseFnLike(/*requireParens=*/true));
      } else if (isKw("phase")) {
        advance();
        PhaseDecl p;
        p.name = expect(TK::Ident).value;
        p.body = parseBlock();
        prog_.phases.push_back(std::move(p));
      } else if (isKw("win_condition")) {
        advance();
        prog_.winCondition = parseBlock();
      } else if (isKw("loop")) {
        advance();
        prog_.loop = parseBlock();
      } else if (isKw("world")) {
        prog_.world = parseWorld();
      } else {
        fail("unexpected token '" + peek().value + "' in sim body");
      }
    }
    expect(TK::Symbol, "}");
  }

 private:
  std::vector<Token> toks_;
  size_t pos_ = 0;
  Program& prog_;

  const Token& peek(size_t ahead = 0) const {
    size_t i = pos_ + ahead;
    return toks_[i < toks_.size() ? i : toks_.size() - 1];
  }
  const Token& advance() {
    const Token& t = toks_[pos_];
    if (t.kind != TK::End) ++pos_;
    return t;
  }
  bool check(TK kind, const char* value = nullptr) const {
    const Token& t = peek();
    return t.kind == kind && (!value || t.value == value);
  }
  bool match(TK kind, const char* value = nullptr) {
    if (!check(kind, value)) return false;
    advance();
    return true;
  }
  const Token& expect(TK kind, const char* value = nullptr) {
    if (!check(kind, value)) {
      static const char* kNames[] = {"NUMBER", "STRING", "KEYWORD", "IDENT", "SYMBOL", "EOF"};
      std::string want = value ? value : kNames[static_cast<int>(kind)];
      fail("expected '" + want + "', got '" + peek().value + "'");
    }
    return advance();
  }
  bool isKw(const char* w) const { return check(TK::Keyword, w); }
  [[noreturn]] void fail(const std::string& msg) const {
    throw ParseError("line " + std::to_string(peek().line) + ": " + msg);
  }
  int intLit() { return static_cast<int>(std::trunc(std::stod(expect(TK::Number).value))); }

  ExprP node(EK k, int line) {
    auto e = std::make_unique<Expr>();
    e->k = k;
    e->line = line;
    return e;
  }

  FnDecl parseFnLike(bool requireParens) {
    advance();
    FnDecl f;
    f.name = expect(TK::Ident).value;
    bool open = requireParens ? (expect(TK::Symbol, "("), true) : match(TK::Symbol, "(");
    if (open) {
      while (!check(TK::Symbol, ")")) {
        f.params.push_back(prog_.syms.intern(expect(TK::Ident).value));
        if (!match(TK::Symbol, ",")) break;
      }
      expect(TK::Symbol, ")");
    }
    f.body = parseBlock();
    return f;
  }

  RoleDecl parseRole() {
    advance();
    RoleDecl r;
    r.name = expect(TK::Ident).value;
    r.team = r.name;
    expect(TK::Symbol, "{");
    while (!check(TK::Symbol, "}")) {
      std::string field = advance().value;
      expect(TK::Symbol, ":");
      if (field == "team") {
        r.team = expect(TK::String).value;
      } else if (field == "memory") {
        r.memoryName = expect(TK::Ident).value;
        if (match(TK::Symbol, "(")) {
          while (!check(TK::Symbol, ")")) {
            r.memoryArgs.push_back(parseExpr());
            if (!match(TK::Symbol, ",")) break;
          }
          expect(TK::Symbol, ")");
        }
      } else if (field == "sees") {
        r.sees = advance().value;
      } else if (field == "count") {
        if (peek().kind == TK::Ident && peek().value == "remainder") {
          advance();
          r.count = -1;
        } else {
          r.count = intLit();
        }
      } else {
        fail("unknown role field '" + field + "'");
      }
      match(TK::Symbol, ",");
    }
    expect(TK::Symbol, "}");
    return r;
  }

  std::unique_ptr<WorldDecl> parseWorld() {
    advance();
    expect(TK::Symbol, "{");
    auto w = std::make_unique<WorldDecl>();
    while (!check(TK::Symbol, "}")) {
      if (peek().kind == TK::Ident && peek().value == "location") {
        advance();
        LocationTypeDecl lt;
        lt.name = expect(TK::Ident).value;
        expect(TK::Symbol, "{");
        while (!check(TK::Symbol, "}")) {
          std::string field = advance().value;
          expect(TK::Symbol, ":");
          if (field == "tag") {
            lt.tag = expect(TK::String).value;
            lt.hasTag = true;
          } else if (field == "capacity") {
            lt.capacity = intLit();
          } else if (field == "count") {
            lt.count = intLit();
          } else {
            fail("unknown location field '" + field + "'");
          }
          match(TK::Symbol, ",");
        }
        expect(TK::Symbol, "}");
        w->types.push_back(std::move(lt));
        continue;
      }
      std::string field = advance().value;
      expect(TK::Symbol, ":");
      if (field == "width") w->width = intLit();
      else if (field == "height") w->height = intLit();
      else fail("unknown world field '" + field + "'");
      match(TK::Symbol, ",");
    }
    expect(TK::Symbol, "}");
    return w;
  }

  std::unique_ptr<Block> parseBlock() {
    expect(TK::Symbol, "{");
    auto b = std::make_unique<Block>();
    while (!check(TK::Symbol, "}")) {
      if (peek().kind == TK::End) fail("unexpected end of file (missing '}')");
      b->stmts.push_back(parseStatement());
    }
    expect(TK::Symbol, "}");
    return b;
  }

  StmtP stmt(SK k, int line) {
    auto s = std::make_unique<Stmt>();
    s->k = k;
    s->line = line;
    return s;
  }

  StmtP parseStatement() {
    int line = peek().line;
    if (isKw("let")) {
      advance();
      auto s = stmt(SK::Let, line);
      s->sym = prog_.syms.intern(expect(TK::Ident).value);
      expect(TK::Symbol, "=");
      s->e = parseExpr();
      return s;
    }
    if (isKw("if")) return parseIf();
    if (isKw("while")) {
      advance();
      auto s = stmt(SK::While, line);
      s->e = parseExpr();
      s->body = parseBlock();
      return s;
    }
    if (isKw("for")) {
      advance();
      auto s = stmt(SK::For, line);
      s->sym = prog_.syms.intern(expect(TK::Ident).value);
      expect(TK::Keyword, "in");
      s->e = parseExpr();
      s->body = parseBlock();
      return s;
    }
    if (isKw("return")) {
      advance();
      auto s = stmt(SK::Return, line);
      if (!check(TK::Symbol, "}")) s->e = parseExpr();
      return s;
    }
    if (isKw("break")) {
      advance();
      return stmt(SK::Break, line);
    }
    if (isKw("run")) {
      advance();
      auto s = stmt(SK::Run, line);
      s->phaseName = expect(TK::Ident).value;
      return s;
    }
    auto expr = parseExpr();
    if (match(TK::Symbol, "=")) {
      auto s = stmt(SK::Assign, line);
      s->target = std::move(expr);
      s->e = parseExpr();
      return s;
    }
    auto s = stmt(SK::Expr, line);
    s->e = std::move(expr);
    return s;
  }

  StmtP parseIf() {
    int line = peek().line;
    advance();
    auto s = stmt(SK::If, line);
    s->e = parseExpr();
    s->body = parseBlock();
    if (match(TK::Keyword, "else")) {
      if (isKw("if")) s->elseIf = parseIf();
      else s->elseBlock = parseBlock();
    }
    return s;
  }

  ExprP binary(Op op, ExprP l, ExprP r, int line) {
    auto e = node(EK::Binary, line);
    e->op = op;
    e->a = std::move(l);
    e->b = std::move(r);
    return e;
  }

  ExprP parseExpr() { return parseOr(); }

  ExprP parseOr() {
    auto l = parseAnd();
    while (check(TK::Keyword, "or")) {
      int line = advance().line;
      l = binary(Op::Or, std::move(l), parseAnd(), line);
    }
    return l;
  }

  ExprP parseAnd() {
    auto l = parseNot();
    while (check(TK::Keyword, "and")) {
      int line = advance().line;
      l = binary(Op::And, std::move(l), parseNot(), line);
    }
    return l;
  }

  ExprP parseNot() {
    if (check(TK::Keyword, "not")) {
      auto e = node(EK::Unary, advance().line);
      e->op = Op::Not;
      e->a = parseNot();
      return e;
    }
    return parseComparison();
  }

  ExprP parseComparison() {
    static const std::unordered_map<std::string, Op> kOps = {
        {"==", Op::Eq}, {"!=", Op::Ne}, {"<", Op::Lt}, {">", Op::Gt}, {"<=", Op::Le}, {">=", Op::Ge}};
    auto l = parseAdditive();
    while (peek().kind == TK::Symbol) {
      auto it = kOps.find(peek().value);
      if (it == kOps.end()) break;
      int line = advance().line;
      l = binary(it->second, std::move(l), parseAdditive(), line);
    }
    return l;
  }

  ExprP parseAdditive() {
    auto l = parseMultiplicative();
    while (check(TK::Symbol, "+") || check(TK::Symbol, "-")) {
      const Token& t = advance();
      Op op = t.value == "+" ? Op::Add : Op::Sub;
      l = binary(op, std::move(l), parseMultiplicative(), t.line);
    }
    return l;
  }

  ExprP parseMultiplicative() {
    auto l = parseUnary();
    while (check(TK::Symbol, "*") || check(TK::Symbol, "/") || check(TK::Symbol, "%")) {
      const Token& t = advance();
      Op op = t.value == "*" ? Op::Mul : t.value == "/" ? Op::Div : Op::Mod;
      l = binary(op, std::move(l), parseUnary(), t.line);
    }
    return l;
  }

  ExprP parseUnary() {
    if (check(TK::Symbol, "-")) {
      auto e = node(EK::Unary, advance().line);
      e->op = Op::Neg;
      e->a = parseUnary();
      return e;
    }
    return parsePostfix();
  }

  ExprP parsePostfix() {
    auto expr = parsePrimary();
    for (;;) {
      int line = peek().line;
      if (match(TK::Symbol, ".")) {
        const Token& t = peek();
        if (t.kind != TK::Ident && t.kind != TK::Keyword) fail("expected an attribute name after '.'");
        auto e = node(EK::Attr, line);
        e->a = std::move(expr);
        e->sym = prog_.syms.intern(advance().value);
        expr = std::move(e);
      } else if (match(TK::Symbol, "(")) {
        if (expr->k != EK::Ident) fail("only named functions can be called");
        auto e = node(EK::Call, line);
        e->sym = expr->sym;
        while (!check(TK::Symbol, ")")) {
          if (peek().kind == TK::Ident && peek(1).kind == TK::Symbol && peek(1).value == "=") {
            int key = prog_.syms.intern(advance().value);
            advance();
            e->kwargs.emplace_back(key, parseExpr());
          } else {
            e->items.push_back(parseExpr());
          }
          if (!match(TK::Symbol, ",")) break;
        }
        expect(TK::Symbol, ")");
        expr = std::move(e);
      } else if (match(TK::Symbol, "[")) {
        auto e = node(EK::Index, line);
        e->a = std::move(expr);
        e->b = parseExpr();
        expect(TK::Symbol, "]");
        expr = std::move(e);
      } else {
        break;
      }
    }
    return expr;
  }

  ExprP parsePrimary() {
    const Token& t = peek();
    int line = t.line;
    if (t.kind == TK::Number) {
      auto e = node(EK::Num, line);
      e->constant = Value::num(std::stod(advance().value));
      return e;
    }
    if (t.kind == TK::String) {
      auto e = node(EK::Str, line);
      e->constant = Value::str(advance().value);
      return e;
    }
    if (t.kind == TK::Keyword && (t.value == "true" || t.value == "false")) {
      auto e = node(EK::Bool, line);
      e->constant = Value::boolean(advance().value == "true");
      return e;
    }
    if (t.kind == TK::Keyword && t.value == "null") {
      advance();
      return node(EK::Null, line);
    }
    if (t.kind == TK::Ident) {
      auto e = node(EK::Ident, line);
      e->sym = prog_.syms.intern(advance().value);
      return e;
    }
    if (match(TK::Symbol, "(")) {
      auto inner = parseExpr();
      expect(TK::Symbol, ")");
      return inner;
    }
    if (match(TK::Symbol, "[")) {
      auto e = node(EK::List, line);
      while (!check(TK::Symbol, "]")) {
        e->items.push_back(parseExpr());
        if (!match(TK::Symbol, ",")) break;
      }
      expect(TK::Symbol, "]");
      return e;
    }
    if (match(TK::Symbol, "{")) {
      auto e = node(EK::Dict, line);
      while (!check(TK::Symbol, "}")) {
        auto key = parseExpr();
        expect(TK::Symbol, ":");
        auto value = parseExpr();
        e->pairs.emplace_back(std::move(key), std::move(value));
        if (!match(TK::Symbol, ",")) break;
      }
      expect(TK::Symbol, "}");
      return e;
    }
    fail("unexpected token '" + t.value + "'");
  }
};

// ---- Link pass: resolve call sites and `run` statements once.

struct Linker {
  Program& prog;
  std::unordered_map<std::string, int> builtins, fns, phases;

  void expr(Expr* e) {
    if (!e) return;
    if (e->k == EK::Call) {
      const std::string& name = prog.syms.name(e->sym);
      if (auto it = builtins.find(name); it != builtins.end()) e->builtin = it->second;
      else if (auto jt = fns.find(name); jt != fns.end()) e->userFn = jt->second;
      // Neither: left unlinked, reported as "undefined function" only if the
      // call actually executes -- same as the reference interpreters.
    }
    expr(e->a.get());
    expr(e->b.get());
    for (auto& i : e->items) expr(i.get());
    for (auto& [k, v] : e->pairs) { expr(k.get()); expr(v.get()); }
    for (auto& [k, v] : e->kwargs) expr(v.get());
  }
  void stmt(Stmt* s) {
    if (!s) return;
    if (s->k == SK::Run) {
      auto it = phases.find(s->phaseName);
      s->phase = it == phases.end() ? -1 : it->second;
    }
    expr(s->e.get());
    expr(s->target.get());
    block(s->body.get());
    stmt(s->elseIf.get());
    block(s->elseBlock.get());
  }
  void block(Block* b) {
    if (!b) return;
    for (auto& s : b->stmts) stmt(s.get());
  }
  void run() {
    for (int i = 0; i < B_COUNT_; ++i) builtins.emplace(std::string(kBuiltinNames[i]), i);
    for (size_t i = 0; i < prog.fns.size(); ++i) fns.emplace(prog.fns[i].name, static_cast<int>(i));
    for (size_t i = 0; i < prog.phases.size(); ++i) phases.emplace(prog.phases[i].name, static_cast<int>(i));
    for (auto& f : prog.fns) block(f.body.get());
    for (auto& m : prog.memoryDecls) block(m.body.get());
    for (auto& p : prog.phases) block(p.body.get());
    for (auto& r : prog.roles)
      for (auto& a : r.memoryArgs) expr(a.get());
    block(prog.winCondition.get());
    block(prog.loop.get());
  }
};

}  // namespace

std::shared_ptr<Program> parseProgram(const std::string& source) {
  auto prog = std::make_shared<Program>();
  Parser(tokenize(source), *prog).parseProgram();
  Linker{*prog}.run();
  return prog;
}

}  // namespace sl
