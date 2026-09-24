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

// ---- Everything below reads the behavior file: routines, everyday
// entries, free time, and activity breakdowns.

std::string fill(std::string text, const Persona& p, const std::string& activity = "") {
  auto sub = [&](const std::string& key, const std::string& value) {
    for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size()))
      text.replace(at, key.size(), value);
  };
  sub("{currently}", p.currently);
  sub("{name}", p.first);
  sub("{activity}", activity);
  return text;
}

// Short "I'm ..." description of an action.
std::string doing(const Agent& a) { return a.action.empty() ? "out and about" : a.action; }

// Hour of an entry end point for this resident, clamped to the day.
int hourOf(const HourRef& h, const Persona& p) { return std::clamp(h.at(p.wakeHour, p.sleepHour), 0, 24); }

class PersonaCognition : public Cognition {
 public:
  void planDay(Ville& v, int i, sl::SeededRandom& rng, std::vector<std::string>& plan,
               std::vector<HourSlot>& schedule) override {
    const Persona& p = v.agents()[i].p;
    const BehaviorSpec& b = v.behavior();
    int wake = std::clamp(p.wakeHour, 0, 23), sleep = std::clamp(p.sleepHour, wake + 1, 24);
    // Hour by hour: sleeping, then the routine, then everyday entries (meals,
    // the morning routine), then free time for whatever is still open.
    std::vector<HourSlot> day(24);
    std::vector<char> set(24, 0);
    for (int h = 0; h < 24; ++h) day[h] = {"sleeping", "home", 60};
    auto apply = [&](const std::vector<RoutineEntry>& entries, bool overwrite) {
      for (auto& e : entries) {
        if (e.options.empty()) continue;
        const auto& opt = e.options[rng.index(e.options.size())];
        int from = hourOf(e.from, p), to = hourOf(e.to, p);
        for (int h = from; h < to && h < 24; ++h) {
          if (h < wake || h >= sleep) continue;
          if (set[h] && !overwrite) continue;
          day[h] = {fill(opt.first, p), opt.second, 60};
          set[h] = 1;
        }
      }
    };
    if (auto it = b.routines.find(p.archetype); it != b.routines.end()) apply(it->second, true);
    apply(b.everyday, false);
    float total = 0;
    for (auto& f : b.freeTime) total += f.weight * (f.social ? 0.5f + p.sociability : 1.0f);
    for (int h = wake; h < sleep && h < 24; ++h) {
      if (set[h] || b.freeTime.empty()) continue;
      float roll = static_cast<float>(rng.random()) * total;
      const FreeTimeSpec* pick = &b.freeTime.back();
      for (auto& f : b.freeTime) {
        float w = f.weight * (f.social ? 0.5f + p.sociability : 1.0f);
        if (roll < w) {
          pick = &f;
          break;
        }
        roll -= w;
      }
      day[h] = {fill(pick->activity, p), pick->place, 60};
      set[h] = 1;
    }
    schedule = day;
    // Broad-strokes plan.
    plan.clear();
    std::string last;
    for (int h = wake; h < sleep && h < 24; ++h)
      if (schedule[h].activity != last) {
        last = schedule[h].activity;
        plan.push_back(last + " at " + hourText(h));
      }
    plan.push_back("go to bed at " + hourText(sleep % 24));
  }

