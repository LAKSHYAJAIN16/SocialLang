// Cognition backends for towns. The simulation decides *when* an agent
// plans, talks, or reflects; a Cognition decides *what*, reading routines,
// activities, emoji, and importance from the town's behavior file.
//
//   PersonaCognition -- offline and fast: schedules, task decompositions,
//     conversations, and reflections built from each persona's archetype,
//     lifestyle, current activity, and retrieved memories. No model calls, so
//     it runs thousands of agents in real time. It is a stand-in for the LLM,
//     not the LLM: behavior follows templates, not open-ended generation.
//   LlmCognition -- prompts to the agent's own model
//     (daily plan + hourly schedule, task decomposition, dialogue,
//     reflection), falling back to the persona model if a reply can't be
//     parsed or the call fails.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ville/sim.h"

namespace ville {

class Cognition {
 public:
  virtual ~Cognition() = default;
  virtual void planDay(Ville& v, int agent, sl::SeededRandom& rng, std::vector<std::string>& plan,
                       std::vector<HourSlot>& schedule) = 0;
  virtual std::vector<Task> decompose(Ville& v, int agent, const HourSlot& slot, sl::SeededRandom& rng) = 0;
  virtual bool wantsToChat(Ville& v, int a, int b, sl::SeededRandom& rng) = 0;
  // Lines as (speaker, text). `news` is what the opener chose to share (-1: none).
  virtual std::vector<std::pair<int, std::string>> converse(Ville& v, int a, int b, int news, sl::SeededRandom& rng) = 0;
  virtual std::vector<std::string> reflect(Ville& v, int agent, sl::SeededRandom& rng) = 0;
  virtual float importance(const std::string& desc) const;
  virtual std::string emoji(const std::string& desc) const;
  virtual void setBehavior(const BehaviorSpec* b) { behavior_ = b; }

 protected:
  const BehaviorSpec* behavior_ = nullptr;
};

std::unique_ptr<Cognition> makePersonaCognition();
std::unique_ptr<Cognition> makeLlmCognition(int maxConcurrency);

// Residents from the environment file (declared, then generated).
std::vector<Persona> makePersonas(const World& world, const TownSpec& spec);

// The events and news the environment file seeds.
std::vector<News> seedNews(const World& world, const std::vector<Persona>& personas, const TownSpec& spec);

}  // namespace ville
