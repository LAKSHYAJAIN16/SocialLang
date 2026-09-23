#include "ville/cognition.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace ville {

namespace {

bool has(const std::string& s, const char* kw) { return s.find(kw) != std::string::npos; }

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n-*0123456789.)"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  std::stringstream ss(text);
  std::string l;
  while (std::getline(ss, l))
    if (!trim(l).empty()) out.push_back(l);
  return out;
}

std::string hourText(int h) {
  h %= 24;
  int hh = h % 12 == 0 ? 12 : h % 12;
  return std::to_string(hh) + (h < 12 ? ":00 am" : ":00 pm");
}

// ---- Schedules: the archetype's working day, then meals, free time, sleep.

struct Block {
  int from, to;
  std::string activity, place;
};

std::vector<Block> workday(const Persona& p, sl::SeededRandom& rng) {
  const std::string& a = p.archetype;
  int w = p.wakeHour;
  std::vector<Block> b = {{w, w + 1, "waking up and completing the morning routine", "home"}};
  auto lunch = [&](int h) {
    b.push_back({h, h + 1, rng.random() < 0.6 ? "having lunch at Hobbs Cafe" : "having lunch at home",
                 rng.random() < 0.6 ? "cafe" : "home"});
    if (b.back().place == "home") b.back().activity = "having lunch at home";
    else b.back().activity = "having lunch at Hobbs Cafe";
  };
  if (a == "cafe_owner") {
    b.push_back({w + 1, 8, "opening the cafe for the day", "work"});
    b.push_back({8, 12, "working at the counter of the cafe", "work"});
    b.push_back({12, 13, "having lunch at the cafe", "work"});
    b.push_back({13, 17, "working at the counter of the cafe", "work"});
    b.push_back({17, 20, "serving customers at the cafe", "work"});
    b.push_back({20, 21, "closing up the cafe", "work"});
  } else if (a == "student") {
    b.push_back({9, 12, "attending class at Oak Hill College", "classroom"});
    lunch(12);
    b.push_back({13, 16, "studying at the college library", "library"});
    b.push_back({16, 17, p.currently, "library"});
    b.push_back({17, 18, "taking a walk in the park", "park"});
  } else if (a == "professor") {
    b.push_back({w + 1, 9, "preparing the day's lecture", "office"});
    b.push_back({9, 12, "teaching a class at Oak Hill College", "classroom"});
    lunch(12);
    b.push_back({13, 16, "holding office hours", "office"});
    b.push_back({16, 17, "grading papers", "office"});
  } else if (a == "pharmacist" || a == "shopkeeper") {
    b.push_back({w + 1, 12, "working at the store counter", "work"});
    lunch(12);
    b.push_back({13, 17, "working at the store counter", "work"});
  } else if (a == "bartender") {
    b.push_back({w + 1, 12, "running errands at the market", "market"});
    lunch(12);
    b.push_back({13, 15, p.currently, "home"});
    b.push_back({15, 16, "preparing the pub for the evening", "work"});
    b.push_back({16, 24, "tending the bar at the pub", "work"});
  } else if (a == "politician") {
    b.push_back({w + 1, 8, "reading the newspaper over breakfast", "home"});
    b.push_back({8, 10, "talking to residents in the park about the mayoral campaign", "park"});
    b.push_back({10, 12, "working on the mayoral campaign at Town Hall", "work"});
    b.push_back({12, 13, "having lunch at Hobbs Cafe", "cafe"});
    b.push_back({13, 15, "campaigning around the market", "market"});
    b.push_back({15, 16, "campaigning at Hobbs Cafe", "cafe"});
    b.push_back({16, 17, "taking a walk in the park", "park"});
  } else if (a == "comedian") {
    b.push_back({w + 1, 12, p.currently, "home"});
    lunch(12);
    b.push_back({13, 17, "writing new jokes at Hobbs Cafe", "cafe"});
    b.push_back({19, 22, "performing at the open mic at the pub", "work"});
  } else if (a == "photographer" || (a == "artist" && p.work >= 0)) {
    b.push_back({w + 1, 12, a == "photographer" ? "taking photographs around the park" : "painting in the park", "work"});
    lunch(12);
    b.push_back({13, 16, a == "photographer" ? "editing photos at Hobbs Cafe" : p.currently, a == "photographer" ? "cafe" : "work"});
  } else if (a == "mathematician") {
    b.push_back({w + 1, 12, "researching at the college library", "library"});
    lunch(12);
    b.push_back({13, 16, p.currently, "library"});
  } else if (a == "retiree") {
    b.push_back({w + 1, 10, "tending the garden at home", "home"});
    b.push_back({10, 12, "taking a walk in the park", "park"});
    lunch(12);
    b.push_back({13, 15, "shopping for groceries at the market", "market"});
  } else {  // writer, artist at home, engineer, lawyer
    std::string place = p.work >= 0 ? "work" : "home";
    std::string job = a == "engineer" ? "coding at the desk"
                      : a == "lawyer" ? "working with clients"
                      : a == "artist" ? "painting"
                                      : "writing";
    b.push_back({w + 1, 12, job, place});
    lunch(12);
    b.push_back({13, 17, p.currently, place});
  }
  return b;
}

