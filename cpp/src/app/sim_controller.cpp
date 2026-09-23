#include "app/sim_controller.h"

#include <chrono>
#include <set>

#include "engine/parser.h"

#include <algorithm>

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
  if (ville_) ville_->cancel = true;
  interp_.reset();
  ville_.reset();
  villePopulation_ = 0;
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
  int pop;
  {
    std::lock_guard<std::mutex> lock(mu_);
    src = source_;
    seed = seed_;
    s = settings_;
    pop = villePopulation_;
  }
  ville::TownSpec spec;
  {
    std::lock_guard<std::mutex> lock(mu_);
    spec = villeSpec_;
  }
  if (pop > 0) loadVille(pop, seed, s, spec);
  else if (!src.empty()) load(src, seed, s);
}

void SimController::setRules(const ville::TownSpec& spec) {
  std::lock_guard<std::mutex> lock(mu_);
  villeSpec_.rules = spec.rules;
  villeSpec_.groupRules = spec.groupRules;
  villeSpec_.residentRules = spec.residentRules;
  pendingRules_ = villeSpec_;
  rulesPending_ = true;
}

void SimController::loadVille(int population, uint32_t seed, const Settings& settings, const ville::TownSpec& spec) {
  clear();
  std::vector<std::shared_ptr<Provider>> roster;
  std::vector<std::string> labels;
  for (auto& e : settings.roster)
    if (e.enabled) {
      roster.push_back(makeProvider(e, settings, seed));
      labels.push_back(roster.back()->label);
    }
  if (roster.empty()) {
    roster.push_back(std::make_shared<MockProvider>(seed));
    labels.push_back("Mock");
  }
  unsigned gen;
  {
    std::lock_guard<std::mutex> lock(mu_);
    gen = generation_;
    seed_ = seed;
    settings_ = settings;
    source_.clear();
    villePopulation_ = population;
    villeSpec_ = spec;
    rulesPending_ = false;
  }
  ville::VilleOptions o;
  o.population = population;
  o.seed = seed;
  o.maxConcurrency = settings.maxConcurrency;
  o.spec = spec;
  auto v = std::make_shared<ville::Ville>(o, roster, [this, gen](const ville::VilleEvent& e) {
    std::lock_guard<std::mutex> lock(mu_);
    if (gen != generation_ || !ville_) return;
    static const LogKind kinds[] = {LogKind::Note, LogKind::Plan, LogKind::Dialogue, LogKind::Reflection,
                                    LogKind::Whisper, LogKind::Error};
    LogEntry l;
    l.seq = static_cast<int>(e.step);
    l.round = static_cast<int>(e.step);
    l.kind = kinds[std::clamp(e.kind, 0, 5)];
    l.author = ville_->agents()[e.agent].p.name;
    l.text = e.text;
    if (e.other >= 0) l.visibleTo = {e.agent, e.other};
    pendingLog_.push_back(std::move(l));
    ++logTotal_;
  });
  std::lock_guard<std::mutex> lock(mu_);
  ville_ = v;
  info_.isVille = true;
  info_.population = population;
  info_.programName = "The Ville";
  info_.rosterLabels = labels;
  info_.hasWorld = true;
  info_.worldW = v->world().width();
  info_.worldH = v->world().height();
  info_.villeWorld = std::make_shared<ville::World>(v->world());
  info_.agents.clear();
  for (auto& a : v->agents()) {
    std::string arch = a.p.archetype;
    std::replace(arch.begin(), arch.end(), '_', ' ');
    info_.agents.push_back({a.p.name, arch, a.p.home >= 0 ? v->world().sectors[a.p.home].name : "", a.provider ? a.provider->label : "Mock"});
  }
  // Rooms as the Town panel's locations, grouped by building.
  info_.locations.clear();
  for (auto& ar : v->world().arenas) {
    sl::Location l;
    l.id = ar.name;
    l.type = v->world().sectors[ar.sector].name;
    l.x = ar.rect.x + ar.rect.w * 0.5;
    l.y = ar.rect.y + ar.rect.h * 0.5;
    info_.locations.push_back(l);
  }
  info_.hiddenRoles = false;
  captureVille(*v, true);
  info_.status = SimStatus::Ready;
}

