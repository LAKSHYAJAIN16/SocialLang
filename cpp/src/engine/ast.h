// SocialLang AST. Mirrors sociallang/lang/ast_nodes.py node-for-node, with one
// difference that matters for speed: every identifier, attribute name, and
// keyword-argument name is interned to an integer symbol at parse time, and
// every call site and `run` statement is linked to its builtin / user fn /
// phase index once, after parsing -- so the interpreter's hot path never
// hashes or compares a string to find a variable or a function.
#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/value.h"

namespace sl {

struct ParseError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct RuntimeError : std::runtime_error {
  int line = 0;
  RuntimeError(const std::string& msg, int line_ = 0) : std::runtime_error(msg), line(line_) {}
};

// Symbols every program shares, pre-interned in this order so the interpreter
// can switch on them directly (attribute names, keyword arguments, and the
// two implicit globals).
enum Sym : int {
  S_seat, S_role, S_team, S_alive, S_model, S_death_cause, S_x, S_y, S_location,
  S_persona, S_plan, S_subplan, S_text, S_author, S_kind, S_seq, S_round,
  S_importance, S_id, S_type, S_tag, S_capacity,
  S_temperature, S_max_tokens, S_threshold, S_goal, S_steps, S_chunks, S_topic,
  S_max_turns, S_cause, S_radius,
  S_agents, S_events, S_query,
  S_COUNT_
};

class SymbolTable {
 public:
  SymbolTable();
  int intern(const std::string& name);
  const std::string& name(int sym) const { return names_[sym]; }

 private:
  std::vector<std::string> names_;
  std::unordered_map<std::string, int> ids_;
};

enum class EK : uint8_t { Num, Str, Bool, Null, List, Dict, Ident, Unary, Binary, Attr, Call, Index };
enum class Op : uint8_t { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Gt, Le, Ge, And, Or, Not, Neg };

struct Expr {
  EK k = EK::Null;
  int line = 0;
  Op op = Op::Add;
  int sym = -1;       // Ident / Attr name, or the callee name for Call
  int builtin = -1;   // Call: linked builtin id, or -1
  int userFn = -1;    // Call: linked user fn index, or -1
  Value constant;     // Num / Str / Bool literal, prebuilt once
  std::unique_ptr<Expr> a, b;
  std::vector<std::unique_ptr<Expr>> items;  // List items / Call args
  std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> pairs;  // Dict
  std::vector<std::pair<int, std::unique_ptr<Expr>>> kwargs;                   // Call
};

struct Block;

enum class SK : uint8_t { Let, Assign, If, While, For, Return, Break, Run, Expr };

struct Stmt {
  SK k = SK::Expr;
  int line = 0;
  int sym = -1;       // Let name / For variable
  int phase = -1;     // Run: linked phase index
  std::string phaseName;
  std::unique_ptr<Expr> e;       // value / condition / iterable / expression
  std::unique_ptr<Expr> target;  // Assign target
  std::unique_ptr<Block> body;   // If-then / While / For
  std::unique_ptr<Stmt> elseIf;
  std::unique_ptr<Block> elseBlock;
};

struct Block {
  std::vector<std::unique_ptr<Stmt>> stmts;
};

struct RoleDecl {
  std::string name, team, sees = "none";
  std::string memoryName;  // empty: no memory pattern (sees everything visible)
  std::vector<std::unique_ptr<Expr>> memoryArgs;
  int count = -1;  // -1: `count: remainder`
};

struct LocationTypeDecl {
  std::string name, tag;
  bool hasTag = false;
  int capacity = -1;
  int count = 1;
};

struct WorldDecl {
  int width = 0, height = 0;
  std::vector<LocationTypeDecl> types;
};

struct FnDecl {
  std::string name;
  std::vector<int> params;
  std::unique_ptr<Block> body;
};

struct PhaseDecl {
  std::string name;
  std::unique_ptr<Block> body;
};

struct Program {
  std::string name;
  int agentsMin = 0, agentsMax = 0;
  std::vector<RoleDecl> roles;
  std::vector<FnDecl> memoryDecls;
  std::vector<FnDecl> fns;
  std::vector<PhaseDecl> phases;
  std::unique_ptr<Block> winCondition, loop;
  std::unique_ptr<WorldDecl> world;
  SymbolTable syms;
};

}  // namespace sl
