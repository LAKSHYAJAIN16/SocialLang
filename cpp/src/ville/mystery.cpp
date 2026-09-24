// Mysteries: a murder played out inside the town (see MysterySpec).
//
// Nothing here tells residents who did it. They only know what they saw:
// every resident keeps a log of who they saw where and when (sightings). Once
// a body is found, a sighting of someone at the scene around the time of the
// crime is evidence against them, and a sighting somewhere else is an alibi.
// Residents trade what they saw in conversation -- trusting friends more --
// and the killer lies: an alibi for themselves, and a sighting that points at
// someone else. At the meeting, everyone who knows votes; a wrong arrest and
// the killer strikes again.
#include <algorithm>
#include <cmath>

#include "ville/cognition.h"
#include "ville/sim.h"

namespace ville {

namespace {

constexpr int kWindow = 60;      // minutes either side of a crime a sighting matters
constexpr int kHuntLead = 60;    // minutes before the crime the killer sets out

std::string clockOf(int minuteOfDay) {
  int h = (minuteOfDay / 60) % 24, m = minuteOfDay % 60;
  char buf[24];
  std::snprintf(buf, sizeof buf, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "am" : "pm");
  return buf;
}

}  // namespace

std::string Ville::minuteText(int64_t minute) const {
  minute = (minute / 5) * 5;  // people round
  int day = static_cast<int>(minute / 1440);
  std::string t = clockOf(static_cast<int>(minute % 1440));
  if (day == dayIndex()) return "around " + t;
  if (day == dayIndex() - 1) return "yesterday around " + t;
  return "around " + t + " on day " + std::to_string(day + 1);
}

void Ville::setupMystery() {
  const MysterySpec& m = opts_.spec.behavior.mystery;
  int n = static_cast<int>(agents_.size());
  if (!m.on || n < 3) return;
  case_.on = true;
  case_.name = m.name;
  auto find = [&](const std::string& name) {
    for (int i = 0; i < n; ++i)
      if (agents_[i].p.name == name) return i;
    return -1;
  };
  sl::SeededRandom rng(opts_.spec.env.seed * 7919u + 17u);
  case_.target = m.victim == "random" ? -1 : find(m.victim);
  if (case_.target < 0) case_.target = static_cast<int>(rng.index(n));
  case_.killer = m.killer == "random" ? -1 : find(m.killer);
  while (case_.killer < 0 || case_.killer == case_.target) case_.killer = static_cast<int>(rng.index(n));
  case_.crimeDay = m.day;
  case_.crimeMinute = m.minute;
  case_.meetingSector = world_.findSector(m.meeting);
  for (size_t s = 0; case_.meetingSector < 0 && s < world_.sectors.size(); ++s)
    if (world_.sectors[s].kind == SectorKind::TownHall) case_.meetingSector = static_cast<int>(s);
  if (case_.meetingSector < 0) case_.meetingSector = 0;
  MemoryNode mem;
  mem.type = NodeType::Thought;
  mem.description = "I have a secret: I am going to kill " + agents_[case_.target].p.name + ". No one can ever know.";
  mem.poignancy = 9;
  mem.person = case_.target;
  agents_[case_.killer].memory.push_back(mem);
}

bool Ville::knowsCase(int i) const {
  if (!case_.on) return false;
  for (int n : agents_[i].knows)
    if (n >= 0 && n < (int)news_.size() && news_[n].name.rfind("the murder of ", 0) == 0) return true;
  return false;
}

void Ville::recordSightings(int i) {
  if (!case_.on || case_.phase >= CaseState::Solved) return;
  Agent& a = agents_[i];
  int r = rulesFor(i).visionRadius;
  int myArena = world_.arenaAt(a.x, a.y);
  int sector = myArena >= 0 ? world_.arenas[myArena].sector : -1;
  int64_t now = minutesSinceStart();
  int cx0 = std::max(0, (a.x - r) / kGridCell), cx1 = std::min(gridW_ - 1, (a.x + r) / kGridCell);
  int cy0 = std::max(0, (a.y - r) / kGridCell), cy1 = std::min(gridH_ - 1, (a.y + r) / kGridCell);
  for (int cy = cy0; cy <= cy1; ++cy)
    for (int cx = cx0; cx <= cx1; ++cx)
      for (int j : grid_[cy * gridW_ + cx]) {
        if (j == i) continue;
        const Agent& b = agents_[j];
        if (b.dead || std::abs(b.x - a.x) > r || std::abs(b.y - a.y) > r) continue;
        if (world_.arenaAt(b.x, b.y) != myArena) continue;
        auto it = a.lastSighting.find(j);
        if (it != a.lastSighting.end() && it->second.first == sector && now - it->second.second < 10) continue;
        a.lastSighting[j] = {sector, now};
        Sighting s{j, sector, now, -1, -1};
        if (b.action.rfind("hurrying away from ", 0) == 0 && !case_.crimes.empty()) s.fleeing = case_.crimes.back().sector;
        a.sightings.push_back(s);
      }
  if (a.sightings.size() > 800) a.sightings.erase(a.sightings.begin(), a.sightings.begin() + 200);
}

void Ville::startHunt() {
  case_.phase = CaseState::Hunting;
  Agent& k = agents_[case_.killer];
  k.asleep = false;
  // The first victim is kept late at the scene (the cafe owner closing up).
  const MysterySpec& m = opts_.spec.behavior.mystery;
  int at = case_.crimes.empty() && !m.at.empty() ? world_.findSector(m.at) : -1;
  Agent& v = agents_[case_.target];
  if (at >= 0 && v.schedule.size() == 24) {
    for (int h = (case_.crimeMinute - kHuntLead) / 60; h <= case_.crimeMinute / 60 && h < 24; ++h) {
      if (h < 0 || resolveSector(v, v.schedule[h].place) == at) continue;
      v.schedule[h] = {"staying late at " + world_.sectors[at].name, "sector:" + std::to_string(at), 60};
      if (v.slot == h && v.chatWith < 0) startSlot(case_.target, h);
    }
  }
}

void Ville::kill(int victim) {
  Agent& v = agents_[victim];
  Agent& k = agents_[case_.killer];
  for (int who : {victim, case_.killer}) {
    int o = agents_[who].chatWith;
    if (o < 0) continue;
    finishChat(who);
    if (agents_[o].chatWith == who) finishChat(o);
  }
  v.dead = true;
  v.asleep = false;
  v.path.clear();
  v.tasks.clear();
  v.targetObject = v.usingObject = -1;
  v.action = "lying motionless on the floor";
  v.emoji = "\xF0\x9F\x92\x80";
  Crime c;
  c.victim = victim;
  int arena = world_.arenaAt(v.x, v.y);
  c.sector = arena >= 0 ? world_.arenas[arena].sector : -1;
  c.at = minutesSinceStart();
  case_.crimes.push_back(c);
  case_.phase = CaseState::Undiscovered;
  std::string place = c.sector >= 0 ? world_.sectors[c.sector].name : "the street";
  // The scapegoat: whoever else was closest to the scene.
  int best = -1, bestD = 1 << 30;
  for (int j = 0; j < (int)agents_.size(); ++j) {
    const Agent& o = agents_[j];
    if (j == victim || j == case_.killer || o.dead || o.arrested) continue;
    int d = std::abs(o.x - v.x) + std::abs(o.y - v.y);
    if (d < bestD) bestD = d, best = j;
  }
  case_.scapegoat = best;
  MemoryNode m;
  m.type = NodeType::Thought;
  m.description = "I killed " + v.p.name + " at " + place + ". No one can know. If anyone asks, I was at home" +
                  (best >= 0 ? ", and I saw " + agents_[best].p.name + " near " + place + "." : ".");
  m.poignancy = 10;
  m.person = victim;
  addMemory(case_.killer, std::move(m));
  emit(7, case_.killer, "[secret] " + k.p.name + " killed " + v.p.name + " at " + place + ".", victim);
  // Get away -- anyone on the street sees someone in a hurry.
  k.path.clear();
  if (k.schedule.size() == 24) {
    int h = minuteOfDay() / 60;
    k.schedule[h] = {"hurrying away from " + place, "home", 60};
    startSlot(case_.killer, h);
  }
}

void Ville::discover(int finder, int victim) {
  Crime* c = nullptr;
  for (auto& cr : case_.crimes)
    if (cr.victim == victim) c = &cr;
  if (!c || finder == case_.killer) return;
  Agent& f = agents_[finder];
  const Agent& v = agents_[victim];
  std::string place = c->sector >= 0 ? world_.sectors[c->sector].name : "the street";
  if (c->discovered) {  // another body-finder: they know first-hand
    int n = -1;
    for (size_t k = 0; k < news_.size(); ++k)
      if (news_[k].name == "the murder of " + v.p.name) n = static_cast<int>(k);
    if (n >= 0 && std::find(f.knows.begin(), f.knows.end(), n) == f.knows.end()) {
      f.knows.push_back(n);
      applyMystery(finder);
    }
    return;
  }
  c->discovered = true;
  c->finder = finder;
  if (case_.searcher >= 0) {  // back to their day
    Agent& s = agents_[case_.searcher];
    case_.searcher = -1;
    s.slot = -1;
  }
  int day = dayIndex();
  const MysterySpec& spec = opts_.spec.behavior.mystery;
  case_.meetingDay = minuteOfDay() < spec.meetingStart - 60 ? day : day + 1;
  case_.phase = CaseState::Investigating;
  News n;
  n.name = "the murder of " + v.p.name;
  n.text = v.p.name + " was found dead at " + place + " -- someone killed " + v.p.first + ". The town will meet at " +
           world_.sectors[case_.meetingSector].name + " at " + clockOf(spec.meetingStart) +
           (case_.meetingDay == day ? " today." : " tomorrow.");
  n.origin = finder;
  n.sector = c->sector;
  n.day = day;
  news_.push_back(n);
  case_.news = static_cast<int>(news_.size()) - 1;
  f.knows.push_back(case_.news);
  MemoryNode m;
  m.type = NodeType::Event;
  m.description = "I found " + v.p.name + "'s body at " + place + ". Someone killed " + v.p.first + ".";
  m.poignancy = 10;
  m.news = case_.news;
  m.person = victim;
  addMemory(finder, std::move(m));
  emit(6, finder, f.p.name + " found " + v.p.name + " dead at " + place + ".", victim);
  Agent& k = agents_[case_.killer];
  if (std::find(k.knows.begin(), k.knows.end(), case_.news) == k.knows.end()) k.knows.push_back(case_.news);
  applyMystery(finder);
  applyMystery(case_.killer);
}

void Ville::applyMystery(int i) {
  if (!case_.on || case_.phase != CaseState::Investigating) return;
  Agent& a = agents_[i];
  if (a.dead || a.arrested || a.schedule.size() != 24 || a.day != case_.meetingDay || !knowsCase(i)) return;
  const MysterySpec& spec = opts_.spec.behavior.mystery;
  std::string victim = case_.crimes.empty() ? "the victim" : agents_[case_.crimes.back().victim].p.first;
  for (int h = spec.meetingStart / 60; h < (spec.meetingEnd + 59) / 60 && h < 24; ++h)
    a.schedule[h] = {"attending the town meeting about " + victim + "'s death", "sector:" + std::to_string(case_.meetingSector), 60};
  int now = minuteOfDay() / 60;
  if (now >= spec.meetingStart / 60 && now < (spec.meetingEnd + 59) / 60 && a.slot == now && a.chatWith < 0) {
    a.slot = -1;  // restart this hour as the meeting
  }
}

std::vector<std::pair<int, float>> Ville::suspects(int i) const {
  std::vector<std::pair<int, float>> out;
  if (!case_.on || !knowsCase(i)) return out;
  if (i == case_.killer) {
    int s = case_.scapegoat;
    if (s >= 0 && !agents_[s].dead && !agents_[s].arrested) out.push_back({s, 10.0f});
    return out;
  }
  const Agent& a = agents_[i];
  auto trust = [&](int from) {
    auto it = a.familiarity.find(from);
    float fam = it == a.familiarity.end() ? 0.0f : it->second;
    return 0.6f + std::min(fam, 8.0f) * 0.05f;
  };
  std::unordered_map<int, float> score;
  for (const Crime& c : case_.crimes) {
    if (!c.discovered) continue;
    for (const Sighting& s : a.sightings) {
      if (s.who == i || s.who == c.victim) continue;
      int64_t dt = std::llabs(s.minute - c.at);
      if (dt > kWindow) continue;
      float w = s.from < 0 ? 1.0f : 0.5f * trust(s.from);
      // The closer to the moment, the more a sighting says.
      float close = dt <= 20 ? 3.0f : dt <= 45 ? 1.5f : 0.5f;
      if (s.fleeing == c.sector && s.minute >= c.at) score[s.who] += 5.0f * w;  // running from the scene
      else if (s.sector == c.sector) score[s.who] += close * w;
      else score[s.who] -= close * w;  // seen somewhere else: an alibi
    }
  }
  for (auto& [who, s] : a.caughtLying) score[who] += 4.0f;  // their story doesn't hold up
  for (auto& [j, v] : score) {
    const Agent& o = agents_[j];
    if (v <= 0 || o.dead || o.arrested) continue;
    float s = v;
    if (auto n = a.relationNote.find(j); n != a.relationNote.end() && n->second.find("rival") != std::string::npos) s += 0.5f;
    out.push_back({j, s});
  }
  std::sort(out.begin(), out.end(), [](auto& x, auto& y) { return x.second != y.second ? x.second > y.second : x.first < y.first; });
  return out;
}

std::vector<std::string> Ville::evidence(int i, int suspect) const {
  std::vector<std::string> out;
  if (!case_.on) return out;
  if (i == case_.killer && suspect == case_.scapegoat) {
    out.push_back("(secretly the killer) pointing everyone at " + agents_[suspect].p.name);
    return out;
  }
  const Agent& a = agents_[i];
  if (auto l = a.caughtLying.find(suspect); l != a.caughtLying.end()) {
    std::string where = l->second.sector >= 0 ? world_.sectors[l->second.sector].name : "out on the street";
    out.push_back(agents_[suspect].p.first + " says they were home all night, but I saw them at " + where + " " +
                  minuteText(l->second.minute));
  }
  for (const Crime& c : case_.crimes) {
    if (!c.discovered) continue;
    std::string scene = c.sector >= 0 ? world_.sectors[c.sector].name : "the street";
    for (auto it = a.sightings.rbegin(); it != a.sightings.rend() && out.size() < 4; ++it) {
      const Sighting& s = *it;
      if (s.who != suspect || std::llabs(s.minute - c.at) > kWindow) continue;
      std::string where = s.sector >= 0 ? world_.sectors[s.sector].name : "out on the street";
      std::string seen = agents_[suspect].p.name + " at " + where + " " + minuteText(s.minute);
      if (s.fleeing == c.sector && s.minute >= c.at)
        seen = agents_[suspect].p.name + " hurrying away from " + scene + " " + minuteText(s.minute);
      std::string line = s.from < 0 ? "I saw " + seen : agents_[s.from].p.first + " told me they saw " + seen;
      if (s.sector != c.sector && s.fleeing != c.sector) line += ", nowhere near " + scene;
      if (std::find(out.begin(), out.end(), line) == out.end()) out.push_back(line);
    }
  }
  return out;
}

bool Ville::caseTopic(int a, int b, int share) const {
  if (!case_.on || case_.phase < CaseState::Investigating || !knowsCase(a)) return false;
  if (knowsCase(b)) return true;
  return share >= 0 && news_[share].name.rfind("the murder of ", 0) == 0;
}

std::string Ville::caseLine(int speaker, int listener) const {
  if (!case_.on || case_.crimes.empty() || !knowsCase(speaker)) return "";
  const Crime* c = nullptr;
  for (auto& cr : case_.crimes)
    if (cr.discovered) c = &cr;
  if (!c) return "";
  const Agent& v = agents_[c->victim];
  std::string scene = c->sector >= 0 ? world_.sectors[c->sector].name : "the street";
  if (speaker == case_.killer) {
    int s = case_.scapegoat;
    int64_t when = c->at + ((speaker * 7 + listener * 13) % 40) - 20;
    if (s >= 0 && s != listener && !agents_[s].dead)
      return "It's awful about " + v.p.first + ". I was home all night -- but I did see " + agents_[s].p.name + " near " + scene +
             " " + minuteText(when) + ".";
    return "It's awful about " + v.p.first + ". I was home all night, I didn't see a thing.";
  }
  auto sus = suspects(speaker);
  if (!sus.empty()) {
    auto ev = evidence(speaker, sus.front().first);
    if (!ev.empty()) return "I can't stop thinking about " + v.p.first + ". " + ev.front() + ".";
  }
  return "I can't believe what happened to " + v.p.first + ". I didn't see anything that night.";
}

void Ville::shareEvidence(int from, int to) {
  if (!case_.on || case_.crimes.empty() || from == to) return;
  const Crime* c = nullptr;
  for (auto& cr : case_.crimes)
    if (cr.discovered) c = &cr;
  if (!c) return;
  Agent& T = agents_[to];
  if (from == case_.killer) {
    // Lies: a sighting of the scapegoat at the scene, and an alibi.
    int s = case_.scapegoat;
    if (s >= 0 && s != to) T.sightings.push_back({s, c->sector, c->at + ((from * 7 + to * 13) % 40) - 20, from, -1});
    // "I was home all night" -- unless the listener saw them somewhere else.
    for (const Sighting& own : T.sightings)
      if (own.from < 0 && own.who == from && own.sector != agents_[from].p.home && std::llabs(own.minute - c->at) <= 45) {
        T.caughtLying[from] = own;
        emit(6, to, T.p.first + " knows " + agents_[from].p.first + " is lying about where they were that night.", from);
        return;
      }
    T.sightings.push_back({from, agents_[from].p.home, c->at, from, -1});
    return;
  }
  auto sus = suspects(from);
  if (sus.empty()) return;
  int suspect = sus.front().first;
  const Agent& F = agents_[from];
  // Someone caught lying is the first thing worth passing on.
  if (auto l = F.caughtLying.find(suspect); l != F.caughtLying.end()) {
    Sighting h = l->second;
    h.from = from;
    T.sightings.push_back(h);
  }
  int copied = 0;
  for (const Sighting& s : F.sightings) {
    if (copied >= 2 || s.from >= 0 || s.who != suspect || std::llabs(s.minute - c->at) > kWindow) continue;
    Sighting h = s;
    h.from = from;
    T.sightings.push_back(h);
    ++copied;
  }
  // And whatever they saw of the person the listener suspects -- often an
  // alibi ("no, I saw her at the pub all evening").
  auto theirs = suspects(to);
  if (theirs.empty() || theirs.front().first == suspect || theirs.front().first == from) return;
  copied = 0;
  for (const Sighting& s : F.sightings) {
    if (copied >= 2 || s.from >= 0 || s.who != theirs.front().first || std::llabs(s.minute - c->at) > 45) continue;
    Sighting h = s;
    h.from = from;
    T.sightings.push_back(h);
    ++copied;
  }
}

void Ville::holdVote() {
  Verdict vd;
  vd.day = dayIndex();
  std::vector<int> candidates, voters;
  for (int j = 0; j < (int)agents_.size(); ++j) {
    const Agent& a = agents_[j];
    if (a.dead || a.arrested) continue;
    candidates.push_back(j);
    if (knowsCase(j)) voters.push_back(j);
  }
  std::unordered_map<int, int> tally;
  for (int voter : voters) {
    auto rng = rngFor(voter, 0xB07E);
    int s = cog_->accuse(*this, voter, candidates, rng);
    vd.votes.push_back({voter, s});
    const Agent& V = agents_[voter];
    if (s < 0) {
      emit(2, voter, V.p.first + ": I don't know who did it. I won't accuse anyone without proof.");
      continue;
    }
    ++tally[s];
    auto ev = evidence(voter, s);
    std::string why = voter == case_.killer || ev.empty() ? "" : " " + ev.front() + ".";
    if (voter == case_.killer) why = " I saw them near the scene myself.";
    emit(2, voter, V.p.first + ": I vote for " + agents_[s].p.name + "." + why, s);
  }
  int best = -1, bestN = 0;
  bool tie = false;
  for (auto& [s, n] : tally) {
    if (n > bestN || (n == bestN && s < best)) {
      tie = n == bestN;
      best = s;
      bestN = n;
    } else if (n == bestN) {
      tie = true;
    }
  }
  // An arrest needs a clear plurality: no tie, and at least a third of the room.
  if (tie || bestN * 3 < static_cast<int>(voters.size())) best = -1;
  vd.accused = best;
  vd.correct = best >= 0 && best == case_.killer;
  case_.verdicts.push_back(vd);
  int author = best >= 0 ? best : (voters.empty() ? case_.killer : voters.front());
  std::string place = world_.sectors[case_.meetingSector].name;
  if (best >= 0) {
    Agent& A = agents_[best];
    if (A.chatWith >= 0) {
      int o = A.chatWith;
      finishChat(best);
      if (agents_[o].chatWith == best) finishChat(o);
    }
    A.arrested = true;
    A.path.clear();
    A.tasks.clear();
    A.targetObject = A.usingObject = -1;
    A.action = "held at " + place;
    A.emoji = "\xF0\x9F\x94\x92";
    emit(6, author, "The town voted to arrest " + A.p.name + " (" + std::to_string(bestN) + " of " +
                        std::to_string(voters.size()) + " votes).");
  } else {
    emit(6, author, "The town couldn't agree on anyone. Nobody was arrested.");
  }
  if (vd.correct) {
    case_.phase = CaseState::Solved;
    case_.outcome = "Solved on day " + std::to_string(vd.day + 1) + ": the town arrested " + agents_[best].p.name + ", the killer.";
    emit(6, author, "Case closed. " + agents_[best].p.name + " was the killer.");
    return;
  }
  ++case_.round;
  if (case_.round > opts_.spec.behavior.mystery.rounds) {
    case_.phase = CaseState::KillerWon;
    case_.outcome = "The killer got away: it was " + agents_[case_.killer].p.name + ".";
    emit(6, case_.killer, "The town gave up. The killer is still out there.");
    return;
  }
  // The killer strikes again -- whoever is closest to the truth.
  int next = -1;
  float nextScore = 0;
  for (int j : candidates) {
    if (j == case_.killer || agents_[j].arrested) continue;
    for (auto& [s, sc] : suspects(j))
      if (s == case_.killer && sc > nextScore) next = j, nextScore = sc;
  }
  if (next < 0) {
    std::vector<int> pool;
    for (int j : candidates)
      if (j != case_.killer && !agents_[j].arrested) pool.push_back(j);
    if (pool.empty()) {
      case_.phase = CaseState::KillerWon;
      case_.outcome = "The killer got away: it was " + agents_[case_.killer].p.name + ".";
      return;
    }
    auto rng = rngFor(case_.killer, 0x7A26);
    next = pool[rng.index(pool.size())];
  }
  case_.target = next;
  const MysterySpec& spec = opts_.spec.behavior.mystery;
  case_.crimeDay = minuteOfDay() < spec.minute - kHuntLead ? dayIndex() : dayIndex() + 1;
  case_.crimeMinute = spec.minute;
  case_.phase = CaseState::Waiting;
}

void Ville::updateMystery() {
  if (!case_.on) return;
  int64_t now = minutesSinceStart();
  int64_t crimeAt = static_cast<int64_t>(case_.crimeDay) * 1440 + case_.crimeMinute;
  switch (case_.phase) {
    case CaseState::Waiting:
      if (now >= crimeAt - kHuntLead) startHunt();
      break;
    case CaseState::Hunting: {
      Agent& k = agents_[case_.killer];
      Agent& v = agents_[case_.target];
      if (v.dead || v.arrested || k.arrested) {
        case_.phase = CaseState::Waiting;
        break;
      }
      if (k.chatWith >= 0) {
        int o = k.chatWith;
        finishChat(case_.killer);
        if (agents_[o].chatWith == case_.killer) finishChat(o);
      }
      k.asleep = false;
      int varena = world_.arenaAt(v.x, v.y);
      bool close = std::max(std::abs(k.x - v.x), std::abs(k.y - v.y)) <= 1 && world_.arenaAt(k.x, k.y) == varena;
      if (now >= crimeAt && close) {
        kill(case_.target);
        break;
      }
      if (now >= crimeAt + 180) {  // never let the night stall
        k.x = v.x;
        k.y = v.y;
        kill(case_.target);
        break;
      }
      if (close) k.path.clear();
      else if (k.path.empty() || step_ % 6 == 0) k.path = world_.path(k.x, k.y, v.x, v.y);
      int vsector = varena >= 0 ? world_.arenas[varena].sector : -1;
      std::string where = vsector >= 0 ? world_.sectors[vsector].name : "town";
      k.action = "stopping by " + where;
      k.emoji = cog_->emoji(k.action);
      k.targetObject = -1;
      k.addressText = world_.name + ":" + where;
      break;
    }
    case CaseState::Undiscovered: {
      // Nobody has found the body by the next morning: whoever is closest to
      // the victim goes looking.
      Crime& c = case_.crimes.back();
      int64_t deadline = (c.at / 1440 + 1) * 1440 + 9 * 60;
      if (c.finder == -2 && case_.searcher >= 0) {  // walk right up to the body
        Agent& s = agents_[case_.searcher];
        const Agent& v = agents_[c.victim];
        if (s.chatWith < 0 && s.path.empty() && std::max(std::abs(s.x - v.x), std::abs(s.y - v.y)) > 1)
          s.path = world_.path(s.x, s.y, v.x, v.y);
        break;
      }
      if (now < deadline || c.finder == -2) break;
      c.finder = -2;
      int best = -1;
      float bestF = -1;
      for (int j = 0; j < (int)agents_.size(); ++j) {
        const Agent& o = agents_[j];
        if (j == case_.killer || o.dead || o.arrested || o.asleep || o.schedule.size() != 24) continue;
        auto it = o.familiarity.find(c.victim);
        float f = it == o.familiarity.end() ? 0.0f : it->second;
        if (f > bestF) bestF = f, best = j;
      }
      if (best < 0) break;
      Agent& s = agents_[best];
      int h = minuteOfDay() / 60;
      s.schedule[h] = {"looking for " + agents_[c.victim].p.first, "sector:" + std::to_string(c.sector >= 0 ? c.sector : agents_[c.victim].p.home), 60};
      if (s.chatWith < 0) startSlot(best, h);
      s.targetObject = -1;
      s.path = world_.path(s.x, s.y, agents_[c.victim].x, agents_[c.victim].y);
      case_.searcher = best;
      emit(6, best, s.p.name + " hasn't seen " + agents_[c.victim].p.first + " all morning and goes looking.", c.victim);
      break;
    }
    case CaseState::Investigating: {
      const MysterySpec& spec = opts_.spec.behavior.mystery;
      if (dayIndex() == case_.meetingDay && minuteOfDay() >= spec.meetingEnd &&
          static_cast<int>(case_.verdicts.size()) < case_.round)
        holdVote();
      break;
    }
    default:
      break;
  }
}

// The persona model's vote: its top suspect, if the evidence is real.
int Cognition::accuse(Ville& v, int voter, const std::vector<int>& candidates, sl::SeededRandom& rng) {
  (void)rng;
  for (auto& [j, s] : v.suspects(voter))
    if (s >= 1.0f && j != voter && std::find(candidates.begin(), candidates.end(), j) != candidates.end()) return j;
  return -1;
}

}  // namespace ville
