// The town simulation: generative agents at scale, defined entirely by an
// environment file and a behavior file (see spec.h).
//
// Every agent runs this loop each step (10 game-seconds):
//   perceive  -- nearby agents' actions and object states, within its vision
//               radius and current arena, become observations in its memory
//               stream (skipping ones it has recently recorded)
//   retrieve  -- memories scored by recency + importance + relevance
//   plan      -- on waking: a broad daily plan, then an hourly schedule; each
//               hour is decomposed into 5-15 minute tasks, each resolved to a
//               world:sector:arena:object address, an emoji, and a path
//   react     -- on seeing someone, decide whether to talk; conversations are
//               generated turn by turn, grounded in what each agent remembers,
//               and pass knowledge on (how an event's invitation spreads)
//   act       -- walk the path one tile per step; objects in use change state
//   reflect   -- once accumulated importance crosses a threshold, synthesize
//               higher-level thoughts back into memory
//
// Cognition is pluggable: an LLM through the provider layer, or an offline
// persona model driven by the behavior file for runs far larger than an LLM
// budget allows (see cognition.h). The simulation is deterministic for a seed.
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
constexpr int kGridCell = 8;  // perception grid cell, in tiles

enum class NodeType : uint8_t { Event, Chat, Thought };

struct MemoryNode {
  NodeType type = NodeType::Event;
  int64_t created = 0, lastAccess = 0;  // steps
  std::string subject, predicate, object, description;
  float poignancy = 1;  // importance, 1-10
  int news = -1;        // a piece of news this memory carries, if any
  int person = -1;      // the other agent involved, if any
};

// Something worth passing on: an event invite or a piece of town news.
struct News {
  std::string name;  // the event's name ("" for plain news)
  std::string text;
  int origin = -1;
  bool invite = false;
  int sector = -1;             // where the event happens
  int day = 0, startMin = 0, endMin = 0;  // day index, minutes since midnight
  std::string activity;       // what a guest's schedule says during it
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

// Someone seen somewhere at some time: what a mystery's evidence is made of.
// `from` is who told us (-1: we saw it ourselves).
struct Sighting {
  int who = -1, sector = -1;  // sector -1: out on the street
  int64_t minute = 0;         // minutes since the start of day 0
  int from = -1;
  int fleeing = -1;           // the sector they were hurrying away from, if they were
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
  int chatNews = -1;  // the news this conversation is about, if any
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

  // Mysteries
  bool dead = false, arrested = false;
  std::vector<Sighting> sightings;
  std::unordered_map<int, std::pair<int, int64_t>> lastSighting;  // who -> (sector, minute)
  // Someone whose story ("I was home all night") contradicts what this
  // resident saw: who -> the sighting that caught them.
  std::unordered_map<int, Sighting> caughtLying;

  sl::Provider* provider = nullptr;
  uint32_t rngState = 0;
};

// The state of a mystery (see MysterySpec).
struct Crime {
  int victim = -1, sector = -1;
  int64_t at = 0;             // minutes since the start of day 0
  bool discovered = false;
  int finder = -1;
};

struct Verdict {
  int day = 0;
  std::vector<std::pair<int, int>> votes;  // (voter, suspect or -1)
  int accused = -1;                        // -1: no majority, nobody arrested
  bool correct = false;
};

struct CaseState {
  bool on = false;
  std::string name;
  enum Phase { Waiting, Hunting, Undiscovered, Investigating, Solved, KillerWon } phase = Waiting;
  int killer = -1, target = -1, scapegoat = -1;
  int searcher = -1;  // sent looking for an undiscovered victim
  int crimeDay = 0, crimeMinute = 0;  // the next (or current) crime
  int meetingSector = -1, meetingDay = -1;
  int news = -1;  // the latest "found dead" news
  int round = 1;
  std::vector<Crime> crimes;
  std::vector<Verdict> verdicts;
  std::string outcome;
};

class Cognition;

struct VilleEvent {  // a log line for the UI
  int64_t step;
  int kind;  // 0 action, 1 plan, 2 dialogue, 3 reflection, 4 news, 5 error, 6 case (public), 7 secret
  int agent;
  int other = -1;
  std::string text;
};

struct VilleOptions {
  TownSpec spec;                // the environment + behavior files: everything about the town
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
  std::string clockText() const;  // "Monday, February 13, 2023 -- 8:32 am", from the environment's start date

  const World& world() const { return world_; }
  const std::vector<Agent>& agents() const { return agents_; }
  const std::vector<News>& news() const { return news_; }
  const VilleOptions& options() const { return opts_; }
  const BehaviorSpec& behavior() const { return opts_.spec.behavior; }
  const EnvironmentSpec& environment() const { return opts_.spec.env; }
  const SocialRules& rules() const { return opts_.spec.behavior.rules; }
  // The rules one resident follows (their own, their group's, or the town's).
  const SocialRules& rulesFor(int agent) const {
    return opts_.spec.rulesFor(agents_[agent].p.name, agents_[agent].p.archetype);
  }
  // Applied between steps (the caller must not be inside step()).
  void setRules(const TownSpec& spec) {
    opts_.spec.behavior.rules = spec.behavior.rules;
    opts_.spec.behavior.groupRules = spec.behavior.groupRules;
    opts_.spec.behavior.residentRules = spec.behavior.residentRules;
  }
  bool llmCognition() const { return llm_; }

  // Mysteries
  const CaseState& mystery() const { return case_; }
  bool knowsCase(int agent) const;
  // Who `agent` suspects, most suspicious first (empty before a body is found).
  std::vector<std::pair<int, float>> suspects(int agent) const;
  // Why `agent` suspects (or clears) `suspect`: "I saw Ryan Park at Hobbs Cafe around 8:40 pm".
  std::vector<std::string> evidence(int agent, int suspect) const;
  // What `speaker` says about the case to `listener` ("" if it isn't their topic).
  std::string caseLine(int speaker, int listener) const;
  bool caseTopic(int a, int b, int share) const;
  std::string minuteText(int64_t minute) const;  // "8:40 pm", "yesterday at 8:40 pm"

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

  // mystery.cpp
  CaseState case_;
  void setupMystery();
  void updateMystery();  // start of every step: the hunt, the crime, discovery deadlines, the vote
  void recordSightings(int i);
  void kill(int victim);
  void discover(int finder, int victim);
  void applyMystery(int i);  // the meeting takes over that day's hours
  void holdVote();
  void shareEvidence(int from, int to);
  void startHunt();
};

// The clock for a step of a town: "Monday, February 13, 2023 -- 8:32 am", or
// "Mon Feb 13, 8:32 am" short.
std::string clockFor(const EnvironmentSpec& env, long long step, bool shortForm);

}  // namespace ville