std::string freeTime(const Persona& p, sl::SeededRandom& rng, std::string& place) {
  double r = rng.random() * (0.6 + p.sociability);
  if (r < 0.35) {
    place = "home";
    return "relaxing at home";
  }
  if (r < 0.6) {
    place = "home";
    return "reading at home";
  }
  if (r < 0.85) {
    place = "park";
    return "taking a walk in the park";
  }
  if (r < 1.1) {
    place = "pub";
    return "having a drink at the pub";
  }
  place = "cafe";
  return "hanging out at Hobbs Cafe";
}

// ---- Task decomposition: 5-15 minute steps, each tied to an object.

struct T {
  const char* desc;
  const char* obj;
  int min;
};

std::vector<T> templatesFor(const std::string& activity, const std::string& place) {
  const std::string& s = activity;
  if (has(s, "sleep")) return {{"sleeping", "bed", 60}};
  if (has(s, "morning routine"))
    return {{"waking up and stretching", "bed", 5},     {"using the bathroom", "toilet", 5},
            {"taking a shower", "shower", 10},           {"getting dressed", "closet", 5},
            {"making breakfast", "stove", 15},           {"eating breakfast", "table", 20}};
  if (has(s, "party") && has(s, "hosting"))
    return {{"welcoming guests to the party", "cafe customer seating", 20},
            {"serving drinks at the party", "behind the cafe counter", 20},
            {"chatting with guests at the party", "cafe customer seating", 20}};
  if (has(s, "party"))
    return {{"arriving at the Valentine's Day party", "cafe customer seating", 10},
            {"chatting with people at the party", "cafe customer seating", 30},
            {"enjoying snacks at the party", "cafe customer seating", 20}};
  if (has(s, "opening the cafe") || has(s, "closing up"))
    return {{has(s, "opening") ? "unlocking the cafe and turning on the lights" : "wiping down the tables", "cafe customer seating", 15},
            {has(s, "opening") ? "brewing the first pot of coffee" : "cleaning the coffee machine", "coffee machine", 25},
            {has(s, "opening") ? "setting up the counter" : "counting the register", "behind the cafe counter", 20}};
  if (has(s, "counter of the cafe") || has(s, "serving customers") || has(s, "party preparations"))
    return {{"greeting customers", "behind the cafe counter", 10}, {"brewing coffee for customers", "coffee machine", 15},
            {"taking orders", "behind the cafe counter", 15},    {"baking pastries", "cooking area", 10},
            {"cleaning the counter", "behind the cafe counter", 10}};
  if (has(s, "store counter"))
    return {{"helping customers", "behind the", 15}, {"restocking shelves", "shelf", 20},
            {"ringing up purchases", "behind the", 15}, {"checking inventory", "shelf", 10}};
  if (has(s, "tending the bar") || has(s, "preparing the pub"))
    return {{"pouring drinks", "beer taps", 20}, {"chatting with customers", "behind the bar counter", 20},
            {"cleaning glasses", "behind the bar counter", 20}};
  if (has(s, "open mic"))
    return {{"warming up backstage", "bar customer seating", 20}, {"performing stand-up comedy", "karaoke machine", 25},
            {"chatting with the audience", "bar customer seating", 15}};
  if (has(s, "attending class"))
    return {{"listening to the lecture", "classroom student seating", 25}, {"taking notes", "classroom student seating", 20},
            {"discussing with classmates", "classroom student seating", 15}};
  if (has(s, "teaching"))
    return {{"giving a lecture", "classroom podium", 30}, {"writing on the blackboard", "blackboard", 15},
            {"answering students' questions", "classroom podium", 15}};
  if (has(s, "office hours") || has(s, "grading") || has(s, "preparing the day"))
    return {{has(s, "grading") ? "grading papers" : has(s, "office hours") ? "meeting with a student" : "preparing lecture notes", "desk", 30},
            {"reading a book", "bookshelf", 15},
            {"answering emails", "desk", 15}};
  if (has(s, "library") || has(s, "studying") || has(s, "research"))
    return {{"reading at the library", "library table", 25}, {"looking for books", "bookshelf", 10},
            {"writing notes", "library table", 25}};
  if (has(s, "lunch at Hobbs") || has(s, "lunch at the cafe") || has(s, "hanging out at Hobbs"))
    return {{"ordering food at the counter", "cafe customer seating", 10}, {"eating lunch", "cafe customer seating", 30},
            {"having a coffee", "cafe customer seating", 20}};
  if (has(s, "lunch") || has(s, "dinner") || has(s, "eating"))
    return {{"cooking a meal", "stove", 20}, {"eating", "table", 30}, {"washing the dishes", "sink", 10}};
  if (has(s, "park") || has(s, "walk"))
    return {{"walking around the park", "park garden", 20}, {"sitting on a bench", "park bench", 25},
            {"enjoying the view", "picnic table", 15}};
  if (has(s, "pub") || has(s, "drink"))
    return {{"ordering a drink", "bar customer seating", 10}, {"having a drink", "bar customer seating", 30},
            {"playing pool", "pool table", 20}};
  if (has(s, "campaign"))
    return {{"talking with residents about the campaign", place == "work" ? "notice board" : "bench", 30},
            {"handing out campaign flyers", place == "work" ? "desk" : "bench", 20},
            {"listening to residents' concerns", place == "work" ? "desk" : "bench", 10}};
  if (has(s, "market") || has(s, "shopping") || has(s, "errands"))
    return {{"browsing the shelves", "grocery shelf", 20}, {"picking up groceries", "grocery shelf", 20},
            {"paying at the counter", "behind the grocery counter", 20}};
  if (has(s, "photograph") || has(s, "photos"))
    return {{"taking photographs", "park garden", 25}, {"setting up the camera", "park bench", 10},
            {"reviewing shots", place == "cafe" ? "cafe customer seating" : "picnic table", 25}};
  if (has(s, "painting") || has(s, "watercolor") || has(s, "animation"))
    return {{"sketching ideas", place == "work" ? "park bench" : "desk", 20},
            {"painting", place == "work" ? "park bench" : "desk", 30},
            {"cleaning the brushes", place == "work" ? "picnic table" : "sink", 10}};
  if (has(s, "coding") || has(s, "app"))
    return {{"writing code", "desk", 30}, {"on a video call with the team", "desk", 15}, {"making coffee", "stove", 5},
            {"testing the app", "desk", 10}};
  if (has(s, "garden"))
    return {{"watering the plants", "couch", 20}, {"pulling weeds", "couch", 25}, {"resting on the couch", "couch", 15}};
  if (has(s, "newspaper"))
    return {{"reading the newspaper", "dining table", 30}, {"drinking coffee", "dining table", 30}};
  if (has(s, "relaxing") || has(s, "reading at home"))
    return {{has(s, "reading") ? "reading a book" : "watching TV", has(s, "reading") ? "couch" : "tv", 30},
            {"relaxing on the couch", "couch", 30}};
  if (has(s, "writing") || has(s, "jokes") || has(s, "poems") || has(s, "novel") || has(s, "book") || has(s, "thesis"))
    return {{"writing", place == "cafe" ? "cafe customer seating" : "desk", 35},
            {"rereading the draft", place == "cafe" ? "cafe customer seating" : "desk", 15},
            {"taking a short break", place == "cafe" ? "cafe customer seating" : "couch", 10}};
  if (has(s, "clients") || has(s, "taxes"))
    return {{"reviewing client documents", "desk", 30}, {"meeting with a client", "meeting table", 30}};
  return {{s.c_str(), "", 60}};
}

