// Owns the running simulation on a dedicated worker thread, so the editor
// stays at full frame rate while a round waits on real LLM calls. The UI
// never touches the Interpreter directly: after every round the worker
// publishes an immutable snapshot, and log lines stream in as they happen.
#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine/interpreter.h"
#include "providers/settings.h"

namespace app {

// Per-agent state that changes every round -- all the Scene view and the
// timeline need, kept compact so thousands of agents x hundreds of rounds
// stays cheap to keep in memory for scrubbing.
struct AgentLive {
  float x = 0, y = 0;
  int location = -1;
  bool alive = true, hasPos = false;
};

struct Frame {
  int round = 0;
  std::vector<AgentLive> agents;
  size_t logCount = 0;  // log lines emitted up to the end of this round
};

// The Inspector's richer, string-heavy view -- only the latest round's.
struct AgentDetail {
  bool hasDeathCause = false;
  std::string deathCause, persona;
  std::vector<std::pair<std::string, std::string>> plan;  // (time, activity)
  int planCursor = 0;
  std::vector<std::string> subplan;
  int subplanCursor = 0;
};

struct AgentStatic {
  std::string seat, role, team, model;
};

enum class SimStatus { Empty, CompileError, Ready, Paused, Running, Done, RuntimeError };

struct SimInfo {
  SimStatus status = SimStatus::Empty;
  std::string programName, error, winner;
  int errorLine = 0;
  int round = 0;
  bool busy = false;  // the worker is inside a round right now
  bool playing = false;
  bool hiddenRoles = false;
  bool hasWorld = false;
  int worldW = 0, worldH = 0;
  long long calls = 0, promptTokens = 0, completionTokens = 0, errors = 0;
  std::vector<AgentStatic> agents;
  std::vector<sl::Location> locations;
  std::shared_ptr<const std::vector<AgentDetail>> details;
  std::vector<std::string> rosterLabels;
};

class SimController {
 public:
  SimController();
  ~SimController();

  // Parses and sets up a fresh run (round 0). On a compile or setup error the
  // status becomes CompileError and the message is in info().error.
  void load(const std::string& source, uint32_t seed, const sl::Settings& settings);
  void reset();  // same source, seed, and roster
  void clear();

  void step();
  void play(double roundsPerSecond);
  void pause();
  void runToEnd();
  void setSpeed(double roundsPerSecond);

  // Snapshot of everything small. Cheap: static data is shared, not copied.
  SimInfo info();
  // Appends log lines and frames published since the last call; returns
  // true if the run was replaced (the caller should clear its copies first).
  bool drain(std::vector<sl::LogEntry>& log, std::vector<std::shared_ptr<const Frame>>& frames);

 private:
  void worker();
  std::shared_ptr<const Frame> capture(const sl::Interpreter& interp, size_t logCount);
  std::shared_ptr<const std::vector<AgentDetail>> captureDetails(const sl::Interpreter& interp);
  void setupFromInterpreter(const sl::Interpreter& interp);

  std::mutex mu_;
  std::condition_variable cv_;
  std::thread thread_;
  bool quit_ = false;

  std::shared_ptr<sl::Interpreter> interp_;
  std::vector<sl::RosterEntry> roster_;
  unsigned generation_ = 0, drainedGeneration_ = 0;

  // Commands
  int pendingSteps_ = 0;
  bool playing_ = false, toEnd_ = false;
  double roundsPerSecond_ = 4;

  // Published state
  SimInfo info_;
  std::vector<sl::LogEntry> pendingLog_;
  std::vector<std::shared_ptr<const Frame>> pendingFrames_;
  size_t logTotal_ = 0;

  // For reset()
  std::string source_;
  uint32_t seed_ = 1;
  sl::Settings settings_;
};

}  // namespace app
