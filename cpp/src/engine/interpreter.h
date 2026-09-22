// Tree-walking interpreter for SocialLang -- a port of
// sociallang/lang/interpreter.py (via the browser engine) with the same
// builtins, memory patterns, and Generative Agents mechanisms.
//
// Performance notes, since a run can mean thousands of agents:
//   * control flow (return/break) is a returned Flow code, never an exception;
//   * scopes are stack-allocated with inline storage -- no heap per block;
//   * an agent's memory context is only built when its provider will read it
//     (a mock run never builds one);
//   * maybe_reflect's "importance since last reflection" is a running sum,
//     not a rescan of every visible event.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/ast.h"
#include "engine/rng.h"
#include "engine/value.h"
#include "providers/provider.h"

namespace sl {

enum class LogKind : uint8_t { Ask, Broadcast, Whisper, Note, Reflection, Plan, Dialogue, Print, Error };
const char* logKindName(LogKind k);

struct LogEntry {
  int seq = -1;  // event sequence number; -1 for print() and provider errors
  int round = 0;
  LogKind kind = LogKind::Print;
  std::string text;
  std::string author;          // seat, a free-form author, or empty
  std::vector<int> visibleTo;  // agent indices; empty = public
};

struct Agent {
  std::string seat, role, team, model;
  int roleIdx = 0;
  Provider* provider = nullptr;
  bool alive = true;
  std::string deathCause;
  bool hasDeathCause = false;
  bool hasPos = false;
  double x = 0, y = 0;
  int location = -1;
  Value persona = Value::str("");
  Value plan = Value::list();     // list of {"time", "activity"} dicts
  Value subplan = Value::list();  // list of strings
  int planCursor = 0, subplanCursor = 0;
  // maybe_reflect bookkeeping: public importance total at last reflection,
  // and private importance accumulated since.
  double publicImpMark = 0, privateImpSince = 0;
  Value seatV, roleV, teamV, modelV;  // prebuilt attribute values
};

struct Location {
  std::string id, type, tag;
  bool hasTag = false;
  int capacity = -1;
  double x = 0, y = 0;
};

struct Event {
  int round;
  LogKind kind;
  std::string text;
  std::string author;
  double importance;
};

struct RosterEntry {
  std::string key;
  std::shared_ptr<Provider> provider;
};

struct RunStats {
  std::atomic<long long> calls{0}, promptTokens{0}, completionTokens{0}, errors{0};
};

class Interpreter {
 public:
  using LogSink = std::function<void(const LogEntry&)>;

  // Assigns roles and models to seats and scatters the world, exactly like
  // run_source(): two independently seeded RNGs from the same seed.
  Interpreter(std::shared_ptr<const Program> program, const std::vector<RosterEntry>& roster, uint32_t seed,
              LogSink sink, int maxConcurrency = 8);
  ~Interpreter();

  struct StepResult {
    bool done = false;
    Value winner;
  };
  // One execution of the `loop` block. Throws RuntimeError.
  StepResult step();

  int round() const { return round_; }
  const std::vector<Agent>& agents() const { return agents_; }
  const std::vector<Location>& locations() const { return locations_; }
  bool hasWorld() const { return program_->world != nullptr; }
  int worldWidth() const { return program_->world ? program_->world->width : 0; }
  int worldHeight() const { return program_->world ? program_->world->height : 0; }
  const Program& program() const { return *program_; }
  std::string stringify(const Value& v) const;

  std::atomic<bool> cancel{false};  // set from another thread to stop a long round
  RunStats stats;

 private:
  enum class Flow { Normal, Return, Break };
  struct Env;
  struct Kwargs;

  std::shared_ptr<const Program> program_;
  const SymbolTable& syms_;
  std::vector<Agent> agents_;
  std::vector<Location> locations_;
  std::vector<Event> events_;  // seq = index + 1
  std::vector<int> publicEvents_;
  std::vector<std::vector<int>> privateEvents_;  // per agent
  double publicImpTotal_ = 0;
  int round_ = 0;
  SeededRandom rng_;
  LogSink sink_;
  int maxConcurrency_;
  Value returnValue_;
  std::unique_ptr<Env> global_;
  Value kTimeKey_ = Value::str("time"), kActivityKey_ = Value::str("activity");

  // Execution
  Flow execBlock(const Block& b, Env& env);
  Flow execStmt(const Stmt& s, Env& env);
  Value eval(const Expr& e, Env& env);
  Value evalBinary(const Expr& e, Env& env);
  Value evalAttr(const Expr& e, Env& env);
  Value evalCall(const Expr& e, Env& env);
  void assign(const Expr& target, Value v, Env& env);
  Value callUser(int fnIdx, std::vector<Value>& args);
  Value callBuiltin(int id, std::vector<Value>& args, const Kwargs& kw, int line);
  Value checkWin();
  void checkCancel();

  // Events and memory
  int appendEvent(LogKind kind, std::string text, std::string author, std::vector<int> visibleTo,
                  double importance = -1);
  void emit(LogEntry entry);
  std::vector<int> visibleEventsFor(int agent) const;
  std::vector<int> callMemory(const RoleDecl& role, const std::vector<int>& visible, const std::string& query);
  std::string buildContext(int agent, const std::string& query);
  std::string identityFor(int agent);
  std::string renderEvent(int idx) const;

  // Provider calls. `prompt` is the "Now: ..." instruction; the full user
  // prompt (history + prompt) is only assembled if the provider reads it.
  CompletionRequest makeRequest(int agent, const std::string& prompt, double temperature, int maxTokens);
  std::string runCompletion(int agent, const CompletionRequest& req);
  std::vector<std::string> runCompletions(const std::vector<int>& agents, const std::vector<CompletionRequest>& reqs);

  // Builtin helpers
  int agentArg(const std::vector<Value>& args, size_t i, const char* fn, int line) const;
  std::vector<int> agentList(const Value& v, const char* fn, int line) const;
  Value agentsWhere(const std::function<bool(const Agent&)>& pred) const;
  Value matchOption(const std::string& text, const std::vector<Value>& options, const std::vector<std::string>& labels);
  std::vector<std::string> splitLines(const std::string& text) const;
  std::vector<std::string> reflect(int agent);
  Value currentStep(int agent);
  Value currentAction(int agent);
};

}  // namespace sl
