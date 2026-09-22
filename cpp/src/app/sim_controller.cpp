#include "app/sim_controller.h"

#include <chrono>
#include <set>

#include "engine/parser.h"

namespace app {

using namespace sl;

SimController::SimController() : thread_([this] { worker(); }) {}

SimController::~SimController() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    quit_ = true;
    if (interp_) interp_->cancel = true;
  }
  cv_.notify_all();
  thread_.join();
}

void SimController::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  if (interp_) interp_->cancel = true;
  interp_.reset();
  ++generation_;
  info_ = SimInfo{};
  pendingLog_.clear();
  pendingFrames_.clear();
  logTotal_ = 0;
  pendingSteps_ = 0;
  playing_ = toEnd_ = false;
}

void SimController::load(const std::string& source, uint32_t seed, const Settings& settings) {
  clear();
  source_ = source;
  seed_ = seed;
  settings_ = settings;

  // Build the roster: every enabled model, or the mock when none are.
  std::vector<RosterEntry> roster;
  for (auto& e : settings.roster)
    if (e.enabled) roster.push_back({e.name.empty() ? e.modelId : e.name, makeProvider(e, settings, seed)});
  if (roster.empty()) roster.push_back({"Mock", std::make_shared<MockProvider>(seed)});

  unsigned gen;
  {
    std::lock_guard<std::mutex> lock(mu_);
    gen = generation_;
  }
  auto sink = [this, gen](const LogEntry& e) {
    std::lock_guard<std::mutex> lock(mu_);
    if (gen != generation_) return;  // a stale run still winding down
    pendingLog_.push_back(e);
    ++logTotal_;
  };

  std::shared_ptr<Interpreter> interp;
  std::string error;
  int errorLine = 0;
  std::string name;
  try {
    auto program = parseProgram(source);
    name = program->name;
    InterpreterOptions opts;
    opts.maxConcurrency = settings.maxConcurrency;
    interp = std::make_shared<Interpreter>(program, roster, seed, sink, opts);
  } catch (const RuntimeError& e) {
    error = e.what();
    errorLine = e.line;
  } catch (const std::exception& e) {
    error = e.what();
  }

  std::lock_guard<std::mutex> lock(mu_);
  roster_ = std::move(roster);
  if (!interp) {
    info_.status = SimStatus::CompileError;
    info_.error = error;
    info_.errorLine = errorLine;
    info_.programName = name;
    return;
  }
  interp_ = interp;
  info_.programName = name;
  for (auto& r : roster_) info_.rosterLabels.push_back(r.provider->label);
  setupFromInterpreter(*interp);
  pendingFrames_.push_back(capture(*interp, 0));
  info_.details = captureDetails(*interp);
  info_.status = SimStatus::Ready;
}

void SimController::setupFromInterpreter(const Interpreter& interp) {
  info_.agents.clear();
  std::set<std::string> teams;
  for (auto& a : interp.agents()) {
    info_.agents.push_back({a.seat, a.role, a.team, a.model});
    teams.insert(a.team);
  }
  info_.hiddenRoles = teams.size() > 1;
  info_.locations = interp.locations();
  info_.hasWorld = interp.hasWorld();
  info_.worldW = interp.worldWidth();
  info_.worldH = interp.worldHeight();
}

void SimController::reset() {
  std::string src;
  uint32_t seed;
  Settings s;
  {
    std::lock_guard<std::mutex> lock(mu_);
    src = source_;
    seed = seed_;
    s = settings_;
  }
  if (!src.empty()) load(src, seed, s);
}

void SimController::step() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    playing_ = toEnd_ = false;
    ++pendingSteps_;
  }
  cv_.notify_all();
}

void SimController::play(double rps) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    roundsPerSecond_ = rps;
    playing_ = true;
    toEnd_ = false;
  }
  cv_.notify_all();
}

void SimController::pause() {
  std::lock_guard<std::mutex> lock(mu_);
  playing_ = toEnd_ = false;
  pendingSteps_ = 0;
}

void SimController::runToEnd() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    toEnd_ = true;
  }
  cv_.notify_all();
}

void SimController::setSpeed(double rps) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    roundsPerSecond_ = rps;
  }
  cv_.notify_all();
}

SimInfo SimController::info() {
  std::lock_guard<std::mutex> lock(mu_);
  SimInfo out = info_;
  out.playing = playing_ || toEnd_;
  if (interp_) {
    out.calls = interp_->stats.calls;
    out.promptTokens = interp_->stats.promptTokens;
    out.completionTokens = interp_->stats.completionTokens;
    out.errors = interp_->stats.errors;
  }
  return out;
}

