// Residents and events come from the environment file: the residents it
// declares, then (for bigger towns) generated ones built from its `generate`
// block -- names, traits, and routines weighted as the file says.
#include <algorithm>

#include "ville/cognition.h"

namespace ville {

namespace {

std::string firstName(const std::string& full) { return full.substr(0, full.find(' ')); }

std::string lifestyleFor(const Persona& p) {
  int s = p.sleepHour % 24;
  return p.first + " goes to bed around " + std::to_string(s % 12 == 0 ? 12 : s % 12) + (s < 12 ? " am" : " pm") +
         " and wakes up around " + std::to_string(p.wakeHour) + " am.";
}

// Rooms in a home with a bed in them, for residents whose bedroom isn't named.
std::vector<int> bedrooms(const World& w, int home) {
  std::vector<int> out;
  if (home < 0) return out;
  for (int a : w.sectors[home].arenas)
    for (int o : w.arenas[a].objects)
      if (w.objects[o].name == "bed") {
        out.push_back(a);
        break;
      }
  return out;
}

}  // namespace

std::vector<Persona> makePersonas(const World& world, const TownSpec& spec) {
  std::vector<Persona> out;
  for (auto& r : spec.env.residents) {
    Persona p;
    p.name = r.name;
    p.first = firstName(r.name);
    p.age = r.age;
    p.innate = r.innate;
    p.learned = r.learned;
    p.currently = r.currently;
    p.archetype = r.routine;
    p.home = world.findSector(r.home);
    p.work = r.work.empty() || r.work == "home" ? -1 : world.findSector(r.work);
    p.bedArena = r.bedroom;
    p.wakeHour = r.wakeHour;
    p.sleepHour = r.sleepHour;
    p.sociability = r.sociability;
    p.lifestyle = lifestyleFor(p);
    out.push_back(std::move(p));
  }

  // Generated residents.
  const GenerateSpec& g = spec.env.generate;
  if (g.residents <= 0) return out;
  std::vector<int> houses;
  std::map<std::string, std::vector<int>> byKind;
  for (size_t s = 0; s < world.sectors.size(); ++s) {
    const Sector& sec = world.sectors[s];
    byKind[sectorKindName(sec.kind)].push_back(static_cast<int>(s));
    if (sec.kind == SectorKind::Home && sec.name.rfind("House ", 0) == 0) houses.push_back(static_cast<int>(s));
  }
  float total = 0;
  for (auto& r : g.routines) total += r.weight;
  sl::SeededRandom rng(spec.env.seed ^ 0xA5A5u);
  int perHouse = g.onePer.count("home") ? std::max(1, g.onePer.at("home")) : 3;
  auto pick = [&](const std::vector<std::string>& v, const char* fallback) {
    return v.empty() ? std::string(fallback) : v[rng.index(v.size())];
  };
  for (int k = 0; k < g.residents; ++k) {
    Persona p;
    p.first = pick(g.firstNames, "Alex");
    p.name = p.first + " " + pick(g.lastNames, "Smith");
    const GenRoutine* routine = nullptr;
    if (total > 0) {
      float roll = static_cast<float>(rng.random()) * total;
      for (auto& r : g.routines) {
        if (roll < r.weight) {
          routine = &r;
          break;
        }
        roll -= r.weight;
      }
      if (!routine) routine = &g.routines.back();
    }
    p.archetype = routine ? routine->name : "writer";
    p.age = routine ? routine->minAge + static_cast<int>(rng.index(std::max(1, routine->maxAge - routine->minAge + 1))) : 30;
    p.innate = pick(g.traits, "friendly, curious");
    p.learned = p.name + " is " + (routine ? routine->learned : std::string("a resident")) + ".";
    p.currently = routine ? routine->currently : "settling into town";
    p.home = houses.empty() ? (byKind["home"].empty() ? 0 : byKind["home"][0]) : houses[(k / perHouse) % houses.size()];
    std::string worksAt = routine ? routine->worksAt : "home";
    auto jobs = byKind.find(worksAt);
    p.work = worksAt == "home" || jobs == byKind.end() || jobs->second.empty() ? -1 : jobs->second[rng.index(jobs->second.size())];
    // Students may live in a dorm, if the town has one.
    if (worksAt == "college" && byKind.count("dorm") && !byKind["dorm"].empty() && rng.random() < 0.5)
      p.home = byKind["dorm"][rng.index(byKind["dorm"].size())];
    auto beds = bedrooms(world, p.home);
    p.bedArena = beds.empty() ? "" : world.arenas[beds[k % beds.size()]].name;
    p.wakeHour = 6 + static_cast<int>(rng.index(3));
    p.sleepHour = 22 + static_cast<int>(rng.index(3));
    p.sociability = 0.3f + static_cast<float>(rng.random()) * 0.6f;
    p.lifestyle = lifestyleFor(p);
    out.push_back(std::move(p));
  }
  return out;
}

std::vector<News> seedNews(const World& world, const std::vector<Persona>& personas, const TownSpec& spec) {
  std::vector<News> out;
  for (auto& ev : spec.env.events) {
    News n;
    n.text = ev.text;
    for (size_t i = 0; i < personas.size(); ++i)
      if (personas[i].name == ev.host) n.origin = static_cast<int>(i);
    if (n.origin < 0) continue;  // nobody to start the news
    n.invite = ev.invite;
    n.sector = world.findSector(ev.at);
    n.day = ev.day;
    n.startMin = ev.startMin;
    n.endMin = ev.endMin;
    n.activity = ev.activity.empty() ? "going to " + (ev.name.empty() ? std::string("the event") : ev.name) : ev.activity;
    n.name = ev.name;
    out.push_back(n);
  }
  return out;
}

}  // namespace ville
