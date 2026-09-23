// sl_run -- headless SocialLang runner: engine smoke tests and benchmarks
// without opening a window.
//
//   sl_run games/city.sl                        one run, mock provider
//   sl_run games/mafia.sl --seed 7 --log        print every log line
//   sl_run games/village.sl --models            use the roster from Model Settings
//   sl_run games/city.sl --runs 64 --jobs 8     64 seeds, 8 at a time
//   sl_run games/smallville.sl --mock-context   mock that still builds every
//                                               prompt's memory context
//   sl_run ... --serial                         reference path: no parallel
//                                               context building
//
// Every run prints its seed, so any result can be replayed exactly.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "engine/interpreter.h"
#include "engine/parser.h"
#include "providers/settings.h"
#include "ville/sim.h"

using namespace sl;
using Clock = std::chrono::steady_clock;

namespace {

struct Config {
  std::string path;
  uint32_t seed = 1;
  int maxRounds = 300, runs = 1, jobs = 1;
  bool log = false, useModels = false, mockContext = false, serial = false;
};

struct RunResult {
  uint32_t seed = 0;
  size_t agents = 0, locations = 0;
  int alive = 0, rounds = 0;
  long long logLines = 0, calls = 0, errors = 0;
  std::string winner;
  double setupMs = 0, stepMs = 0;
};

std::vector<RosterEntry> buildRoster(const Config& cfg, uint32_t seed) {
  std::vector<RosterEntry> roster;
  if (cfg.useModels) {
    Settings s = Settings::load();
    for (auto& e : s.roster)
      if (e.enabled) roster.push_back({e.name, makeProvider(e, s, seed)});
  }
  if (roster.empty()) roster.push_back({"mock", std::make_shared<MockProvider>(seed, cfg.mockContext)});
  return roster;
}

RunResult runOnce(const std::shared_ptr<const Program>& program, const Config& cfg, uint32_t seed) {
  RunResult r;
  r.seed = seed;
  auto roster = buildRoster(cfg, seed);
  InterpreterOptions opts;
  opts.parallelContext = !cfg.serial;
  auto t0 = Clock::now();
  Interpreter interp(program, roster, seed, [&](const LogEntry& e) {
    ++r.logLines;
    if (cfg.log)
      std::printf("[r%d] %-10s %s%s\n", e.round, logKindName(e.kind), e.author.empty() ? "" : (e.author + ": ").c_str(),
                  e.text.c_str());
  }, opts);
  auto t1 = Clock::now();
  Interpreter::StepResult res;
  while (!res.done && interp.round() < cfg.maxRounds) res = interp.step();
  auto t2 = Clock::now();
  r.setupMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  r.stepMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
  r.agents = interp.agents().size();
  r.locations = interp.locations().size();
  for (auto& a : interp.agents()) r.alive += a.alive;
  r.rounds = interp.round();
  r.winner = interp.stringify(res.winner);
  r.calls = interp.stats.calls;
  r.errors = interp.stats.errors;
  return r;
}

}  // namespace

