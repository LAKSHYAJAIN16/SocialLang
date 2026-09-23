// The Ville simulation: Generative Agents (Park et al. 2023) at scale.
//
// Every agent runs the paper's loop each step (10 game-seconds):
//   perceive  -- nearby agents' actions and object states, within its vision
//               radius and current arena, become observations in its memory
//               stream (skipping ones it has recently recorded)
//   retrieve  -- memories scored by recency + importance + relevance
//   plan      -- on waking: a broad daily plan, then an hourly schedule; each
//               hour is decomposed into 5-15 minute tasks, each resolved to a
//               world:sector:arena:object address, an emoji, and a path
//   react     -- on seeing someone, decide whether to talk; conversations are
//               generated turn by turn, grounded in what each agent remembers,
//               and pass knowledge on (how the Valentine's party invite spreads)
//   act       -- walk the path one tile per step; objects in use change state
//   reflect   -- once accumulated importance crosses a threshold, synthesize
//               higher-level thoughts back into memory
//
// Cognition is pluggable: an LLM (the paper's setup, via the provider layer)
// or an offline persona-driven model for runs far larger than an LLM budget
// allows (see cognition.h). The simulation is deterministic for a seed.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/rng.h"
#include "providers/provider.h"
#include "ville/spec.h"
#include "ville/world.h"

namespace ville {

constexpr int kSecondsPerStep = 10;
constexpr int kStepsPerMinute = 60 / kSecondsPerStep;
constexpr int kStepsPerHour = 60 * kStepsPerMinute;
constexpr int kStepsPerDay = 24 * kStepsPerHour;

enum class NodeType : uint8_t { Event, Chat, Thought };

struct MemoryNode {
  NodeType type = NodeType::Event;
  int64_t created = 0, lastAccess = 0;  // steps
  std::string subject, predicate, object, description;
  float poignancy = 1;  // 1-10, as in the paper
  int news = -1;        // a piece of news this memory carries, if any
  int person = -1;      // the other agent involved, if any
};

// Something worth passing on: an event invite or a piece of town news.
struct News {
  std::string text;  // "Isabella Rodriguez is hosting a Valentine's Day party at Hobbs Cafe on February 14th, 5-7 pm."
  int origin = -1;
  bool invite = false;
  int sector = -1;             // where the event happens
  int day = 0, startMin = 0, endMin = 0;  // day index, minutes since midnight
  std::string activity;       // "attending Isabella's Valentine's Day party"
};

struct Persona {
  std::string name, first, innate, learned, currently, lifestyle;
  int age = 30;
  std::string archetype;  // key into the schedule templates (cognition.cpp)
  int home = -1, work = -1;  // sectors
  std::string bedArena;      // which bedroom is theirs
  int wakeHour = 7, sleepHour = 23;
  float sociability = 0.5f;
};

struct HourSlot {
  std::string activity;
  std::string place;  // "home", "work", "cafe", "park", "pub", "college", "library", "market", "store", "townhall", "event:N"
  int minutes = 60;
};

struct Task {
  std::string desc;     // "brewing coffee for customers"
  std::string place;    // as HourSlot::place
  std::string objectKw; // "coffee machine"
  int minutes = 10;
  std::string emoji;
};

struct Agent {
  Persona p;
  int x = 0, y = 0;
  std::vector<std::pair<int16_t, int16_t>> path;

  // Planning state
  int day = -1;  // day index the current plan is for
  std::vector<std::string> dailyPlan;
  std::vector<HourSlot> schedule;
  int slot = -1, slotStartMin = 0;
  std::vector<Task> tasks;
  int task = -1;
  int64_t taskEnd = 0;
  std::string action = "sleeping", emoji = "z", addressText;
  int targetObject = -1, usingObject = -1;
  bool asleep = true;

  // Conversation
  int chatWith = -1;
  int64_t chatEnd = 0;
  std::vector<std::pair<int, std::string>> chatLines;  // (speaker, line)
  std::vector<int64_t> chatLineAt;
  size_t chatNext = 0;
  std::unordered_map<int, int64_t> lastChat;
  std::unordered_map<int, float> familiarity;
  std::unordered_map<int, std::string> relationNote;  // "has a crush on", "rivals", ... (from the town spec)