// Snapshot of the town for the UI: positions every step, the string-heavy
// resident details only a few times a second (thousands of residents).
void SimController::captureVille(const ville::Ville& v, bool withDetails) {
  auto f = std::make_shared<Frame>();
  f->round = static_cast<int>(v.stepCount());
  f->logCount = logTotal_;
  f->agents.reserve(v.agents().size());
  const auto& world = v.world();
  for (auto& a : v.agents()) f->agents.push_back({a.x + 0.5f, a.y + 0.5f, world.arenaAt(a.x, a.y), true, true});
  if (pendingFrames_.size() > 8) pendingFrames_.erase(pendingFrames_.begin());
  pendingFrames_.push_back(f);
  info_.round = static_cast<int>(v.stepCount());
  info_.step = v.stepCount();
  info_.clock = v.clockText();
  info_.calls = v.calls;
  info_.errors = v.errors;
  if (!withDetails) return;
  auto busy = std::make_shared<std::vector<char>>(world.objects.size(), 0);
  for (auto& a : v.agents())
    if (a.usingObject >= 0) (*busy)[a.usingObject] = 1;
  info_.objectBusy = busy;
  auto out = std::make_shared<std::vector<AgentDetail>>();
  out->reserve(v.agents().size());
  static const char* kTypes = "";
  (void)kTypes;
  for (size_t i = 0; i < v.agents().size(); ++i) {
    const ville::Agent& a = v.agents()[i];
    AgentDetail d;
    d.persona = a.p.learned;
    d.innate = a.p.innate;
    d.learned = a.p.learned;
    d.currently = a.p.name + " is " + a.p.currently + ".";
    d.lifestyle = a.p.lifestyle;
    d.homeName = a.p.home >= 0 ? world.sectors[a.p.home].name : "";
    d.workName = a.p.work >= 0 ? world.sectors[a.p.work].name : "home";
    d.action = a.action;
    d.emoji = a.emoji;
    d.address = a.addressText;
    d.chatWith = a.chatWith;
    if (a.chatWith >= 0 && a.chatNext > 0 && a.chatNext <= a.chatLines.size()) d.utterance = a.chatLines[a.chatNext - 1].second;
    d.dailyPlan = a.dailyPlan;
    for (size_t h = 0; h < a.schedule.size(); ++h) {
      char t[16];
      std::snprintf(t, sizeof t, "%02zu:00", h);
      d.plan.push_back({t, a.schedule[h].activity});
    }
    d.planCursor = a.slot;
    for (auto& t : a.tasks) d.subplan.push_back(t.desc + " (" + std::to_string(t.minutes) + " min)");
    d.subplanCursor = a.task;
    d.memoryCount = static_cast<int>(a.memory.size());
    for (size_t m = a.memory.size(); m-- > 0 && d.memories.size() < 25;) {
      const auto& n = a.memory[m];
      char imp[24];
      std::snprintf(imp, sizeof imp, "  [%.0f]", n.poignancy);
      d.memories.push_back({static_cast<int>(n.type), n.description + imp});
    }
    std::vector<std::pair<float, int>> rel;
    for (auto& [who, fam] : a.familiarity) rel.push_back({fam, who});
    std::sort(rel.rbegin(), rel.rend());
    for (size_t k = 0; k < rel.size() && k < 8; ++k) d.relations.push_back({v.agents()[rel[k].second].p.name, rel[k].first});
    for (int n : a.knows) d.knows.push_back(v.news()[n].text + (std::find(a.attending.begin(), a.attending.end(), n) != a.attending.end() ? "  (going)" : ""));
    out->push_back(std::move(d));
  }
  info_.details = out;
  info_.villeWorld = std::make_shared<ville::World>(world);  // refreshed object states
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
  if (ville_) {
    out.calls = ville_->calls;
    out.errors = ville_->errors;
  } else if (interp_) {
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
    if (ville_) {
      auto v = ville_;
      bool ok = info_.status == SimStatus::Ready || info_.status == SimStatus::Paused;
      if (!ok) {
        pendingSteps_ = 0;
        playing_ = toEnd_ = false;
        continue;
      }
      unsigned gen = generation_;
      if (rulesPending_) {  // between steps: safe to swap the rules
        v->setRules(pendingRules_);
        rulesPending_ = false;
      }
      info_.status = SimStatus::Running;
      info_.busy = true;
      lock.unlock();
      bool done = false;
      std::string err;
      try {
        done = v->step();
      } catch (const std::exception& e) {
        err = e.what();
      }
      auto now = std::chrono::steady_clock::now().time_since_epoch();
      double t = std::chrono::duration<double>(now).count();
      lock.lock();
      info_.busy = false;
      if (gen != generation_) continue;
      bool details = t - lastDetailCapture_ > 0.12 || !playing_ || pendingSteps_ > 0 || done;
      if (details) lastDetailCapture_ = t;
      captureVille(*v, details);
      if (!err.empty()) {
        info_.status = SimStatus::RuntimeError;
        info_.error = err;
        pendingSteps_ = 0;
        playing_ = toEnd_ = false;
        continue;
      }
      if (done) {
        info_.status = SimStatus::Done;
        info_.winner = "end of day " + std::to_string(v->dayIndex());
        pendingSteps_ = 0;
        playing_ = toEnd_ = false;
        captureVille(*v, true);
        continue;
      }
      info_.status = SimStatus::Paused;
      if (pendingSteps_ > 0) --pendingSteps_;
      if (playing_ && !toEnd_) {
        auto wait = std::chrono::duration<double>(1.0 / std::max(0.1, roundsPerSecond_));
        cv_.wait_for(lock, wait, [&] { return quit_ || !playing_ || toEnd_ || gen != generation_; });
      }
      continue;
    }
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