// Keeps the strings alive for templatesFor's fallback case.
std::vector<Task> toTasks(const std::vector<T>& ts, const std::string& place, int minutes, const Cognition& cog,
                          const std::string& fallbackDesc) {
  std::vector<Task> out;
  int sum = 0;
  for (auto& t : ts) sum += t.min;
  for (auto& t : ts) {
    Task k;
    k.desc = t.desc[0] ? t.desc : fallbackDesc;
    k.place = place;
    k.objectKw = t.obj;
    k.minutes = std::max(5, t.min * minutes / std::max(sum, 1));
    k.emoji = cog.emoji(k.desc);
    out.push_back(std::move(k));
  }
  return out;
}

// Short "I'm ..." description of an action.
std::string doing(const Agent& a) { return a.action.empty() ? "out and about" : a.action; }

class PersonaCognition : public Cognition {
 public:
  void planDay(Ville& v, int i, sl::SeededRandom& rng, std::vector<std::string>& plan,
               std::vector<HourSlot>& schedule) override {
    const Agent& ag = v.agents()[i];
    const Persona& p = ag.p;
    std::vector<Block> blocks = workday(p, rng);
    // Meals, free time, then sleep fill the rest of the day.
    auto covered = [&](int h) {
      for (auto& b : blocks)
        if (h >= b.from && h < b.to) return true;
      return false;
    };
    int endWork = 0;
    for (auto& b : blocks) endWork = std::max(endWork, b.to);
    int sleepH = p.sleepHour;
    for (int h = p.wakeHour; h < std::min(sleepH, 24); ++h) {
      if (covered(h)) continue;
      if (h == 18) {
        blocks.push_back({h, h + 1, "eating dinner", "home"});
        continue;
      }
      std::string place;
      std::string act = freeTime(p, rng, place);
      blocks.push_back({h, h + 1, act, place});
    }
    std::sort(blocks.begin(), blocks.end(), [](const Block& x, const Block& y) { return x.from < y.from; });
    // Hourly schedule, midnight to midnight.
    schedule.clear();
    for (int h = 0; h < 24; ++h) {
      HourSlot s;
      s.activity = "sleeping";
      s.place = "home";
      if (h >= p.wakeHour && h < sleepH)
        for (auto& b : blocks)
          if (h >= b.from && h < b.to) {
            s.activity = b.activity;
            s.place = b.place;
          }
      schedule.push_back(s);
    }
    // Broad-strokes plan, in the paper's format.
    plan.clear();
    plan.push_back("wake up and complete the morning routine at " + hourText(p.wakeHour));
    std::string last;
    for (int h = p.wakeHour + 1; h < std::min(sleepH, 24); ++h)
      if (schedule[h].activity != last) {
        last = schedule[h].activity;
        plan.push_back(last + " at " + hourText(h));
      }
    plan.push_back("go to bed at " + hourText(sleepH % 24));
  }

