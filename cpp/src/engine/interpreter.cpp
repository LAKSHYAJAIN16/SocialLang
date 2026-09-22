#include "engine/interpreter.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <thread>

#include "engine/builtins.h"
#include "engine/memory.h"
#include "engine/parallel.h"

namespace sl {

const char* logKindName(LogKind k) {
  switch (k) {
    case LogKind::Ask: return "ask";
    case LogKind::Broadcast: return "broadcast";
    case LogKind::Whisper: return "whisper";
    case LogKind::Note: return "note";
    case LogKind::Reflection: return "reflection";
    case LogKind::Plan: return "plan";
    case LogKind::Dialogue: return "dialogue";
    case LogKind::Print: return "print";
    case LogKind::Error: return "error";
  }
  return "?";
}

// ---- Scopes: stack-allocated, a few variables inline, overflow on the heap.

struct Interpreter::Env {
  explicit Env(Env* p) : parent(p) {}
  Env* parent;
  static constexpr int kInline = 6;
  int n = 0;
  int syms[kInline];
  Value vals[kInline];
  std::vector<std::pair<int, Value>> more;

  Value* local(int s) {
    for (int i = 0; i < n; ++i)
      if (syms[i] == s) return &vals[i];
    for (auto& [k, v] : more)
      if (k == s) return &v;
    return nullptr;
  }
  Value* find(int s) {
    for (Env* e = this; e; e = e->parent)
      if (Value* v = e->local(s)) return v;
    return nullptr;
  }
  void define(int s, Value v) {
    if (Value* existing = local(s)) {
      *existing = std::move(v);
    } else if (n < kInline) {
      syms[n] = s;
      vals[n++] = std::move(v);
    } else {
      more.emplace_back(s, std::move(v));
    }
  }
};

struct Interpreter::Kwargs {
  std::vector<std::pair<int, Value>> v;
  const Value* get(int sym) const {
    for (auto& [k, val] : v)
      if (k == sym) return &val;
    return nullptr;
  }
  double num(int sym, double fallback) const {
    const Value* p = get(sym);
    return p && p->t == VT::Num ? p->n : fallback;
  }
};

namespace {

std::string trimmed(const std::string& s, const char* chars = " \t\r\n") {
  size_t a = s.find_first_not_of(chars);
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(chars);
  return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string join(const std::vector<std::string>& parts, const char* sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

const char* kFarewells[] = {"bye", "goodbye", "good bye", "see you", "farewell", "gotta go",
                            "have to go", "talk later", "talk to you later", "catch you later"};

bool looksLikeFarewell(const std::string& line) {
  std::string l = lower(line);
  for (const char* w : kFarewells)
    if (l.find(w) != std::string::npos) return true;
  return false;
}

size_t pyListIndex(const std::vector<Value>& list, const Value& key, int line) {
  if (key.t != VT::Num) throw RuntimeError("list index must be a number", line);
  long long idx = static_cast<long long>(std::trunc(key.n));
  if (idx < 0) idx += static_cast<long long>(list.size());
  if (idx < 0 || idx >= static_cast<long long>(list.size())) throw RuntimeError("list index out of range", line);
  return static_cast<size_t>(idx);
}

constexpr int kMaxCallDepth = 2000;
thread_local int gCallDepth = 0;

}  // namespace

// ---- Setup: seat assignment and world scatter.

Interpreter::Interpreter(std::shared_ptr<const Program> program, const std::vector<RosterEntry>& roster,
                         uint32_t seed, LogSink sink, InterpreterOptions options)
    : program_(std::move(program)),
      syms_(program_->syms),
      rng_(seed),
      sink_(std::move(sink)),
      opts_(options) {
  opts_.maxConcurrency = std::max(1, opts_.maxConcurrency);
  const Program& p = *program_;
  SeededRandom assignRng(seed);

  int remainderRoles = 0, explicitTotal = 0;
  for (auto& r : p.roles) {
    if (r.count < 0) ++remainderRoles;
    else explicitTotal += r.count;
  }
  if (remainderRoles > 1) throw RuntimeError("at most one role may use `count: remainder`");
  int remainderCount = 0;
  if (remainderRoles > 0) {
    int drawn = p.agentsMax > 0 ? assignRng.randint(p.agentsMin, p.agentsMax) : explicitTotal;
    remainderCount = std::max(drawn - explicitTotal, 0);
  }
  std::vector<int> roleSeq;
  for (size_t i = 0; i < p.roles.size(); ++i) {
    int n = p.roles[i].count >= 0 ? p.roles[i].count : remainderCount;
    roleSeq.insert(roleSeq.end(), n, static_cast<int>(i));
  }
  assignRng.shuffle(roleSeq);
  if (roleSeq.empty()) throw RuntimeError("sim has no agents configured -- check role `count`s and `agents:`");
  if (roster.empty()) throw RuntimeError("roster is empty -- no runnable models available");

  std::vector<size_t> modelIdx;
  if (roster.size() >= roleSeq.size()) {
    std::vector<size_t> all(roster.size());
    for (size_t i = 0; i < all.size(); ++i) all[i] = i;
    assignRng.shuffle(all);
    modelIdx.assign(all.begin(), all.begin() + static_cast<long long>(roleSeq.size()));
  } else {
    for (size_t i = 0; i < roleSeq.size(); ++i) modelIdx.push_back(assignRng.index(roster.size()));
  }

  agents_.resize(roleSeq.size());
  for (size_t i = 0; i < roleSeq.size(); ++i) {
    Agent& a = agents_[i];
    const RoleDecl& role = p.roles[roleSeq[i]];
    a.seat = "P" + std::to_string(i + 1);
    a.roleIdx = roleSeq[i];
    a.role = role.name;
    a.team = role.team;
    a.model = roster[modelIdx[i]].key;
    a.provider = roster[modelIdx[i]].provider.get();
    a.seatV = Value::str(a.seat);
    a.roleV = Value::str(a.role);
    a.teamV = Value::str(a.team);
    a.modelV = Value::str(a.model);
  }
  privateEvents_.resize(agents_.size());

  global_ = std::make_unique<Env>(nullptr);
  std::vector<Value> all;
  all.reserve(agents_.size());
  for (size_t i = 0; i < agents_.size(); ++i) all.push_back(Value::agent(static_cast<int>(i)));
  global_->define(S_agents, Value::list(std::move(all)));

  if (p.world) {
    const WorldDecl& w = *p.world;
    std::vector<std::string> seen;
    for (auto& lt : w.types) {
      if (std::find(seen.begin(), seen.end(), lt.name) != seen.end())
        throw RuntimeError("world { }: duplicate location type '" + lt.name + "'");
      seen.push_back(lt.name);
      for (int i = 0; i < lt.count; ++i) {
        Location loc;
        loc.id = lt.name + "_" + std::to_string(i);
        loc.type = lt.name;
        loc.tag = lt.tag;
        loc.hasTag = lt.hasTag;
        loc.capacity = lt.capacity;
        // scatterPoint: up to 20 tries at >= 1 unit from every placed location.
        if (w.width > 0 && w.height > 0) {
          bool placed = false;
          for (int attempt = 0; attempt < 20 && !placed; ++attempt) {
            double x = rng_.uniform(0, w.width), y = rng_.uniform(0, w.height);
            bool ok = true;
            for (auto& other : locations_) {
              double dx = x - other.x, dy = y - other.y;
              if (dx * dx + dy * dy < 1.0) {
                ok = false;
                break;
              }
            }
            if (ok) {
              loc.x = x;
              loc.y = y;
              placed = true;
            }
          }
          if (!placed) {
            loc.x = rng_.uniform(0, w.width);
            loc.y = rng_.uniform(0, w.height);
          }
        }
        locations_.push_back(std::move(loc));
      }
    }
  }
}

Interpreter::~Interpreter() = default;

// ---- Stepping

Interpreter::StepResult Interpreter::step() {
  if (!program_->loop) throw RuntimeError("sim has no `loop` block");
  ++round_;
  global_->define(S_round, Value::num(round_));
  Env env(global_.get());
  StepResult r;
  Flow f = execBlock(*program_->loop, env);
  if (f == Flow::Return) {
    r.done = true;
    r.winner = std::move(returnValue_);
  } else if (f == Flow::Break) {
    r.done = true;
  }
  return r;
}

void Interpreter::checkCancel() {
  if (cancel.load(std::memory_order_relaxed)) throw RuntimeError("stopped");
}

Value Interpreter::checkWin() {
  if (!program_->winCondition) return Value();
  Env env(global_.get());
  if (execBlock(*program_->winCondition, env) == Flow::Return) return std::move(returnValue_);
  return Value();
}

// ---- Statements

Interpreter::Flow Interpreter::execBlock(const Block& b, Env& env) {
  for (auto& s : b.stmts) {
    Flow f = execStmt(*s, env);
    if (f != Flow::Normal) return f;
  }
  return Flow::Normal;
}

Interpreter::Flow Interpreter::execStmt(const Stmt& s, Env& env) {
  try {
    switch (s.k) {
      case SK::Let: env.define(s.sym, eval(*s.e, env)); return Flow::Normal;
      case SK::Assign: assign(*s.target, eval(*s.e, env), env); return Flow::Normal;
      case SK::Expr: eval(*s.e, env); return Flow::Normal;
      case SK::If: {
        if (truthy(eval(*s.e, env))) {
          Env inner(&env);
          return execBlock(*s.body, inner);
        }
        if (s.elseIf) return execStmt(*s.elseIf, env);
        if (s.elseBlock) {
          Env inner(&env);
          return execBlock(*s.elseBlock, inner);
        }
        return Flow::Normal;
      }
      case SK::While:
        while (truthy(eval(*s.e, env))) {
          checkCancel();
          Env inner(&env);
          Flow f = execBlock(*s.body, inner);
          if (f == Flow::Break) break;
          if (f == Flow::Return) return f;
        }
        return Flow::Normal;
      case SK::For: {
        Value it = eval(*s.e, env);
        std::vector<Value> items;
        if (it.t == VT::List) items = it.l();
        else if (it.t == VT::Dict)
          for (auto& [k, v] : it.d().entries) items.push_back(k);
        else throw RuntimeError("can only loop over a list or dict", s.line);
        for (auto& item : items) {
          checkCancel();
          Env inner(&env);
          inner.define(s.sym, item);
          Flow f = execBlock(*s.body, inner);
          if (f == Flow::Break) break;
          if (f == Flow::Return) return f;
        }
        return Flow::Normal;
      }
      case SK::Return:
        returnValue_ = s.e ? eval(*s.e, env) : Value();
        return Flow::Return;
      case SK::Break: return Flow::Break;
      case SK::Run: {
        if (s.phase < 0) throw RuntimeError("no such phase '" + s.phaseName + "'", s.line);
        Env inner(global_.get());
        // A `return` or `break` inside a phase propagates to the loop that ran
        // it, exactly like the reference's ReturnSignal/BreakSignal.
        return execBlock(*program_->phases[s.phase].body, inner);
      }
    }
  } catch (RuntimeError& e) {
    if (!e.line) e.line = s.line;
    throw;
  }
  return Flow::Normal;
}

void Interpreter::assign(const Expr& target, Value v, Env& env) {
  if (target.k == EK::Ident) {
    Value* slot = env.find(target.sym);
    if (!slot)
      throw RuntimeError("assignment to undefined variable '" + syms_.name(target.sym) +
                         "' (use 'let' to declare it first)", target.line);
    *slot = std::move(v);
    return;
  }
  if (target.k == EK::Index) {
    Value obj = eval(*target.a, env);
    Value key = eval(*target.b, env);
    if (obj.t == VT::Dict) obj.d().set(key, std::move(v));
    else if (obj.t == VT::List) obj.l()[pyListIndex(obj.l(), key, target.line)] = std::move(v);
    else throw RuntimeError("cannot index-assign into this value", target.line);
    return;
  }
  throw RuntimeError("invalid assignment target", target.line);
}

// ---- Expressions

Value Interpreter::eval(const Expr& e, Env& env) {
  switch (e.k) {
    case EK::Num:
    case EK::Str:
    case EK::Bool: return e.constant;
    case EK::Null: return Value();
    case EK::Ident: {
      if (Value* v = env.find(e.sym)) return *v;
      throw RuntimeError("undefined variable '" + syms_.name(e.sym) + "'", e.line);
    }
    case EK::List: {
      std::vector<Value> items;
      items.reserve(e.items.size());
      for (auto& i : e.items) items.push_back(eval(*i, env));
      return Value::list(std::move(items));
    }
    case EK::Dict: {
      Value d = Value::dict();
      for (auto& [k, v] : e.pairs) {
        Value key = eval(*k, env);
        d.d().set(key, eval(*v, env));
      }
      return d;
    }
    case EK::Unary: {
      Value v = eval(*e.a, env);
      if (e.op == Op::Not) return Value::boolean(!truthy(v));
      if (v.t != VT::Num) throw RuntimeError("unary '-' needs a number", e.line);
      return Value::num(-v.n);
    }
    case EK::Binary: return evalBinary(e, env);
    case EK::Attr: return evalAttr(e, env);
    case EK::Call: return evalCall(e, env);
    case EK::Index: {
      Value obj = eval(*e.a, env);
      Value key = eval(*e.b, env);
      if (obj.t == VT::Dict) {
        Value* v = obj.d().find(key);
        return v ? *v : Value();
      }
      if (obj.t == VT::List) return obj.l()[pyListIndex(obj.l(), key, e.line)];
      throw RuntimeError("cannot index this value", e.line);
    }
  }
  return Value();
}

Value Interpreter::evalBinary(const Expr& e, Env& env) {
  if (e.op == Op::And) {
    Value l = eval(*e.a, env);
    return truthy(l) ? eval(*e.b, env) : l;
  }
  if (e.op == Op::Or) {
    Value l = eval(*e.a, env);
    return truthy(l) ? l : eval(*e.b, env);
  }
  Value l = eval(*e.a, env);
  Value r = eval(*e.b, env);
  switch (e.op) {
    case Op::Eq: return Value::boolean(valuesEqual(l, r));
    case Op::Ne: return Value::boolean(!valuesEqual(l, r));
    case Op::Add:
      if (l.t == VT::Num && r.t == VT::Num) return Value::num(l.n + r.n);
      if (l.t == VT::Str && r.t == VT::Str) return Value::str(l.s() + r.s());
      if (l.t == VT::Str || r.t == VT::Str)
        throw RuntimeError("cannot '+' a string with a non-string -- use str() to convert first", e.line);
      throw RuntimeError("'+' needs two numbers or two strings", e.line);
    case Op::Lt:
    case Op::Gt:
    case Op::Le:
    case Op::Ge: {
      int cmp;
      if (l.t == VT::Num && r.t == VT::Num) cmp = l.n < r.n ? -1 : l.n > r.n ? 1 : 0;
      else if (l.t == VT::Str && r.t == VT::Str) cmp = l.s().compare(r.s());
      else throw RuntimeError("can only compare two numbers or two strings", e.line);
      bool res = e.op == Op::Lt ? cmp < 0 : e.op == Op::Gt ? cmp > 0 : e.op == Op::Le ? cmp <= 0 : cmp >= 0;
      return Value::boolean(res);
    }
    default: break;
  }
  if (l.t != VT::Num || r.t != VT::Num) throw RuntimeError("arithmetic needs numbers", e.line);
  switch (e.op) {
    case Op::Sub: return Value::num(l.n - r.n);
    case Op::Mul: return Value::num(l.n * r.n);
    case Op::Div: return Value::num(l.n / r.n);
    case Op::Mod: return Value::num(std::fmod(std::fmod(l.n, r.n) + r.n, r.n));  // floor-mod, like Python
    default: throw RuntimeError("unknown operator", e.line);
  }
}

Value Interpreter::evalAttr(const Expr& e, Env& env) {
  Value obj = eval(*e.a, env);
  const std::string& name = syms_.name(e.sym);
  if (obj.t == VT::Agent) {
    const Agent& a = agents_[obj.idx];
    switch (e.sym) {
      case S_seat: return a.seatV;
      case S_role: return a.roleV;
      case S_team: return a.teamV;
      case S_alive: return Value::boolean(a.alive);
      case S_model: return a.modelV;
      case S_death_cause: return a.hasDeathCause ? Value::str(a.deathCause) : Value();
      case S_x: return a.hasPos ? Value::num(a.x) : Value();
      case S_y: return a.hasPos ? Value::num(a.y) : Value();
      case S_location: return a.location >= 0 ? Value::loc(a.location) : Value();
      case S_persona: return a.persona;
      case S_plan: return a.plan;
      case S_subplan: return a.subplan;
      default: break;
    }
  } else if (obj.t == VT::Event) {
    const Event& ev = events_[obj.idx];
    switch (e.sym) {
      case S_text: return Value::str(ev.text);
      case S_author: return ev.author.empty() ? Value() : Value::str(ev.author);
      case S_kind: return Value::str(logKindName(ev.kind));
      case S_seq: return Value::num(obj.idx + 1);
      case S_round: return Value::num(ev.round);
      case S_importance: return Value::num(ev.importance);
      default: break;
    }
  } else if (obj.t == VT::Loc) {
    const Location& l = locations_[obj.idx];
    switch (e.sym) {
      case S_id: return Value::str(l.id);
      case S_type: return Value::str(l.type);
      case S_tag: return l.hasTag ? Value::str(l.tag) : Value();
      case S_capacity: return l.capacity >= 0 ? Value::num(l.capacity) : Value();
      case S_x: return Value::num(l.x);
      case S_y: return Value::num(l.y);
      default: break;
    }
  } else {
    throw RuntimeError("'." + name + "' is not valid on this kind of value", e.line);
  }
  throw RuntimeError("no attribute '" + name + "' here", e.line);
}

Value Interpreter::evalCall(const Expr& e, Env& env) {
  std::vector<Value> args;
  args.reserve(e.items.size());
  for (auto& a : e.items) args.push_back(eval(*a, env));
  if (e.builtin >= 0) {
    Kwargs kw;
    for (auto& [k, v] : e.kwargs) kw.v.emplace_back(k, eval(*v, env));
    return callBuiltin(e.builtin, args, kw, e.line);
  }
  if (e.userFn >= 0) return callUser(e.userFn, args);
  throw RuntimeError("undefined function '" + syms_.name(e.sym) + "'", e.line);
}

Value Interpreter::callUser(int fnIdx, std::vector<Value>& args) {
  const FnDecl& fn = program_->fns[fnIdx];
  struct DepthGuard {
    DepthGuard() { ++gCallDepth; }
    ~DepthGuard() { --gCallDepth; }
  } guard;
  if (gCallDepth > kMaxCallDepth)
    throw RuntimeError("maximum call depth exceeded in '" + fn.name + "' (infinite recursion?)");
  Env env(global_.get());
  for (size_t i = 0; i < fn.params.size() && i < args.size(); ++i) env.define(fn.params[i], std::move(args[i]));
  Flow f = execBlock(*fn.body, env);
  return f == Flow::Return ? std::move(returnValue_) : Value();
}

std::string Interpreter::stringify(const Value& v) const {
  switch (v.t) {
    case VT::Null: return "null";
    case VT::Bool: return v.b ? "true" : "false";
    case VT::Num: return numToString(v.n);
    case VT::Str: return v.s();
    case VT::Agent: return agents_[v.idx].seat;
    case VT::Loc: return locations_[v.idx].id;
    case VT::Event: return events_[v.idx].text;
    case VT::List: {
      std::string out;
      const auto& items = v.l();
      for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += stringify(items[i]);
      }
      return out;
    }
    case VT::Dict: {
      std::string out = "{";
      bool first = true;
      for (auto& [k, val] : v.d().entries) {
        if (!first) out += ", ";
        first = false;
        out += stringify(k) + ": " + stringify(val);
      }
      return out + "}";
    }
  }
  return "";
}

// ---- Events and memory

void Interpreter::emit(LogEntry entry) {
  if (sink_) sink_(entry);
}

int Interpreter::appendEvent(LogKind kind, std::string text, std::string author, std::vector<int> visibleTo,
                             double importance) {
  bool isPublic = visibleTo.empty();
  double imp = importance >= 0 ? importance : heuristicImportance(text);
  int idx = static_cast<int>(events_.size());
  events_.push_back({round_, kind, text, author, imp});
  if (isPublic) {
    publicEvents_.push_back(idx);
    publicImpTotal_ += imp;
  } else {
    std::sort(visibleTo.begin(), visibleTo.end());
    visibleTo.erase(std::unique(visibleTo.begin(), visibleTo.end()), visibleTo.end());
    for (int a : visibleTo) {
      privateEvents_[a].push_back(idx);
      agents_[a].privateImpSince += imp;
    }
  }
  if (sink_) {
    LogEntry entry;
    entry.seq = idx + 1;
    entry.round = round_;
    entry.kind = kind;
    entry.text = std::move(text);
    entry.author = std::move(author);
    entry.visibleTo = std::move(visibleTo);
    sink_(entry);
  }
  return idx;
}

std::vector<int> Interpreter::visibleEventsFor(int agent) const {
  const auto& pub = publicEvents_;
  const auto& priv = privateEvents_[agent];
  std::vector<int> merged;
  merged.reserve(pub.size() + priv.size());
  std::merge(pub.begin(), pub.end(), priv.begin(), priv.end(), std::back_inserter(merged));
  return merged;
}

std::string Interpreter::renderEvent(int idx) const {
  const Event& e = events_[idx];
  return e.author.empty() ? e.text : "[" + e.author + "] " + e.text;
}

bool Interpreter::usesScriptedMemory(const RoleDecl& role) const {
  if (role.memoryName.empty()) return false;
  for (auto& decl : program_->memoryDecls)
    if (decl.name == role.memoryName) return true;
  return false;
}

std::vector<int> Interpreter::callMemory(const RoleDecl& role, const std::vector<int>& visible,
                                         const std::string& query) {
  std::vector<Value> args;
  for (auto& a : role.memoryArgs) args.push_back(eval(*a, *global_));

  for (auto& decl : program_->memoryDecls) {
    if (decl.name != role.memoryName) continue;
    Env env(global_.get());
    std::vector<Value> evs;
    evs.reserve(visible.size());
    for (int i : visible) evs.push_back(Value::event(i));
    env.define(S_events, Value::list(std::move(evs)));
    env.define(S_query, Value::str(query));
    for (size_t i = 0; i < decl.params.size() && i < args.size(); ++i) env.define(decl.params[i], args[i]);
    if (execBlock(*decl.body, env) == Flow::Return && returnValue_.t == VT::List) {
      std::vector<int> out;
      for (auto& v : returnValue_.l())
        if (v.t == VT::Event) out.push_back(v.idx);
      return out;
    }
    return visible;
  }
  return nativeMemory(role, args, visible, query);
}

std::vector<int> Interpreter::nativeMemory(const RoleDecl& role, const std::vector<Value>& args,
                                           const std::vector<int>& visible, const std::string& query) const {
  if (role.memoryName == "full_history") return visible;
  if (role.memoryName == "recent") {
    int n = !args.empty() && args[0].t == VT::Num ? static_cast<int>(args[0].n) : 10;
    if (n <= 0) return {};
    size_t start = visible.size() > static_cast<size_t>(n) ? visible.size() - n : 0;
    return std::vector<int>(visible.begin() + static_cast<long long>(start), visible.end());
  }
  if (role.memoryName == "generative") {
    int k = !args.empty() && args[0].t == VT::Num ? static_cast<int>(args[0].n) : 8;
    return retrieveMemory(
        visible, query, static_cast<int>(events_.size()), k,
        [&](int i) -> const std::string& { return events_[i].text; },
        [&](int i) { return events_[i].importance; });
  }
  throw RuntimeError("unknown memory strategy '" + role.memoryName + "' (not a native kind, no `memory " +
                     role.memoryName + "(...) { }` declared)");
}

std::string Interpreter::buildContext(int agent, const std::string& query, const std::vector<Value>* memoryArgs) {
  const RoleDecl& role = program_->roles[agents_[agent].roleIdx];
  std::vector<int> visible = visibleEventsFor(agent);
  std::vector<int> selected = role.memoryName.empty() ? visible
                              : memoryArgs            ? nativeMemory(role, *memoryArgs, visible, query)
                                                      : callMemory(role, visible, query);
  if (selected.empty()) return "(nothing has happened yet)";
  std::string out;
  for (size_t i = 0; i < selected.size(); ++i) {
    if (i) out += '\n';
    out += renderEvent(selected[i]);
  }
  return out;
}

std::string Interpreter::identityFor(int agent) const {
  const Agent& a = agents_[agent];
  const RoleDecl& role = program_->roles[a.roleIdx];
  std::string id = "You are " + a.seat + ". Your role is " + role.name + " (" + role.team + " team).";
  if (!a.persona.s().empty()) id += " " + a.persona.s();
  if (role.sees == "teammates") {
    std::vector<std::string> mates;
    for (size_t i = 0; i < agents_.size(); ++i)
      if (static_cast<int>(i) != agent && agents_[i].roleIdx == a.roleIdx) mates.push_back(agents_[i].seat);
    if (!mates.empty()) id += " Your teammates are: " + join(mates, ", ") + ".";
  }
  return id;
}

// ---- Provider calls

CompletionRequest Interpreter::makeRequest(int agent, const std::string& prompt, double temperature, int maxTokens,
                                           const std::vector<Value>* memoryArgs) {
  CompletionRequest req;
  req.system = identityFor(agent);
  req.temperature = temperature;
  req.maxTokens = maxTokens;
  if (agents_[agent].provider->wantsContext())
    req.user = "What has happened so far:\n" + buildContext(agent, prompt, memoryArgs) + "\n\nNow: " + prompt;
  else
    req.user = "Now: " + prompt;
  return req;
}

// Bulk asks: every agent's context is an independent read of the event
// table, so they're built data-parallel on the worker pool. Scripted memory
// patterns (a `memory name { ... }` block) execute interpreter code and stay
// on the serial path, as does everything when options.parallelContext is off.
std::vector<CompletionRequest> Interpreter::makeRequests(const std::vector<int>& list, const std::string& prompt,
                                                         double temperature, int maxTokens) {
  std::vector<CompletionRequest> reqs(list.size());
  bool parallel = opts_.parallelContext && list.size() > 1;
  bool anyContext = false;
  for (int a : list) {
    anyContext = anyContext || agents_[a].provider->wantsContext();
    if (usesScriptedMemory(program_->roles[agents_[a].roleIdx])) parallel = false;
  }
  if (!parallel || !anyContext) {
    for (size_t i = 0; i < list.size(); ++i) reqs[i] = makeRequest(list[i], prompt, temperature, maxTokens);
    return reqs;
  }
  // Memory-pattern arguments (`recent(10)`) are evaluated once per role, up
  // front, so the parallel region only reads.
  std::vector<std::vector<Value>> roleArgs(program_->roles.size());
  for (size_t r = 0; r < program_->roles.size(); ++r)
    for (auto& e : program_->roles[r].memoryArgs) roleArgs[r].push_back(eval(*e, *global_));
  ThreadPool::shared().parallelFor(list.size(), [&](size_t i) {
    int a = list[i];
    reqs[i] = makeRequest(a, prompt, temperature, maxTokens, &roleArgs[agents_[a].roleIdx]);
  });
  return reqs;
}

std::string Interpreter::runCompletion(int agent, const CompletionRequest& req) {
  checkCancel();
  return runCompletions({agent}, {req})[0];
}

std::vector<std::string> Interpreter::runCompletions(const std::vector<int>& agentIdx,
                                                     const std::vector<CompletionRequest>& reqs) {
  std::vector<CompletionResult> results(reqs.size());
  bool anyRemote = false;
  for (int a : agentIdx) anyRemote = anyRemote || agents_[a].provider->isRemote();

  if (!anyRemote || reqs.size() <= 1) {
    for (size_t i = 0; i < reqs.size(); ++i) {
      checkCancel();
      results[i] = agents_[agentIdx[i]].provider->complete(reqs[i]);
    }
  } else {
    // Fan out across a small pool -- the counterpart of the Python reference's
    // ThreadPoolExecutor in _bi_ask_all. Events are still appended afterward,
    // in agent order, so a run's log is deterministic regardless of which
    // request comes back first.
    std::atomic<size_t> next{0};
    auto worker = [&] {
      for (size_t i = next++; i < reqs.size(); i = next++) {
        if (cancel.load()) {
          results[i].error = "stopped";
          continue;
        }
        results[i] = agents_[agentIdx[i]].provider->complete(reqs[i]);
      }
    };
    size_t n = std::min<size_t>(static_cast<size_t>(opts_.maxConcurrency), reqs.size());
    std::vector<std::thread> pool;
    for (size_t t = 0; t < n; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    checkCancel();
  }

  std::vector<std::string> texts;
  texts.reserve(results.size());
  for (size_t i = 0; i < results.size(); ++i) {
    auto& r = results[i];
    ++stats.calls;
    stats.promptTokens += r.promptTokens;
    stats.completionTokens += r.completionTokens;
    if (!r.error.empty()) {
      ++stats.errors;
      const Agent& a = agents_[agentIdx[i]];
      LogEntry entry;
      entry.round = round_;
      entry.kind = LogKind::Error;
      entry.author = a.seat;
      entry.text = a.model + ": " + r.error;
      emit(std::move(entry));
    }
    texts.push_back(std::move(r.text));
  }
  return texts;
}

// ---- Builtin helpers

int Interpreter::agentArg(const std::vector<Value>& args, size_t i, const char* fn, int line) const {
  if (i >= args.size() || args[i].t != VT::Agent)
    throw RuntimeError(std::string(fn) + "() expects an agent as argument " + std::to_string(i + 1), line);
  return args[i].idx;
}

std::vector<int> Interpreter::agentList(const Value& v, const char* fn, int line) const {
  std::vector<int> out;
  if (v.t == VT::Agent) {
    out.push_back(v.idx);
    return out;
  }
  if (v.t != VT::List) throw RuntimeError(std::string(fn) + "() expects a list of agents", line);
  for (auto& item : v.l()) {
    if (item.t != VT::Agent) throw RuntimeError(std::string(fn) + "() expects a list of agents", line);
    out.push_back(item.idx);
  }
  return out;
}

Value Interpreter::agentsWhere(const std::function<bool(const Agent&)>& pred) const {
  std::vector<Value> out;
  for (size_t i = 0; i < agents_.size(); ++i)
    if (pred(agents_[i])) out.push_back(Value::agent(static_cast<int>(i)));
  return Value::list(std::move(out));
}

Value Interpreter::matchOption(const std::string& text, const std::vector<Value>& options,
                               const std::vector<std::string>& labels) {
  std::string norm = lower(trimmed(trimmed(text), ".\"'"));
  for (size_t i = 0; i < options.size(); ++i)
    if (lower(labels[i]) == norm) return options[i];
  std::string lt = lower(text);
  for (size_t i = 0; i < options.size(); ++i)
    if (lt.find(lower(labels[i])) != std::string::npos) return options[i];
  return options.empty() ? Value() : options[rng_.index(options.size())];
}

std::vector<std::string> Interpreter::splitLines(const std::string& text) const {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = trimmed(trimmed(text.substr(start, end - start), "-* "));
    if (!line.empty()) out.push_back(std::move(line));
    start = end + 1;
  }
  return out;
}

std::vector<std::string> Interpreter::reflect(int agent) {
  const Agent& a = agents_[agent];
  const RoleDecl& role = program_->roles[a.roleIdx];
  CompletionRequest req;
  req.system = "You are " + a.seat + ". Your role is " + role.name + " (" + role.team + " team).";
  req.user =
      "Reflect on what has happened so far and state 1-3 higher-level insights or conclusions you can draw, one "
      "per line, no numbering.";
  if (a.provider->wantsContext()) {
    auto top = retrieveMemory(
        visibleEventsFor(agent), "", static_cast<int>(events_.size()), 15,
        [&](int i) -> const std::string& { return events_[i].text; },
        [&](int i) { return events_[i].importance; });
    req.user += "\n\n";
    for (size_t i = 0; i < top.size(); ++i) {
      if (i) req.user += '\n';
      req.user += renderEvent(top[i]);
    }
  }
  auto insights = splitLines(runCompletion(agent, req));
  for (size_t i = 0; i < insights.size() && i < 3; ++i)
    appendEvent(LogKind::Reflection, insights[i], a.seat, {agent}, 0.9);
  return insights;
}

Value Interpreter::currentStep(int agent) {
  const Agent& a = agents_[agent];
  const auto& plan = a.plan.l();
  if (plan.empty() || a.planCursor >= static_cast<int>(plan.size())) return Value();
  return plan[a.planCursor];
}

Value Interpreter::currentAction(int agent) {
  const Agent& a = agents_[agent];
  const auto& sub = a.subplan.l();
  if (!sub.empty() && a.subplanCursor < static_cast<int>(sub.size())) return sub[a.subplanCursor];
  Value step = currentStep(agent);
  if (step.t == VT::Dict) {
    Value* act = step.d().find(kActivityKey_);
    return act ? *act : Value();
  }
  return Value();
}

static Value stringList(const std::vector<std::string>& items) {
  std::vector<Value> out;
  out.reserve(items.size());
  for (auto& s : items) out.push_back(Value::str(s));
  return Value::list(std::move(out));
}

// ---- Builtins

Value Interpreter::callBuiltin(int id, std::vector<Value>& args, const Kwargs& kw, int line) {
  auto arg = [&](size_t i) -> const Value& {
    static const Value kNull;
    return i < args.size() ? args[i] : kNull;
  };
  auto listArg = [&](size_t i, const char* fn) -> const std::vector<Value>& {
    if (arg(i).t != VT::List) throw RuntimeError(std::string(fn) + "() expects a list", line);
    return arg(i).l();
  };
  auto locArg = [&](size_t i, const char* fn) -> int {
    if (arg(i).t != VT::Loc) throw RuntimeError(std::string(fn) + "() expects a location", line);
    return arg(i).idx;
  };
  double temperature = kw.num(S_temperature, 0.9);
  int maxTokens = static_cast<int>(kw.num(S_max_tokens, 500));

  auto doAsk = [&](int a, const std::string& prompt) {
    std::string text = runCompletion(a, makeRequest(a, prompt, temperature, maxTokens));
    appendEvent(LogKind::Ask, "(asked: " + prompt + ") " + text, agents_[a].seat, {a});
    return text;
  };
  auto choicePrompt = [&](const std::string& prompt, std::vector<std::string>& labels, const char* fn) {
    for (auto& o : listArg(2, fn)) labels.push_back(stringify(o));
    return prompt + "\n\nRespond with exactly one of: " + join(labels, ", ");
  };
  auto doAskAll = [&](const std::vector<int>& list, const std::string& prompt) {
    auto texts = runCompletions(list, makeRequests(list, prompt, temperature, maxTokens));
    for (size_t i = 0; i < list.size(); ++i)
      appendEvent(LogKind::Ask, "(asked: " + prompt + ") " + texts[i], agents_[list[i]].seat, {list[i]});
    return texts;
  };

  switch (id) {
    case B_ask: {
      int a = agentArg(args, 0, "ask", line);
      return Value::str(doAsk(a, stringify(arg(1))));
    }
    case B_ask_choice: {
      int a = agentArg(args, 0, "ask_choice", line);
      std::vector<std::string> labels;
      std::string full = choicePrompt(stringify(arg(1)), labels, "ask_choice");
      return matchOption(doAsk(a, full), listArg(2, "ask_choice"), labels);
    }
    case B_ask_all: {
      auto list = agentList(arg(0), "ask_all", line);
      auto texts = doAskAll(list, stringify(arg(1)));
      Value out = Value::dict();
      for (size_t i = 0; i < list.size(); ++i) out.d().set(Value::agent(list[i]), Value::str(texts[i]));
      return out;
    }
    case B_ask_choice_all: {
      auto list = agentList(arg(0), "ask_choice_all", line);
      std::vector<std::string> labels;
      std::string full = choicePrompt(stringify(arg(1)), labels, "ask_choice_all");
      auto texts = doAskAll(list, full);
      Value out = Value::dict();
      const auto& options = listArg(2, "ask_choice_all");
      for (size_t i = 0; i < list.size(); ++i) out.d().set(Value::agent(list[i]), matchOption(texts[i], options, labels));
      return out;
    }
    case B_broadcast: {
      if (args.size() == 1) {
        appendEvent(LogKind::Broadcast, stringify(arg(0)), "", {});
      } else {
        std::string author = arg(0).t == VT::Agent ? agents_[arg(0).idx].seat : stringify(arg(0));
        appendEvent(LogKind::Broadcast, stringify(arg(1)), author, {});
      }
      return Value();
    }
    case B_whisper: {
      auto list = agentList(arg(0), "whisper", line);
      if (list.empty()) return Value();  // whispering to nobody reaches nobody
      appendEvent(LogKind::Whisper, stringify(arg(1)), "", list);
      return Value();
    }
    case B_remember: {
      int a = agentArg(args, 0, "remember", line);
      appendEvent(LogKind::Note, stringify(arg(1)), agents_[a].seat, {a});
      return Value();
    }
    case B_reflect: return stringList(reflect(agentArg(args, 0, "reflect", line)));
    case B_maybe_reflect: {
      int a = agentArg(args, 0, "maybe_reflect", line);
      double threshold = 4.0;
      if (const Value* t = kw.get(S_threshold); t && t->t == VT::Num) threshold = t->n;
      else if (arg(1).t == VT::Num) threshold = arg(1).n;
      Agent& ag = agents_[a];
      double total = (publicImpTotal_ - ag.publicImpMark) + ag.privateImpSince;
      if (total < threshold) return Value::list();
      auto insights = reflect(a);
      agents_[a].publicImpMark = publicImpTotal_;
      agents_[a].privateImpSince = 0;
      return stringList(insights);
    }
    case B_set_persona: {
      int a = agentArg(args, 0, "set_persona", line);
      agents_[a].persona = Value::str(stringify(arg(1)));
      return Value();
    }
    case B_make_plan: {
      int a = agentArg(args, 0, "make_plan", line);
      std::string goal = args.size() > 1 ? stringify(arg(1))
                         : kw.get(S_goal) ? stringify(*kw.get(S_goal))
                                          : "Plan what you'll do.";
      int steps = static_cast<int>(kw.num(S_steps, 6));
      std::string prompt = goal + "\n\nSketch a rough plan of about " + std::to_string(steps) +
                           " broad-strokes steps for what you'll do, in order. Respond with exactly one step per "
                           "line, formatted as 'time: activity' (e.g. '9am: eat breakfast at home'). No "
                           "numbering, no extra commentary.";
      std::string text = runCompletion(a, makeRequest(a, prompt, 0.7, 300));
      std::vector<Value> plan;
      for (auto& l : splitLines(text)) {
        Value step = Value::dict();
        size_t colon = l.find(':');
        if (colon != std::string::npos) {
          step.d().set(kTimeKey_, Value::str(trimmed(l.substr(0, colon))));
          step.d().set(kActivityKey_, Value::str(trimmed(l.substr(colon + 1))));
        } else {
          step.d().set(kTimeKey_, Value::str(""));
          step.d().set(kActivityKey_, Value::str(l));
        }
        plan.push_back(std::move(step));
      }
      if (plan.empty()) {
        Value step = Value::dict();
        step.d().set(kTimeKey_, Value::str(""));
        step.d().set(kActivityKey_, Value::str(goal));
        plan.push_back(std::move(step));
      }
      if (steps > 0 && static_cast<int>(plan.size()) > steps) plan.resize(steps);
      Agent& ag = agents_[a];
      std::vector<std::string> summary;
      for (auto& s : plan) {
        std::string t = s.d().find(kTimeKey_)->s(), act = s.d().find(kActivityKey_)->s();
        summary.push_back(t.empty() ? act : t + ": " + act);
      }
      ag.plan = Value::list(std::move(plan));
      ag.planCursor = 0;
      ag.subplan = Value::list();
      ag.subplanCursor = 0;
      appendEvent(LogKind::Plan, "Plan: " + join(summary, "; "), ag.seat, {a}, 0.5);
      return ag.plan;
    }
    case B_current_step: return currentStep(agentArg(args, 0, "current_step", line));
    case B_decompose_step: {
      int a = agentArg(args, 0, "decompose_step", line);
      Value step = args.size() > 1 ? arg(1) : currentStep(a);
      int chunks = static_cast<int>(kw.num(S_chunks, 4));
      if (step.isNull()) return Value::list();
      std::string activity;
      if (step.t == VT::Dict) {
        Value* act = step.d().find(kActivityKey_);
        activity = act ? stringify(*act) : "";
      } else {
        activity = stringify(step);
      }
      std::string prompt = "Your current broad-strokes plan step is: '" + activity + "'. Break it down into about " +
                           std::to_string(chunks) +
                           " smaller, concrete, sequential actions (a few minutes each). Respond with exactly one "
                           "action per line, no numbering.";
      auto actions = splitLines(runCompletion(a, makeRequest(a, prompt, 0.7, 250)));
      if (actions.empty()) actions.push_back(activity);
      if (chunks > 0 && static_cast<int>(actions.size()) > chunks) actions.resize(chunks);
      agents_[a].subplan = stringList(actions);
      agents_[a].subplanCursor = 0;
      return agents_[a].subplan;
    }
    case B_current_action: return currentAction(agentArg(args, 0, "current_action", line));
    case B_advance_plan: {
      Agent& ag = agents_[agentArg(args, 0, "advance_plan", line)];
      int subSize = static_cast<int>(ag.subplan.l().size());
      if (subSize > 0 && ag.subplanCursor < subSize - 1) {
        ++ag.subplanCursor;
        return Value();
      }
      ag.subplan = Value::list();
      ag.subplanCursor = 0;
      int planSize = static_cast<int>(ag.plan.l().size());
      if (planSize > 0 && ag.planCursor < planSize - 1) ++ag.planCursor;
      return Value();
    }
    case B_react: {
      int a = agentArg(args, 0, "react", line);
      std::string observation = stringify(arg(1));
      Value cur = currentAction(a);
      std::string current = cur.isNull() || !truthy(cur) ? "nothing in particular" : stringify(cur);
      std::string prompt = "You observe: " + observation + "\nYour current planned action is: " + current +
                           "\n\nDecide: continue with your current planned action, or react to what you just "
                           "observed? If you continue, respond with exactly: CONTINUE\nIf you react, respond with "
                           "one sentence describing your new immediate action instead.";
      std::string text = trimmed(runCompletion(a, makeRequest(a, prompt, 0.8, 80)));
      appendEvent(LogKind::Ask, "(observed: " + observation + ") " + text, agents_[a].seat, {a});
      std::string bare = text;
      while (!bare.empty() && bare.back() == '.') bare.pop_back();
      for (auto& c : bare)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
      if (bare == "CONTINUE") return Value();
      return Value::str(text);
    }
    case B_converse: {
      int a = agentArg(args, 0, "converse", line);
      int b = agentArg(args, 1, "converse", line);
      std::string topic = args.size() > 2 ? stringify(arg(2)) : kw.get(S_topic) ? stringify(*kw.get(S_topic)) : "";
      int maxTurns = static_cast<int>(kw.num(S_max_turns, 4));
      std::vector<std::string> transcript;
      int speaker = a, other = b;
      std::string prompt = "You run into " + agents_[other].seat + "." +
                           (topic.empty() ? "" : " Topic on your mind: " + topic + ".") +
                           " Say one thing to them, one sentence.";
      for (int i = 0; i < std::max(maxTurns, 0) * 2; ++i) {
        std::string said = trimmed(runCompletion(speaker, makeRequest(speaker, prompt, 0.9, 100)));
        if (said.empty()) break;
        std::string lineText = agents_[speaker].seat + ": " + said;
        appendEvent(LogKind::Dialogue, lineText, agents_[speaker].seat, {a, b});
        transcript.push_back(lineText);
        if (looksLikeFarewell(said)) break;
        std::swap(speaker, other);
        prompt = agents_[other].seat + " just said to you: \"" + said + "\" Respond with one sentence.";
      }
      return stringList(transcript);
    }
    case B_alive: return agentsWhere([](const Agent& ag) { return ag.alive; });
    case B_all_agents: return agentsWhere([](const Agent&) { return true; });
    case B_with_role: {
      std::string role = stringify(arg(0));
      return agentsWhere([&](const Agent& ag) { return ag.role == role; });
    }
    case B_team_of: return agents_[agentArg(args, 0, "team_of", line)].teamV;
    case B_eliminate: {
      int a = agentArg(args, 0, "eliminate", line);
      Agent& ag = agents_[a];
      const Value* cause = args.size() > 1 ? &args[1] : kw.get(S_cause);
      ag.alive = false;
      ag.hasDeathCause = cause && !cause->isNull();
      ag.deathCause = ag.hasDeathCause ? stringify(*cause) : "";
      appendEvent(LogKind::Broadcast,
                  ag.seat + " was eliminated" + (ag.hasDeathCause ? " (" + ag.deathCause + ")" : "") + ".", "", {},
                  0.8);
      return Value();
    }
    case B_tally: {
      if (arg(0).t != VT::Dict || arg(0).d().size() == 0) return Value();
      struct Bucket {
        Value first;
        int count;
      };
      std::vector<Bucket> buckets;
      KeyEq eq;
      for (auto& [voter, target] : arg(0).d().entries) {
        auto it = std::find_if(buckets.begin(), buckets.end(), [&](const Bucket& bk) { return eq(bk.first, target); });
        if (it == buckets.end()) buckets.push_back({target, 1});
        else ++it->count;
      }
      int best = 0;
      for (auto& bk : buckets) best = std::max(best, bk.count);
      std::vector<Value> winners;
      for (auto& bk : buckets)
        if (bk.count == best) winners.push_back(bk.first);
      return winners[rng_.index(winners.size())];
    }
    case B_count: {
      const Value& v = arg(0);
      if (v.t == VT::List) return Value::num(static_cast<double>(v.l().size()));
      if (v.t == VT::Dict) return Value::num(static_cast<double>(v.d().size()));
      if (v.t == VT::Str) return Value::num(static_cast<double>(v.s().size()));
      throw RuntimeError("count() expects a list, dict, or string", line);
    }
    case B_last: {
      const auto& list = listArg(0, "last");
      int n = arg(1).t == VT::Num ? static_cast<int>(arg(1).n) : 0;
      if (n <= 0) return Value::list();
      size_t start = list.size() > static_cast<size_t>(n) ? list.size() - n : 0;
      return Value::list(std::vector<Value>(list.begin() + static_cast<long long>(start), list.end()));
    }
    case B_random_choice: {
      if (arg(0).t != VT::List || arg(0).l().empty()) return Value();
      const auto& list = arg(0).l();
      return list[rng_.index(list.size())];
    }
    case B_str: return Value::str(stringify(arg(0)));
    case B_print: {
      LogEntry entry;
      entry.round = round_;
      entry.kind = LogKind::Print;
      entry.text = stringify(arg(0));
      emit(std::move(entry));
      return Value();
    }
    case B_check_win: return checkWin();
    case B_locations: {
      std::vector<Value> out;
      out.reserve(locations_.size());
      for (size_t i = 0; i < locations_.size(); ++i) out.push_back(Value::loc(static_cast<int>(i)));
      return Value::list(std::move(out));
    }
    case B_locations_by_tag: {
      std::string tag = stringify(arg(0));
      std::vector<Value> out;
      for (size_t i = 0; i < locations_.size(); ++i)
        if (locations_[i].hasTag && locations_[i].tag == tag) out.push_back(Value::loc(static_cast<int>(i)));
      return Value::list(std::move(out));
    }
    case B_spawn_agents_at: {
      auto list = agentList(arg(0), "spawn_agents_at", line);
      const Value* tag = args.size() > 1 ? &args[1] : kw.get(S_tag);
      bool filter = tag && !tag->isNull();
      std::string tagStr = filter ? stringify(*tag) : "";
      std::vector<int> candidates;
      for (size_t i = 0; i < locations_.size(); ++i)
        if (!filter || (locations_[i].hasTag && locations_[i].tag == tagStr)) candidates.push_back(static_cast<int>(i));
      if (candidates.empty())
        throw RuntimeError("spawn_agents_at: no locations match" + (filter ? " tag '" + tagStr + "'" : ""), line);
      for (int a : list) {
        int l = candidates[rng_.index(candidates.size())];
        agents_[a].location = l;
        agents_[a].x = locations_[l].x;
        agents_[a].y = locations_[l].y;
        agents_[a].hasPos = true;
      }
      return Value();
    }
    case B_move_to: {
      int a = agentArg(args, 0, "move_to", line);
      int l = locArg(1, "move_to");
      agents_[a].location = l;
      agents_[a].x = locations_[l].x;
      agents_[a].y = locations_[l].y;
      agents_[a].hasPos = true;
      return Value();
    }
    case B_location_of: {
      int a = agentArg(args, 0, "location_of", line);
      return agents_[a].location >= 0 ? Value::loc(agents_[a].location) : Value();
    }
    case B_agents_at: {
      int l = locArg(0, "agents_at");
      return agentsWhere([l](const Agent& ag) { return ag.location == l; });
    }
    case B_nearby: {
      int a = agentArg(args, 0, "nearby", line);
      double radius = arg(1).t == VT::Num ? arg(1).n : kw.num(S_radius, 5.0);
      const Agent& self = agents_[a];
      if (!self.hasPos) return Value::list();
      double r2 = radius * radius;
      std::vector<Value> out;
      for (size_t i = 0; i < agents_.size(); ++i) {
        const Agent& o = agents_[i];
        if (static_cast<int>(i) == a || !o.hasPos) continue;
        double dx = o.x - self.x, dy = o.y - self.y;
        if (dx * dx + dy * dy <= r2) out.push_back(Value::agent(static_cast<int>(i)));
      }
      return Value::list(std::move(out));
    }
    default: throw RuntimeError("unknown builtin", line);
  }
}

}  // namespace sl