  std::vector<Task> decompose(Ville& v, int i, const HourSlot& slot, sl::SeededRandom& rng) override {
    (void)rng;
    const BehaviorSpec& b = v.behavior();
    const Persona& p = v.agents()[i].p;
    std::string act = lower(slot.activity);
    // The first activity whose name matches (and whose place, if given,
    // matches) wins; an activity with no names is the default.
    const ActivitySpec* match = nullptr;
    const ActivitySpec* fallback = nullptr;
    for (auto& a : b.activities) {
      if (a.match.empty()) {
        if (!fallback) fallback = &a;
        continue;
      }
      if (!a.place.empty() && a.place != slot.place) continue;
      for (auto& m : a.match)
        if (act.find(lower(m)) != std::string::npos) {
          match = &a;
          break;
        }
      if (match) break;
    }
    if (!match) match = fallback;
    std::vector<Task> out;
    if (!match || match->steps.empty()) {
      out.push_back({slot.activity, slot.place, "", slot.minutes, emoji(slot.activity)});
      return out;
    }
    int sum = 0;
    for (auto& s : match->steps) sum += s.minutes;
    for (auto& s : match->steps) {
      Task t;
      t.desc = fill(s.desc, p, slot.activity);
      t.place = slot.place;
      t.objectKw = s.object;
      t.minutes = std::max(5, s.minutes * slot.minutes / std::max(sum, 1));
      t.emoji = emoji(t.desc);
      out.push_back(std::move(t));
    }
    return out;
  }

  bool wantsToChat(Ville& v, int a, int b, sl::SeededRandom& rng) override {
    const Agent& A = v.agents()[a];
    const Agent& B = v.agents()[b];
    if (A.asleep || B.asleep || A.chatWith >= 0 || B.chatWith >= 0) return false;
    if (has(A.action, "sleep") || has(B.action, "sleep")) return false;
    const SocialRules& rules = v.rulesFor(a);
    auto it = A.lastChat.find(b);
    if (it != A.lastChat.end() && v.stepCount() - it->second < static_cast<int64_t>(rules.chatCooldownHours * kStepsPerHour))
      return false;
    float fam = 0;
    if (auto f = A.familiarity.find(b); f != A.familiarity.end()) fam = f->second;
    if (fam == 0 && !rules.strangersTalk) return false;
    if (auto n = A.relationNote.find(b); n != A.relationNote.end() && has(lower(n->second), "rival")) fam = 0;
    // Someone with news the other hasn't heard is much keener to talk.
    bool hasNews = false;
    for (int n : A.knows)
      if (std::find(B.knows.begin(), B.knows.end(), n) == B.knows.end()) hasNews = true;
    double p = 0.012 * rules.chattiness * (0.4 + A.p.sociability) * (1 + fam) * (hasNews ? rules.newsEagerness : 1.0);
    return rng.random() < p;
  }