  std::vector<Task> decompose(Ville& v, int i, const HourSlot& slot, sl::SeededRandom& rng) override {
    (void)v;
    (void)i;
    (void)rng;
    return toTasks(templatesFor(slot.activity, slot.place), slot.place, slot.minutes, *this, slot.activity);
  }

  bool wantsToChat(Ville& v, int a, int b, sl::SeededRandom& rng) override {
    const Agent& A = v.agents()[a];
    const Agent& B = v.agents()[b];
    if (A.asleep || B.asleep || A.chatWith >= 0 || B.chatWith >= 0) return false;
    if (has(A.action, "sleep") || has(B.action, "sleep")) return false;
    auto it = A.lastChat.find(b);
    if (it != A.lastChat.end() && v.stepCount() - it->second < 3 * kStepsPerHour) return false;
    float fam = 0;
    if (auto f = A.familiarity.find(b); f != A.familiarity.end()) fam = f->second;
    // Someone with news the other hasn't heard is much keener to talk.
    bool hasNews = false;
    for (int n : A.knows)
      if (std::find(B.knows.begin(), B.knows.end(), n) == B.knows.end()) hasNews = true;
    double p = 0.012 * (0.4 + A.p.sociability) * (1 + fam) * (hasNews ? 4.0 : 1.0);
    return rng.random() < p;
  }