bool SimController::drain(std::vector<LogEntry>& log, std::vector<std::shared_ptr<const Frame>>& frames) {
  std::lock_guard<std::mutex> lock(mu_);
  bool replaced = drainedGeneration_ != generation_;
  drainedGeneration_ = generation_;
  if (replaced) {
    log.clear();
    frames.clear();
  }
  for (auto& e : pendingLog_) log.push_back(std::move(e));
  pendingLog_.clear();
  for (auto& f : pendingFrames_) frames.push_back(std::move(f));
  pendingFrames_.clear();
  return replaced;
}

std::shared_ptr<const Frame> SimController::capture(const Interpreter& interp, size_t logCount) {
  auto f = std::make_shared<Frame>();
  f->round = interp.round();
  f->logCount = logCount;
  f->agents.reserve(interp.agents().size());
  for (auto& a : interp.agents())
    f->agents.push_back({static_cast<float>(a.x), static_cast<float>(a.y), a.location, a.alive, a.hasPos});
  return f;
}

std::shared_ptr<const std::vector<AgentDetail>> SimController::captureDetails(const Interpreter& interp) {
  auto out = std::make_shared<std::vector<AgentDetail>>();
  out->reserve(interp.agents().size());
  for (auto& a : interp.agents()) {
    AgentDetail d;
    d.hasDeathCause = a.hasDeathCause;
    d.deathCause = a.deathCause;
    d.persona = a.persona.s();
    for (auto& step : a.plan.l()) {
      if (step.t != VT::Dict) continue;
      std::string time, activity;
      for (auto& [k, v] : step.d().entries) {
        if (k.t != VT::Str) continue;
        if (k.s() == "time") time = interp.stringify(v);
        else if (k.s() == "activity") activity = interp.stringify(v);
      }
      d.plan.emplace_back(std::move(time), std::move(activity));
    }
    d.planCursor = a.planCursor;
    for (auto& s : a.subplan.l()) d.subplan.push_back(interp.stringify(s));
    d.subplanCursor = a.subplanCursor;
    out->push_back(std::move(d));
  }
  return out;
}

void SimController::worker() {
  std::unique_lock<std::mutex> lock(mu_);
  while (!quit_) {
    cv_.wait(lock, [&] { return quit_ || pendingSteps_ > 0 || playing_ || toEnd_; });
    if (quit_) break;
    auto interp = interp_;
    bool runnable = interp && (info_.status == SimStatus::Ready || info_.status == SimStatus::Paused);
    if (!runnable) {
      pendingSteps_ = 0;
      playing_ = toEnd_ = false;
      continue;
    }
    unsigned gen = generation_;
    info_.status = SimStatus::Running;
    info_.busy = true;
    lock.unlock();

    Interpreter::StepResult res;
    std::string error;
    int errorLine = 0;
    std::shared_ptr<const Frame> frame;
    std::shared_ptr<const std::vector<AgentDetail>> details;
    bool failed = false;
    try {
      res = interp->step();
    } catch (const RuntimeError& e) {
      failed = true;
      error = e.what();
      errorLine = e.line;
    } catch (const std::exception& e) {
      failed = true;
      error = e.what();
    }
    if (!failed) details = captureDetails(*interp);

    lock.lock();
    info_.busy = false;
    if (gen != generation_) continue;  // replaced mid-round; drop the result
    if (!failed) frame = capture(*interp, logTotal_);
    if (failed) {
      info_.status = SimStatus::RuntimeError;
      info_.error = error;
      info_.errorLine = errorLine;
      LogEntry e;
      e.round = interp->round();
      e.kind = LogKind::Error;
      e.text = errorLine ? "line " + std::to_string(errorLine) + ": " + error : error;
      pendingLog_.push_back(std::move(e));
      ++logTotal_;
      pendingSteps_ = 0;
      playing_ = toEnd_ = false;
      continue;
    }
    pendingFrames_.push_back(frame);
    info_.details = details;
    info_.round = interp->round();
    if (res.done) {
      info_.status = SimStatus::Done;
      info_.winner = interp->stringify(res.winner);
      pendingSteps_ = 0;
      playing_ = toEnd_ = false;
      continue;
    }
    info_.status = SimStatus::Paused;
    if (pendingSteps_ > 0) --pendingSteps_;
    if (playing_ && !toEnd_) {
      // Pace Play mode; wakes early on pause, run-to-end, reload, or quit.
      auto wait = std::chrono::duration<double>(1.0 / std::max(0.1, roundsPerSecond_));
      cv_.wait_for(lock, wait, [&] { return quit_ || !playing_ || toEnd_ || gen != generation_; });
    }
  }
}

}  // namespace app
