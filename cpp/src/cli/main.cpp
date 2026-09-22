// sl_run -- headless SocialLang runner: engine smoke tests and benchmarks
// without opening a window.
//
//   sl_run games/city.sl                   run with the mock provider
//   sl_run games/mafia.sl --seed 7 --log   print every log line
//   sl_run games/village.sl --models       use the roster from Model Settings
//   sl_run games/city.sl --bench 20        time 20 full runs
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "engine/interpreter.h"
#include "engine/parser.h"
#include "providers/settings.h"

using namespace sl;

namespace {

std::vector<RosterEntry> buildRoster(bool useSettings, uint32_t seed) {
  std::vector<RosterEntry> roster;
  if (useSettings) {
    Settings s = Settings::load();
    for (auto& e : s.roster)
      if (e.enabled) roster.push_back({e.name, makeProvider(e, s, seed)});
  }
  if (roster.empty()) roster.push_back({"mock", std::make_shared<MockProvider>(seed)});
  return roster;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: sl_run <game.sl> [--seed N] [--max-rounds N] [--log] [--models] [--bench N]\n");
    return 2;
  }
  std::string path = argv[1];
  uint32_t seed = 1;
  int maxRounds = 300, bench = 0;
  bool log = false, useModels = false;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--seed" && i + 1 < argc) seed = static_cast<uint32_t>(std::stoul(argv[++i]));
    else if (a == "--max-rounds" && i + 1 < argc) maxRounds = std::stoi(argv[++i]);
    else if (a == "--bench" && i + 1 < argc) bench = std::stoi(argv[++i]);
    else if (a == "--log") log = true;
    else if (a == "--models") useModels = true;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", path.c_str());
    return 2;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string source = ss.str();

  try {
    auto program = parseProgram(source);
    int runs = bench > 0 ? bench : 1;
    double totalMs = 0;
    for (int r = 0; r < runs; ++r) {
      long long logLines = 0;
      auto roster = buildRoster(useModels, seed + r);
      auto t0 = std::chrono::steady_clock::now();
      Interpreter interp(program, roster, seed + r, [&](const LogEntry& e) {
        ++logLines;
        if (log)
          std::printf("[r%d] %-10s %s%s\n", e.round, logKindName(e.kind),
                      e.author.empty() ? "" : (e.author + ": ").c_str(), e.text.c_str());
      });
      Interpreter::StepResult res;
      while (!res.done && interp.round() < maxRounds) res = interp.step();
      double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      totalMs += ms;
      if (r == 0 || bench == 0) {
        int alive = 0;
        for (auto& a : interp.agents()) alive += a.alive;
        std::printf("%s: %zu agents (%d alive), %zu locations, %d rounds, %lld log lines, winner=%s, %.1f ms\n",
                    program->name.c_str(), interp.agents().size(), alive, interp.locations().size(), interp.round(),
                    logLines, interp.stringify(res.winner).c_str(), ms);
        if (interp.stats.errors > 0) std::printf("  provider errors: %lld\n", interp.stats.errors.load());
      }
    }
    if (bench > 0) std::printf("bench: %d runs, %.2f ms/run\n", bench, totalMs / bench);
  } catch (const RuntimeError& e) {
    std::fprintf(stderr, "runtime error (line %d): %s\n", e.line, e.what());
    return 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
  return 0;
}