  std::vector<std::pair<int, std::string>> converse(Ville& v, int a, int b, int news, sl::SeededRandom& rng) override {
    const Agent& A = v.agents()[a];
    const Agent& B = v.agents()[b];
    std::vector<std::pair<int, std::string>> out;
    int h = v.minuteOfDay() / 60;
    const char* hello = h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening";
    bool met = A.familiarity.count(b) > 0;
    out.push_back({a, met ? std::string(hello) + ", " + B.p.first + "! How's your day going?"
                          : "Hi, I don't think we've met -- I'm " + A.p.first + ". " + hello + "!"});
    out.push_back({b, met ? "Hey " + A.p.first + "! Pretty good -- I've been " + doing(B) + "."
                          : "Nice to meet you, " + A.p.first + ". I'm " + B.p.first + ". I've been " + doing(B) + "."});
    if (news >= 0) {
      const News& n = v.news()[news];
      if (n.origin == a && n.invite) {
        out.push_back({a, "I'm hosting a Valentine's Day party at Hobbs Cafe tomorrow from 5 to 7 pm. I'd love for you to come!"});
      } else {
        std::string t = n.text;
        out.push_back({a, "Did you hear? " + t});
      }
      if (n.invite) {
        double yes = 0.45 + 0.4 * B.p.sociability;
        out.push_back({b, rng.random() < yes ? "Oh, that sounds lovely -- I'll be there!"
                                             : "Thanks for telling me! I'm not sure I can make it, but I'll try."});
      } else {
        out.push_back({b, rng.random() < 0.5 ? "Really? I hadn't heard that. I'll keep an eye on it."
                                              : "Huh, interesting. I'd like to hear more about that."});
      }
    } else {
      // Talk about what's on their minds, grounded in memory: what A has been
      // doing, or something A remembers about B.
      auto mem = v.retrieve(a, B.p.name, 3);
      std::string memLine;
      for (int m : mem)
        if (A.memory[m].person == b && A.memory[m].type == NodeType::Event) {
          memLine = "I saw you " + A.memory[m].predicate + " earlier.";
          break;
        }
      out.push_back({a, !memLine.empty() && rng.random() < 0.5 ? memLine + " How did that go?"
                                                             : "I've been " + A.p.currently + "."});
      out.push_back({b, "That sounds interesting! As for me, I've been " + B.p.currently + "."});
    }
    out.push_back({a, "Well, I should get back to " + doing(A) + ". See you around, " + B.p.first + "!"});
    out.push_back({b, "Bye, " + A.p.first + "!"});
    return out;
  }

  std::vector<std::string> reflect(Ville& v, int i, sl::SeededRandom& rng) override {
    const Agent& ag = v.agents()[i];
    std::map<int, int> people;
    std::map<int, int> newsSeen;
    int from = std::max(0, static_cast<int>(ag.memory.size()) - 100);
    for (size_t m = from; m < ag.memory.size(); ++m) {
      const MemoryNode& n = ag.memory[m];
      if (n.person >= 0 && n.type == NodeType::Chat) people[n.person] += 2;
      else if (n.person >= 0) people[n.person] += 1;
      if (n.news >= 0) newsSeen[n.news]++;
    }
    std::vector<std::string> out;
    int best = -1, bestN = 0;
    for (auto& [p, c] : people)
      if (c > bestN) {
        best = p;
        bestN = c;
      }
    if (best >= 0 && bestN >= 2) {
      const Persona& o = v.agents()[best].p;
      out.push_back(rng.random() < 0.5 ? o.first + " and I have been spending time together; I feel like I'm getting to know them."
                                       : "I enjoy talking with " + o.first + "; they are " + o.innate.substr(0, o.innate.find(',')) + ".");
    }
    for (auto& [n, c] : newsSeen) {
      const News& nw = v.news()[n];
      bool going = std::find(ag.attending.begin(), ag.attending.end(), n) != ag.attending.end();
      if (nw.invite)
        out.push_back(going ? "I'm looking forward to " + v.agents()[nw.origin].p.first + "'s Valentine's Day party at Hobbs Cafe."
                            : "People are talking about " + v.agents()[nw.origin].p.first + "'s party at Hobbs Cafe.");
      else
        out.push_back("Many people in town are talking about the fact that " + nw.text.substr(0, nw.text.size() - 1) + ".");
      if (out.size() >= 3) break;
    }
    if (out.size() < 3) out.push_back("I have been " + ag.p.currently + " and I feel I'm making progress.");
    return out;
  }
};