  // Memory
  std::vector<MemoryNode> memory;
  float importanceSinceReflect = 0;
  std::vector<int> knows;  // news indices
  // What this agent last saw each agent / object doing (hash of the action),
  // so an unchanged scene isn't re-recorded every step.
  std::unordered_map<int, size_t> lastSeen;
  std::vector<int> attending;  // invites accepted

  sl::Provider* provider = nullptr;
  uint32_t rngState = 0;
};

class Cognition;

struct VilleEvent {  // a log line for the UI
  int64_t step;
  int kind;  // 0 action, 1 plan, 2 dialogue, 3 reflection, 4 news, 5 error
  int agent;
  int other = -1;
  std::string text;
};

struct VilleOptions {
  int population = 25;
  uint32_t seed = 1;
  int days = 2;                 // the run ends after this many game days
  int startHour = 6;            // day 1 starts at this hour (Feb 13, 2023)
  TownSpec spec;                // building / resident customizations and the social rules
  bool parallel = true;         // perceive / plan / move on the worker pool
  bool logActions = true;       // one log line per task start (auto-off past 200 agents)
  int maxConcurrency = 8;
};

class Ville {
 public:
  // roster: providers dealt to agents; if every one is the mock, cognition is
  // the offline persona model, otherwise the LLM one.
  Ville(const VilleOptions& opts, const std::vector<std::shared_ptr<sl::Provider>>& roster,
        std::function<void(const VilleEvent&)> sink);
  ~Ville();

  // One step (10 game-seconds). Returns true when the run is over.
  bool step();

  int64_t stepCount() const { return step_; }
  int64_t minutesSinceStart() const;
  int dayIndex() const;
  int minuteOfDay() const;
  std::string clockText() const;  // "Monday, February 13, 2023 -- 08:32 am"

  const World& world() const { return world_; }
  const std::vector<Agent>& agents() const { return agents_; }
  const std::vector<News>& news() const { return news_; }
  const VilleOptions& options() const { return opts_; }
  const SocialRules& rules() const { return opts_.spec.rules; }
  // The rules one resident follows (their own, their group's, or the town's).
  const SocialRules& rulesFor(int agent) const {
    return opts_.spec.rulesFor(agents_[agent].p.name, agents_[agent].p.archetype);
  }
  // Applied between steps (the caller must not be inside step()).
  void setRules(const TownSpec& spec) {
    opts_.spec.rules = spec.rules;
    opts_.spec.groupRules = spec.groupRules;
    opts_.spec.residentRules = spec.residentRules;
  }
  bool llmCognition() const { return llm_; }

  std::atomic<bool> cancel{false};
  std::atomic<long long> calls{0}, errors{0};

  // For Cognition implementations
  std::vector<int> retrieve(int agent, const std::string& focal, int k) const;
  int resolveSector(const Agent& a, const std::string& place) const;
  void emit(int kind, int agent, const std::string& text, int other = -1);
  sl::SeededRandom rngFor(int agent, uint32_t salt) const;

 private:
  VilleOptions opts_;
  World world_;
  std::vector<Agent> agents_;
  std::vector<News> news_;
  std::unique_ptr<Cognition> cog_;
  bool llm_ = false;
  int64_t step_ = 0;
  std::function<void(const VilleEvent&)> sink_;
  std::vector<std::shared_ptr<sl::Provider>> roster_;
  // spatial hash for perception: cell -> agent indices
  std::vector<std::vector<int>> grid_;
  int gridW_ = 0, gridH_ = 0;

  void setupAgents();
  void rebuildGrid();
  void perceive(int i, std::vector<MemoryNode>& out);
  void addMemory(int i, MemoryNode node);
  void planDay(int i);
  void startSlot(int i, int slot);
  void startTask(int i, int task);
  void beginChat(int a, int b);
  void finishChat(int a);
  void reflect(int i);
  void learn(int i, int news, int from);
  void applyInvites(int i);
};

}  // namespace ville
