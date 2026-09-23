// Residents of the Ville. The first 25 are the paper's cast, paraphrased from
// the personas in its released simulation (joonspk-research/generative_agents,
// "base_the_ville_isabella_maria_klaus" and the 25-agent Ville); details are
// approximate. Larger towns add generated residents on the same archetypes.
#include <algorithm>

#include "ville/cognition.h"

namespace ville {

namespace {

struct Cast {
  const char* name;
  int age;
  const char* innate;
  const char* learned;
  const char* currently;  // gerund phrase: "working on ..."
  const char* archetype;
  const char* home;
  const char* work;  // "" = works from home
  const char* bedroom;
  int wake, sleep;
  float sociability;
};

const Cast kCast[] = {
    {"Isabella Rodriguez", 34, "friendly, outgoing, hospitable",
     "Isabella Rodriguez is the owner of Hobbs Cafe who loves to make people feel welcome. She is always looking for "
     "ways to make the cafe a place where people can come to relax and enjoy themselves.",
     "planning a Valentine's Day party at Hobbs Cafe with her customers on February 14th from 5pm to 7pm, and inviting "
     "everyone she meets",
     "cafe_owner", "Isabella Rodriguez's apartment", "Hobbs Cafe", "bedroom", 6, 23, 0.95f},
    {"Klaus Mueller", 20, "kind, inquisitive, passionate",
     "Klaus Mueller is a student at Oak Hill College studying sociology. He is passionate about social justice and "
     "loves to explore different perspectives.",
     "working on a research paper about the effects of gentrification in low-income communities", "student",
     "Dorm for Oak Hill College", "Oak Hill College", "dorm room 1", 7, 23, 0.55f},
    {"Maria Lopez", 21, "energetic, enthusiastic, inquisitive",
     "Maria Lopez is a student at Oak Hill College studying physics and a part-time Twitch game streamer who loves to "
     "connect with people. She has a crush on Klaus Mueller.",
     "studying for her physics classes and streaming games on Twitch", "student", "Dorm for Oak Hill College",
     "Oak Hill College", "dorm room 2", 7, 23, 0.75f},
    {"John Lin", 45, "patient, kind, organized",
     "John Lin is a pharmacy shopkeeper at the Willows Market and Pharmacy who loves to help people. He lives with his "
     "wife, Mei Lin, a college professor, and son, Eddy Lin, a student who studies music theory.",
     "wondering who will run in the local mayor election next month", "pharmacist", "Lin family's house",
     "The Willows Market and Pharmacy", "bedroom 1", 7, 22, 0.6f},
    {"Mei Lin", 44, "compassionate, thoughtful, curious",
     "Mei Lin is a professor of philosophy at Oak Hill College. She is married to John Lin and is the mother of Eddy "
     "Lin.",
     "preparing her next lecture on ethics", "professor", "Lin family's house", "Oak Hill College", "bedroom 1", 7, 22,
     0.55f},
    {"Eddy Lin", 19, "curious, analytical, musical",
     "Eddy Lin is a student at Oak Hill College studying music theory and composition. He loves exploring different "
     "musical styles.",
     "composing a piece of music for his composition class", "student", "Lin family's house", "Oak Hill College",
     "bedroom 2", 7, 23, 0.5f},
    {"Tom Moreno", 48, "opinionated, hardworking, loyal",
     "Tom Moreno runs the Harvey Oak Supply Store and cares a lot about the town's small businesses.",
     "thinking about the upcoming mayor election and what it means for local businesses", "shopkeeper",
     "Moreno family's house", "Harvey Oak Supply Store", "bedroom 1", 6, 22, 0.5f},
    {"Jane Moreno", 46, "warm, practical, community-minded",
     "Jane Moreno is Tom Moreno's wife and works part time at the Willows Market.",
     "planning her vegetable garden for the spring", "shopkeeper", "Moreno family's house",
     "The Willows Market and Pharmacy", "bedroom 1", 7, 22, 0.65f},
    {"Sam Moore", 65, "wise, determined, principled",
     "Sam Moore is a retired resident of the Ville who knows many of his neighbors and loves his community.",
     "running for mayor of the Ville in the upcoming local election and telling residents about his plans",
     "politician", "Moore family's house", "Town Hall", "bedroom 1", 6, 22, 0.9f},
    {"Jennifer Moore", 63, "artistic, gentle, patient",
     "Jennifer Moore is a watercolor painter who is married to Sam Moore.",
     "preparing a set of watercolor paintings for a small exhibition", "artist", "Moore family's house", "", "bedroom 1",
     7, 22, 0.45f},
    {"Yuriko Yamamoto", 40, "meticulous, direct, dependable",
     "Yuriko Yamamoto is a tax lawyer who helps local businesses keep their books in order.",
     "helping her clients prepare their taxes", "lawyer", "Yuriko Yamamoto's house", "Town Hall", "bedroom", 7, 23,
     0.4f},
    {"Carlos Gomez", 36, "expressive, sensitive, spontaneous",
     "Carlos Gomez is a poet who writes about everyday life in the Ville.", "writing a new collection of poems", "writer",
     "Carlos Gomez's apartment", "Hobbs Cafe", "bedroom", 8, 23, 0.6f},
    {"Ayesha Khan", 22, "thoughtful, analytical, bookish",
     "Ayesha Khan is a student at Oak Hill College writing her senior thesis on Shakespeare's plays.",
     "writing her thesis on the use of language in Shakespeare's plays", "student", "Dorm for Oak Hill College",
     "Oak Hill College", "dorm room 3", 7, 23, 0.45f},
    {"Wolfgang Schulz", 21, "hardworking, precise, quiet",
     "Wolfgang Schulz is a student at Oak Hill College studying chemistry.", "preparing for his chemistry exam",
     "student", "Dorm for Oak Hill College", "Oak Hill College", "dorm room 3", 7, 23, 0.35f},
    {"Abigail Chen", 25, "creative, open-minded, playful",
     "Abigail Chen is a digital artist and animator who lives at the artist's co-living space.",
     "working on an animation project for a client", "artist", "Artist's co-living space", "", "bedroom 1", 8, 23,
     0.6f},
    {"Francisco Lopez", 28, "witty, charismatic, energetic",
     "Francisco Lopez is an actor and comedian who performs at open mic nights.", "preparing a new stand-up set",
     "comedian", "Artist's co-living space", "The Rose and Crown Pub", "bedroom 2", 9, 24, 0.85f},
    {"Hailey Johnson", 27, "imaginative, introspective, warm", "Hailey Johnson is a writer working on her first novel.",
     "writing a chapter of her novel", "writer", "Artist's co-living space", "Hobbs Cafe", "bedroom 2", 8, 23, 0.55f},
    {"Rajiv Patel", 26, "creative, calm, observant", "Rajiv Patel is a painter who loves painting landscapes.",
     "painting a series of scenes of Johnson Park", "artist", "Artist's co-living space", "Johnson Park", "bedroom 3", 7,
     23, 0.5f},
    {"Latoya Williams", 29, "curious, adventurous, friendly",
     "Latoya Williams is a photographer documenting life in the Ville.",
     "working on a photo series about the town's residents", "photographer", "Artist's co-living space", "Johnson Park",
     "bedroom 3", 7, 23, 0.75f},
    {"Arthur Burton", 42, "friendly, easygoing, a good listener",
     "Arthur Burton is the bartender at The Rose and Crown Pub who knows everyone's order.",
     "thinking about hosting a trivia night at the pub", "bartender", "Arthur Burton's apartment",
     "The Rose and Crown Pub", "bedroom", 9, 24, 0.8f},
    {"Ryan Park", 29, "logical, ambitious, friendly",
     "Ryan Park is a software engineer at a small startup who works from home.",
     "building a new feature for his startup's app", "engineer", "Ryan Park's apartment", "", "bedroom", 7, 24, 0.5f},
    {"Giorgio Rossi", 38, "analytical, curious, sociable",
     "Giorgio Rossi is a mathematician who studies patterns in nature.",
     "researching a paper on fractals, often at the college library", "mathematician", "Giorgio Rossi's apartment",
     "Oak Hill College", "bedroom", 7, 23, 0.6f},
    {"Carmen Ortiz", 35, "cheerful, efficient, friendly", "Carmen Ortiz is a shopkeeper at Harvey Oak Supply Store.",
     "organizing the store's new inventory", "shopkeeper", "Tamara Taylor and Carmen Ortiz's house",
     "Harvey Oak Supply Store", "bedroom 2", 7, 22, 0.7f},
    {"Adam Smith", 60, "philosophical, thoughtful, calm",
     "Adam Smith is a philosopher and writer who enjoys long walks around town.",
     "working on a book about ethics in daily life", "writer", "Adam Smith's house", "", "bedroom", 7, 22, 0.45f},
    {"Tamara Taylor", 40, "kind, imaginative, patient", "Tamara Taylor is an author of children's books.",
     "writing a new children's book", "writer", "Tamara Taylor and Carmen Ortiz's house", "", "bedroom 1", 7, 22, 0.55f},
};

const char* kFirst[] = {"Ava", "Liam", "Noah", "Emma", "Olivia", "Elijah", "Mia", "Lucas", "Amelia", "Mateo", "Harper",
                        "Leo", "Evelyn", "Ezra", "Aria", "Luca", "Nora", "Kai", "Zoe", "Omar", "Priya", "Hiro", "Lena",
                        "Diego", "Sofia", "Ines", "Tariq", "Maya", "Jonah", "Ruth", "Felix", "Ada", "Samir", "Chloe",
                        "Theo", "Yara", "Ivan", "Mina", "Owen", "Rosa", "Jae", "Nadia", "Ben", "Lucia", "Kofi", "Iris",
                        "Pablo", "Anika", "Hugo", "Leila"};
const char* kLast[] = {"Garcia", "Nguyen", "Smith", "Kim", "Patel", "Okafor", "Silva", "Novak", "Haddad", "Ito",
                       "Brown", "Rossi", "Jensen", "Cohen", "Mendes", "Ali", "Kowalski", "Tanaka", "Dubois", "Reyes",
                       "Singh", "Larsen", "Moreau", "Ahmed", "Walker", "Ferreira", "Yilmaz", "Chen", "Murphy", "Diaz"};

struct Archetype {
  const char* key;
  const char* learned;  // "{name} is ..."
  const char* currently;
  SectorKind work;  // Home = works from home
  float weight;
};

const Archetype kArchetypes[] = {
    {"student", "a student at the local college", "studying for upcoming exams", SectorKind::College, 3},
    {"cafe_owner", "a barista at a neighborhood cafe", "learning to make new coffee drinks", SectorKind::Cafe, 1},
    {"shopkeeper", "a shopkeeper at a supply store", "reorganizing the store's shelves", SectorKind::Store, 1.5f},
    {"pharmacist", "a clerk at the market and pharmacy", "helping customers find what they need", SectorKind::Market, 1.5f},
    {"bartender", "a bartender at a local pub", "planning a new cocktail menu", SectorKind::Pub, 1},
    {"engineer", "an engineer at a local office", "finishing a project for work", SectorKind::Office, 3},
    {"lawyer", "a lawyer at a local office", "preparing documents for a client", SectorKind::Office, 1},
    {"artist", "a painter who works from home", "painting a new series", SectorKind::Home, 1},
    {"writer", "a writer who works from home", "writing a new story", SectorKind::Home, 1},
    {"retiree", "a retiree who enjoys the town's parks", "tending a small garden", SectorKind::Home, 2},
};

std::string firstName(const std::string& full) { return full.substr(0, full.find(' ')); }

}  // namespace

std::vector<Persona> makePersonas(const World& world, int population, uint32_t seed) {
  std::vector<Persona> out;
  int named = std::min<int>(population, static_cast<int>(sizeof kCast / sizeof kCast[0]));
  for (int i = 0; i < named; ++i) {
    const Cast& c = kCast[i];
    Persona p;
    p.name = c.name;
    p.first = firstName(c.name);
    p.age = c.age;
    p.innate = c.innate;
    p.learned = c.learned;
    p.currently = c.currently;
    p.lifestyle = std::string(p.first) + " goes to bed around " + std::to_string(c.sleep > 12 ? c.sleep - 12 : c.sleep) +
                  (c.sleep >= 24 ? " am" : " pm") + " and wakes up around " + std::to_string(c.wake) + " am.";
    p.archetype = c.archetype;
    p.home = world.findSector(c.home);
    p.work = *c.work ? world.findSector(c.work) : -1;
    p.bedArena = c.bedroom;
    p.wakeHour = c.wake;
    p.sleepHour = c.sleep;
    p.sociability = c.sociability;
    out.push_back(std::move(p));
  }

  // Generated residents: three per house, jobs filled by the town's businesses.
  std::vector<int> houses;
  std::vector<std::vector<int>> byKind(10);
  for (size_t s = 0; s < world.sectors.size(); ++s) {
    const Sector& sec = world.sectors[s];
    if (sec.kind == SectorKind::Home && sec.name.rfind("House ", 0) == 0) houses.push_back(static_cast<int>(s));
    byKind[static_cast<int>(sec.kind)].push_back(static_cast<int>(s));
  }
  float total = 0;
  for (auto& a : kArchetypes) total += a.weight;
  sl::SeededRandom rng(seed ^ 0xA5A5u);
  for (int i = named; i < population; ++i) {
    int g = i - named;
    Persona p;
    p.first = kFirst[rng.index(sizeof kFirst / sizeof kFirst[0])];
    p.name = p.first + " " + kLast[rng.index(sizeof kLast / sizeof kLast[0])];
    float pick = static_cast<float>(rng.random()) * total;
    const Archetype* arch = &kArchetypes[0];
    for (auto& a : kArchetypes) {
      if (pick < a.weight) {
        arch = &a;
        break;
      }
      pick -= a.weight;
    }
    p.age = arch->work == SectorKind::College ? 18 + static_cast<int>(rng.index(6)) : 24 + static_cast<int>(rng.index(50));
    static const char* kTraits[] = {"curious, friendly, practical", "quiet, thoughtful, kind", "outgoing, witty, warm",
                                    "organized, patient, loyal", "creative, restless, cheerful",
                                    "calm, observant, generous"};
    p.innate = kTraits[rng.index(6)];
    p.learned = p.name + " is " + arch->learned + " in the Ville.";
    p.currently = arch->currently;
    p.archetype = arch->key;
    p.home = houses.empty() ? world.findSector("Adam Smith's house") : houses[(g / 3) % houses.size()];
    p.bedArena = g % 3 == 0 ? "bedroom 1" : "bedroom 2";
    const auto& jobs = byKind[static_cast<int>(arch->work)];
    p.work = arch->work == SectorKind::Home || jobs.empty() ? -1 : jobs[rng.index(jobs.size())];
    if (arch->work == SectorKind::College && !byKind[static_cast<int>(SectorKind::Dorm)].empty() && rng.random() < 0.5) {
      const auto& dorms = byKind[static_cast<int>(SectorKind::Dorm)];
      p.home = dorms[rng.index(dorms.size())];
      p.bedArena = "dorm room " + std::to_string(1 + rng.index(3));
    }
    p.wakeHour = 6 + static_cast<int>(rng.index(3));
    p.sleepHour = 22 + static_cast<int>(rng.index(3));
    p.lifestyle = p.first + " wakes up around " + std::to_string(p.wakeHour) + " am.";
    p.sociability = 0.3f + static_cast<float>(rng.random()) * 0.6f;
    out.push_back(std::move(p));
  }
  return out;
}

std::vector<News> seedNews(const World& world, const std::vector<Persona>& personas) {
  std::vector<News> out;
  int isabella = -1, sam = -1;
  for (size_t i = 0; i < personas.size(); ++i) {
    if (personas[i].name == "Isabella Rodriguez") isabella = static_cast<int>(i);
    if (personas[i].name == "Sam Moore") sam = static_cast<int>(i);
  }
  if (isabella >= 0) {
    News n;
    n.text = "Isabella Rodriguez is hosting a Valentine's Day party at Hobbs Cafe on February 14th from 5pm to 7pm.";
    n.origin = isabella;
    n.invite = true;
    n.sector = world.findSector("Hobbs Cafe");
    n.day = 1;
    n.startMin = 17 * 60;
    n.endMin = 19 * 60;
    n.activity = "going to Isabella's Valentine's Day party at Hobbs Cafe";
    out.push_back(n);
  }
  if (sam >= 0) {
    News n;
    n.text = "Sam Moore is running for mayor of the Ville in the upcoming local election.";
    n.origin = sam;
    out.push_back(n);
  }
  return out;
}

}  // namespace ville