// ---- LLM cognition: the paper's prompts through each agent's provider,
// with the persona model as the fallback for anything unparsable.

std::string identity(const Agent& a) {
  const Persona& p = a.p;
  return "Name: " + p.name + " (age: " + std::to_string(p.age) + ")\nInnate traits: " + p.innate +
         "\nLearned traits: " + p.learned + "\nCurrently: " + p.name + " is " + p.currently + ".\nLifestyle: " +
         p.lifestyle;
}

class LlmCognition : public Cognition {
 public:
  explicit LlmCognition(int) {}

  sl::CompletionResult ask(Ville& v, int i, const std::string& user, int maxTokens, double temperature = 0.8) {
    sl::Provider* prov = v.agents()[i].provider;
    sl::CompletionRequest req;
    req.system = identity(v.agents()[i]);
    req.user = user;
    req.maxTokens = maxTokens;
    req.temperature = temperature;
    auto r = prov->complete(req);
    ++v.calls;
    if (!r.error.empty()) {
      ++v.errors;
      v.emit(5, i, r.error);
    }
    return r;
  }
  static bool live(Ville& v, int i) {
    sl::Provider* p = v.agents()[i].provider;
    return p && p->wantsContext();  // the mock opts out of context: use the persona model
  }
  static std::string memories(Ville& v, int i, const std::string& focal, int k) {
    std::string out;
    for (int m : v.retrieve(i, focal, k)) out += "- " + v.agents()[i].memory[m].description + "\n";
    return out.empty() ? "- (nothing notable yet)\n" : out;
  }

  void planDay(Ville& v, int i, sl::SeededRandom& rng, std::vector<std::string>& plan,
               std::vector<HourSlot>& schedule) override {
    fallback_.planDay(v, i, rng, plan, schedule);  // baseline (and the sleep hours)
    if (!live(v, i)) return;
    const Agent& a = v.agents()[i];
    std::string attending;
    for (int n : a.attending)
      if (v.news()[n].day == v.dayIndex()) attending += "- " + v.news()[n].text + "\n";
    std::string prompt =
        "Today is " + v.clockText().substr(0, v.clockText().find(" --")) + ". Relevant memories:\n" +
        memories(v, i, "plan for today", 8) + (attending.empty() ? "" : "Plans already made:\n" + attending) +
        "\nWrite " + a.p.first + "'s hourly schedule for today from " + hourText(a.p.wakeHour) + " to " +
        hourText(a.p.sleepHour % 24) +
        ". One line per hour, formatted exactly as 'HH | activity | place', where HH is the 24-hour clock hour and place is "
        "one of: home, work, cafe, park, pub, classroom, library, office, market, store, townhall.";
    auto r = ask(v, i, prompt, 700, 0.7);
    std::vector<HourSlot> parsed = schedule;
    int ok = 0;
    for (auto& l : lines(r.text)) {
      std::vector<std::string> parts;
      std::stringstream ss(l);
      std::string part;
      while (std::getline(ss, part, '|')) parts.push_back(trim(part));
      if (parts.size() < 3) continue;
      int hh = std::atoi(l.c_str() + l.find_first_of("0123456789"));
      if (hh < 0 || hh > 23 || parts[1].empty()) continue;
      parsed[hh].activity = lower(parts[1]);
      parsed[hh].place = lower(parts[2]);
      ++ok;
    }
    if (ok >= 4) {
      schedule = parsed;
      plan.clear();
      std::string last;
      for (int h = 0; h < 24; ++h)
        if (schedule[h].activity != last) {
          last = schedule[h].activity;
          plan.push_back(last + " at " + hourText(h));
        }
    }
  }