// sl_run --ville [population] [days] [--serial] [--log]: The Ville, headless.
int runVille(int argc, char** argv) {
  ville::VilleOptions o;
  o.population = argc > 2 ? std::atoi(argv[2]) : 25;
  o.days = argc > 3 ? std::atoi(argv[3]) : 1;
  bool log = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--serial") o.parallel = false;
    if (std::string(argv[i]) == "--log") log = true;
  }
  std::vector<std::shared_ptr<sl::Provider>> roster = {std::make_shared<sl::MockProvider>(1)};
  long long counts[6] = {};
  auto t0 = Clock::now();
  ville::Ville v(o, roster, [&](const ville::VilleEvent& e) {
    ++counts[e.kind];
    if (log && e.kind != 0) std::printf("[%lld] %d %s\n", (long long)e.step, e.kind, e.text.c_str());
  });
  auto t1 = Clock::now();
  while (!v.step()) {}
  auto t2 = Clock::now();
  double setup = std::chrono::duration<double, std::milli>(t1 - t0).count();
  double run = std::chrono::duration<double>(t2 - t1).count();
  int attending = 0, knowParty = 0;
  size_t mem = 0;
  for (auto& a : v.agents()) {
    attending += !a.attending.empty() && a.p.name != "Isabella Rodriguez";
    knowParty += std::find(a.knows.begin(), a.knows.end(), 0) != a.knows.end();
    mem += a.memory.size();
  }
  std::printf("The Ville: %zu agents, %dx%d tiles, %zu sectors, %zu objects, setup %.0f ms\n", v.agents().size(),
              v.world().width(), v.world().height(), v.world().sectors.size(), v.world().objects.size(), setup);
  std::printf("  simulated %lld steps (%d game days) in %.2f s: %.0f steps/s, %.2f game-hours/s, %.0f agent-hours/s\n",
              (long long)v.stepCount(), o.days, run, v.stepCount() / run, v.stepCount() / 360.0 / run,
              v.agents().size() * v.stepCount() / 360.0 / run);
  std::printf("  actions %lld, plans %lld, dialogue lines %lld, reflections %lld, news heard %lld; memories %zu\n",
              counts[0], counts[1], counts[2], counts[3], counts[4], mem);
  std::printf("  party: %d know about it, %d plan to attend\n", knowParty, attending);
  return 0;
}

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "--ville") return runVille(argc, argv);
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: sl_run <game.sl> [--seed N] [--max-rounds N] [--runs N] [--jobs N] [--log] [--models] "
                 "[--mock-context] [--serial]\n");
    return 2;
  }
  Config cfg;
  cfg.path = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string("0"); };
    if (a == "--seed") cfg.seed = static_cast<uint32_t>(std::stoul(next()));
    else if (a == "--max-rounds") cfg.maxRounds = std::stoi(next());
    else if (a == "--runs" || a == "--bench") cfg.runs = std::max(1, std::stoi(next()));
    else if (a == "--jobs") cfg.jobs = std::max(1, std::stoi(next()));
    else if (a == "--log") cfg.log = true;
    else if (a == "--models") cfg.useModels = true;
    else if (a == "--mock-context") cfg.mockContext = true;
    else if (a == "--serial") cfg.serial = true;
  }

  std::ifstream in(cfg.path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", cfg.path.c_str());
    return 2;
  }
  std::stringstream ss;
  ss << in.rdbuf();

  try {
    auto program = parseProgram(ss.str());
    std::vector<RunResult> results(cfg.runs);
    std::atomic<int> next{0};
    std::mutex errMu;
    std::string firstError;

    // Whole runs are independent -- no shared mutable state -- so N of them
    // run on N threads with no synchronization beyond handing out seeds.
    auto t0 = Clock::now();
    auto worker = [&] {
      for (int i = next++; i < cfg.runs; i = next++) {
        try {
          results[i] = runOnce(program, cfg, cfg.seed + static_cast<uint32_t>(i));
        } catch (const std::exception& e) {
          std::lock_guard<std::mutex> lock(errMu);
          if (firstError.empty()) firstError = "seed " + std::to_string(cfg.seed + i) + ": " + e.what();
        }
      }
    };
    std::vector<std::thread> pool;
    for (int j = 0; j < std::min(cfg.jobs, cfg.runs); ++j) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    double wallMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (!firstError.empty()) throw std::runtime_error(firstError);

    const RunResult& first = results[0];
    std::printf("%s: %zu agents (%d alive), %zu locations, %d rounds, %lld log lines, winner=%s (seed %u)\n",
                program->name.c_str(), first.agents, first.alive, first.locations, first.rounds, first.logLines,
                first.winner.c_str(), first.seed);
    std::printf("  setup %.2f ms, stepping %.2f ms, %lld provider calls\n", first.setupMs, first.stepMs, first.calls);
    if (first.errors > 0) std::printf("  provider errors: %lld\n", first.errors);

    if (cfg.runs > 1) {
      double agentRounds = 0, cpuMs = 0;
      for (auto& r : results) {
        agentRounds += static_cast<double>(r.agents) * r.rounds;
        cpuMs += r.setupMs + r.stepMs;
      }
      std::printf("  %d runs on %d threads: %.1f ms wall, %.2f ms/run avg, %.0f runs/s, %.2fM agent-rounds/s\n",
                  cfg.runs, std::min(cfg.jobs, cfg.runs), wallMs, cpuMs / cfg.runs, cfg.runs * 1000.0 / wallMs,
                  agentRounds / wallMs / 1000.0);
    }
  } catch (const RuntimeError& e) {
    std::fprintf(stderr, "runtime error (line %d): %s\n", e.line, e.what());
    return 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
  return 0;
}