  std::vector<std::pair<int, std::string>> converse(Ville& v, int a, int b, int news, sl::SeededRandom& rng) override {
    const Agent& A = v.agents()[a];
    const Agent& B = v.agents()[b];
    std::vector<std::pair<int, std::string>> out;
    int h = v.minuteOfDay() / 60;
    const char* hello = h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening";
    bool met = A.familiarity.count(b) > 0;
    std::string note;
    if (auto nt = A.relationNote.find(b); nt != A.relationNote.end()) note = lower(nt->second);
    std::string greet = std::string(hello) + ", " + B.p.first + "! How's your day going?";
    if (has(note, "crush")) greet = "Oh -- hi, " + B.p.first + "! I was, um, hoping I'd run into you.";
    else if (has(note, "rival")) greet = B.p.first + ". Didn't expect to see you here.";
    else if (has(note, "friend") || has(note, "family") || has(note, "married")) greet = "Hey " + B.p.first + "! Always good to see you.";
    out.push_back({a, met ? greet : "Hi, I don't think we've met -- I'm " + A.p.first + ". " + hello + "!"});
    out.push_back({b, met ? "Hey " + A.p.first + "! Pretty good -- I've been " + doing(B) + "."
                          : "Nice to meet you, " + A.p.first + ". I'm " + B.p.first + ". I've been " + doing(B) + "."});
    if (v.caseTopic(a, b, news)) {
      // A murder: what each of them saw (or claims to have seen).
      if (news >= 0 && v.news()[news].name.rfind("the murder of ", 0) == 0) {
        out.back().second = "Hey " + A.p.first + ". Is something wrong? You look pale.";
        out.push_back({a, "Did you hear? " + v.news()[news].text});
        out.push_back({b, "What? That's horrible. I had no idea."});
      }
      std::string mine = v.caseLine(a, b), theirs = v.caseLine(b, a);
      if (!mine.empty()) out.push_back({a, mine});
      out.push_back({b, !theirs.empty() && news < 0 ? theirs
                         : rng.random() < 0.5 ? "I don't know what to think. Who would do something like that?"
                                              : "That's terrifying. I'm going to keep my eyes open."});
      out.push_back({a, "Stay safe, " + B.p.first + ". I'll see you at the meeting."});
      out.push_back({b, "You too, " + A.p.first + "."});
      return out;
    }
    if (news >= 0) {
      const News& n = v.news()[news];
      if (n.origin == a && n.invite) {
        std::string when = n.day == v.dayIndex() ? "today" : n.day == v.dayIndex() + 1 ? "tomorrow" : "soon";
        std::string what = n.name.empty() ? "a get-together" : n.name;
        std::string where = n.sector >= 0 ? " at " + v.world().sectors[n.sector].name : "";
        out.push_back({a, "I'm hosting " + what + where + " " + when + ", from " + hourText(n.startMin / 60) + " to " +
                              hourText(n.endMin / 60) + ". I'd love for you to come!"});
      } else {
        std::string t = n.text;
        out.push_back({a, "Did you hear? " + t});
      }
      if (n.invite) {
        double yes = std::clamp((0.45 + 0.4 * B.p.sociability) * v.rulesFor(b).inviteAcceptance, 0.0, 1.0);
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
    // Longer conversations (Rules > Conversation length): more exchanges,
    // drawing on what each remembers from today.
    for (int k = 0; k < v.rulesFor(a).conversationLength; ++k) {
      int speaker = k % 2 ? b : a, listener = k % 2 ? a : b;
      const Agent& S = v.agents()[speaker];
      auto mem = v.retrieve(speaker, "today", 4);
      std::string said;
      for (int m : mem)
        if (S.memory[m].type != NodeType::Thought && S.memory[m].person != listener && S.memory[m].person >= 0) {
          said = "Earlier I saw " + S.memory[m].subject + " " + S.memory[m].predicate + ".";
          break;
        }
      if (said.empty()) said = k % 2 ? "Anything exciting planned for later?" : "It's been a pretty full day so far.";
      out.push_back({speaker, said});
      out.push_back({listener, rng.random() < 0.5 ? "Oh, interesting." : "Ha, I know what you mean."});
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
    if (auto sus = v.suspects(i); !sus.empty() && sus.front().second >= 1.0f && i != v.mystery().killer) {
      const Persona& s = v.agents()[sus.front().first].p;
      out.push_back("I keep coming back to " + s.name + ". I can't shake the feeling " + s.first + " had something to do with it.");
    }
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
      std::string what = nw.name.empty() ? "event" : nw.name;
      std::string where = nw.sector >= 0 ? " at " + v.world().sectors[nw.sector].name : "";
      if (nw.invite)
        out.push_back(going ? "I'm looking forward to " + v.agents()[nw.origin].p.first + "'s " + what + where + "."
                            : "People are talking about " + v.agents()[nw.origin].p.first + "'s " + what + where + ".");
      else
        out.push_back("Many people in town are talking about the fact that " + nw.text.substr(0, nw.text.size() - 1) + ".");
      if (out.size() >= 3) break;
    }
    if (out.size() < 3) out.push_back("I have been " + ag.p.currently + " and I feel I'm making progress.");
    return out;
  }
};

// ---- LLM cognition: prompts through each agent's own provider,
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
  void setBehavior(const BehaviorSpec* b) override {
    behavior_ = b;
    fallback_.setBehavior(b);
  }

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
                         caseContext(v, a, b, news) +
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

  // What a murder adds to a conversation prompt: what each of them knows,
  // and, for the killer, the secret they're keeping.
  static std::string caseContext(Ville& v, int a, int b, int news) {
    if (!v.caseTopic(a, b, news)) return "";
    const Agent& A = v.agents()[a];
    const Agent& B = v.agents()[b];
    std::string out = "They are both shaken by a murder in town and talk about it.\n";
    std::string la = v.caseLine(a, b), lb = v.caseLine(b, a);
    if (!la.empty()) out += A.p.first + " would say: " + la + "\n";
    if (!lb.empty() && news < 0) out += B.p.first + " would say: " + lb + "\n";
    int k = v.mystery().killer;
    if (k == a || k == b) {
      const Agent& K = v.agents()[k];
      int s = v.mystery().scapegoat;
      out += "Secretly, " + K.p.first + " is the killer. " + K.p.first + " never admits it, claims to have been home, " +
             (s >= 0 ? "and steers suspicion toward " + v.agents()[s].p.name + ".\n" : "and stays calm.\n");
    }
    return out;
  }

  int accuse(Ville& v, int voter, const std::vector<int>& candidates, sl::SeededRandom& rng) override {
    if (!live(v, voter)) return fallback_.accuse(v, voter, candidates, rng);
    const Agent& a = v.agents()[voter];
    std::string known;
    for (auto& [s, score] : v.suspects(voter)) {
      for (auto& e : v.evidence(voter, s)) known += "- " + e + "\n";
      if (known.size() > 600) break;
    }
    std::string names;
    for (int c : candidates)
      if (c != voter) names += (names.empty() ? "" : ", ") + v.agents()[c].p.name;
    const CaseState& cs = v.mystery();
    std::string victim = cs.crimes.empty() ? "someone" : v.agents()[cs.crimes.back().victim].p.name;
    std::string secret = voter == cs.killer && cs.scapegoat >= 0
                             ? "Secretly, you are the killer. Vote for someone else -- " + v.agents()[cs.scapegoat].p.name +
                                   " is the easiest to blame.\n"
                             : "";
    std::string prompt = "It is " + v.clockText() + ". The town is meeting about the murder of " + victim + ".\n" + secret +
                         "What " + a.p.first + " knows:\n" + (known.empty() ? "- nothing concrete\n" : known) +
                         "Memories:\n" + memories(v, voter, "murder " + victim, 6) +
                         "Who does " + a.p.first + " vote to arrest? Choose one of: " + names +
                         ". Answer with just the full name, or 'nobody' if there's no real evidence.";
    auto r = ask(v, voter, prompt, 40, 0.3);
    std::string reply = lower(r.text);
    if (reply.find("nobody") != std::string::npos) return -1;
    int pick = -1;
    size_t pickAt = std::string::npos;
    for (int c : candidates) {
      if (c == voter) continue;
      size_t at = reply.find(lower(v.agents()[c].p.name));
      if (at == std::string::npos) at = reply.find(lower(v.agents()[c].p.first));
      if (at != std::string::npos && at < pickAt) pickAt = at, pick = c;
    }
    if (pick >= 0) return pick;
    return r.error.empty() ? -1 : fallback_.accuse(v, voter, candidates, rng);  // a failed call votes on the evidence
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
  // 1-10, the first keyword from the behavior file's importance { } that
  // appears in the description.
  if (behavior_)
    for (auto& [kw, v] : behavior_->importance)
      if (has(d, kw.c_str())) return v;
  return 3;
}

std::string Cognition::emoji(const std::string& d) const {
  if (behavior_)
    for (auto& [kw, e] : behavior_->emoji)
      if (has(d, kw.c_str())) return e;
  return "\xF0\x9F\x92\xAD";  // thought bubble
}

std::unique_ptr<Cognition> makePersonaCognition() { return std::make_unique<PersonaCognition>(); }
std::unique_ptr<Cognition> makeLlmCognition(int maxConcurrency) { return std::make_unique<LlmCognition>(maxConcurrency); }

}  // namespace ville