  std::vector<Task> decompose(Ville& v, int i, const HourSlot& slot, sl::SeededRandom& rng) override {
    auto base = fallback_.decompose(v, i, slot, rng);
    if (!live(v, i) || slot.activity == "sleeping") return base;
    const Agent& a = v.agents()[i];
    int sector = v.resolveSector(a, slot.place);
    std::string objs;
    if (sector >= 0)
      for (int ar : v.world().sectors[sector].arenas)
        for (int o : v.world().arenas[ar].objects) objs += v.world().objects[o].name + ", ";
    std::string prompt = "It is " + v.clockText() + ". " + a.p.first + " is " + slot.activity + " for " +
                         std::to_string(slot.minutes) +
                         " minutes. Break this into 5-15 minute subtasks. One per line, formatted exactly as "
                         "'subtask | minutes | object', choosing the object from: " + objs;
    auto r = ask(v, i, prompt, 300, 0.7);
    std::vector<Task> out;
    for (auto& l : lines(r.text)) {
      std::vector<std::string> parts;
      std::stringstream ss(l);
      std::string part;
      while (std::getline(ss, part, '|')) parts.push_back(trim(part));
      if (parts.size() < 3 || parts[0].empty()) continue;
      Task t;
      t.desc = lower(parts[0]);
      t.minutes = std::clamp(std::atoi(parts[1].c_str()), 5, 60);
      t.objectKw = lower(parts[2]);
      t.place = slot.place;
      t.emoji = emoji(t.desc);
      out.push_back(std::move(t));
    }
    return out.empty() ? base : out;
  }

  bool wantsToChat(Ville& v, int a, int b, sl::SeededRandom& rng) override { return fallback_.wantsToChat(v, a, b, rng); }

  std::vector<std::pair<int, std::string>> converse(Ville& v, int a, int b, int news, sl::SeededRandom& rng) override {
    if (!live(v, a)) return fallback_.converse(v, a, b, news, rng);
    const Agent& A = v.agents()[a];
    const Agent& B = v.agents()[b];
    std::string prompt = "It is " + v.clockText() + ". " + A.p.first + " is " + doing(A) + "; " + B.p.name + " (" +
                         B.p.learned + ") is " + doing(B) + ". They run into each other.\nWhat " + A.p.first +
                         " remembers about " + B.p.first + ":\n" + memories(v, a, B.p.name, 5) +
                         (news >= 0 ? A.p.first + " wants to mention: " + v.news()[news].text + "\n" : "") +
                         "Write their short conversation (4-8 lines), one line per turn, formatted exactly as "
                         "'Name: utterance', using the names " + A.p.first + " and " + B.p.first + ".";
    auto r = ask(v, a, prompt, 500, 0.9);
    std::vector<std::pair<int, std::string>> out;
    for (auto& l : lines(r.text)) {
      size_t c = l.find(':');
      if (c == std::string::npos) continue;
      std::string who = trim(l.substr(0, c)), text = trim(l.substr(c + 1));
      if (text.empty()) continue;
      int speaker = who.find(B.p.first) != std::string::npos ? b : a;
      out.push_back({speaker, text});
    }
    return out.size() >= 2 ? out : fallback_.converse(v, a, b, news, rng);
  }

  std::vector<std::string> reflect(Ville& v, int i, sl::SeededRandom& rng) override {
    if (!live(v, i)) return fallback_.reflect(v, i, rng);
    const Agent& a = v.agents()[i];
    std::string stmts;
    int from = std::max(0, static_cast<int>(a.memory.size()) - 30), k = 1;
    for (size_t m = from; m < a.memory.size(); ++m) stmts += std::to_string(k++) + ". " + a.memory[m].description + "\n";
    auto r = ask(v, i, "Statements about " + a.p.first + ":\n" + stmts +
                           "What 3 high-level insights can you infer from the above statements? One per line, "
                           "written in the first person as " + a.p.first + ".",
                 300, 0.7);
    std::vector<std::string> out;
    for (auto& l : lines(r.text)) out.push_back(trim(l));
    if (out.size() > 3) out.resize(3);
    return out.empty() ? fallback_.reflect(v, i, rng) : out;
  }

 private:
  PersonaCognition fallback_;
};

}  // namespace

float Cognition::importance(const std::string& d) const {
  // The paper asks the model for a 1-10 poignancy rating; offline, a
  // keyword scale in the same range.
  if (has(d, "sleep") || has(d, "idle")) return 1;
  if (has(d, "party") || has(d, "mayor") || has(d, "election") || has(d, "invite")) return 8;
  if (has(d, "convers") || has(d, "chat") || has(d, "talk")) return 5;
  if (has(d, "shower") || has(d, "bathroom") || has(d, "dressed") || has(d, "stretch") || has(d, "dishes")) return 1;
  if (has(d, "eat") || has(d, "lunch") || has(d, "breakfast") || has(d, "dinner") || has(d, "coffee")) return 2;
  if (has(d, "lecture") || has(d, "class") || has(d, "research") || has(d, "writing") || has(d, "painting")) return 4;
  return 3;
}

std::string Cognition::emoji(const std::string& d) const {
  struct E {
    const char* kw;
    const char* e;
  };
  static const E kMap[] = {
      {"sleep", "\xF0\x9F\x98\xB4"},   {"shower", "\xF0\x9F\x9A\xBF"},   {"bathroom", "\xF0\x9F\x9A\xBD"},
      {"stretch", "\xF0\x9F\x99\x86"}, {"dressed", "\xF0\x9F\x91\x95"},  {"party", "\xF0\x9F\x8E\x89"},
      {"breakfast", "\xF0\x9F\x8D\xB3"}, {"cook", "\xF0\x9F\x8D\xB3"},   {"coffee", "\xE2\x98\x95"},
      {"brew", "\xE2\x98\x95"},        {"lunch", "\xF0\x9F\x8D\xBD"},    {"eat", "\xF0\x9F\x8D\xBD"},
      {"dinner", "\xF0\x9F\x8D\xBD"},  {"drink", "\xF0\x9F\x8D\xBA"},    {"pool", "\xF0\x9F\x8E\xB1"},
      {"perform", "\xF0\x9F\x8E\xA4"}, {"lecture", "\xF0\x9F\x93\x9A"},  {"class", "\xF0\x9F\x93\x9A"},
      {"reading", "\xF0\x9F\x93\x96"}, {"book", "\xF0\x9F\x93\x96"},     {"writing", "\xF0\x9F\x93\x9D"},
      {"notes", "\xF0\x9F\x93\x9D"},   {"paint", "\xF0\x9F\x8E\xA8"},    {"sketch", "\xF0\x9F\x8E\xA8"},
      {"code", "\xF0\x9F\x92\xBB"},    {"video call", "\xF0\x9F\x92\xBB"}, {"photo", "\xF0\x9F\x93\xB7"},
      {"walk", "\xF0\x9F\x9A\xB6"},    {"bench", "\xF0\x9F\x8C\xB3"},    {"view", "\xF0\x9F\x8C\xB3"},
      {"campaign", "\xF0\x9F\x93\xA3"}, {"flyer", "\xF0\x9F\x93\xA3"},   {"grocer", "\xF0\x9F\x9B\x92"},
      {"shelves", "\xF0\x9F\x9B\x92"}, {"customer", "\xF0\x9F\x99\x8B"}, {"pour", "\xF0\x9F\x8D\xBA"},
      {"music", "\xF0\x9F\x8E\xB5"},   {"compos", "\xF0\x9F\x8E\xB5"},   {"garden", "\xF0\x9F\x8C\xB1"},
      {"plant", "\xF0\x9F\x8C\xB1"},   {"tv", "\xF0\x9F\x93\xBA"},       {"clean", "\xF0\x9F\xA7\xB9"},
      {"wash", "\xF0\x9F\xA7\xB9"},    {"newspaper", "\xF0\x9F\x93\xB0"}, {"client", "\xF0\x9F\x92\xBC"},
  };
  for (auto& e : kMap)
    if (has(d, e.kw)) return e.e;
  return "\xF0\x9F\x92\xAD";  // thought bubble
}

std::unique_ptr<Cognition> makePersonaCognition() { return std::make_unique<PersonaCognition>(); }
std::unique_ptr<Cognition> makeLlmCognition(int maxConcurrency) { return std::make_unique<LlmCognition>(maxConcurrency); }

}  // namespace ville
