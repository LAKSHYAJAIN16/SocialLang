#include "ville/sim.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "engine/memory.h"
#include "engine/parallel.h"
#include "ville/cognition.h"

namespace ville {

namespace {

std::mutex gEmitMu;
constexpr int kGridCell = 8;

const char* kDays[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                         "July",    "August",   "September", "October", "November", "December"};

}  // namespace

// Calendar: the environment's start date ("Monday, February 13, 2023"), then
// day by day from there.
std::string clockFor(const EnvironmentSpec& env, long long step, bool shortForm) {
  long long minutes = static_cast<long long>(env.startHour) * 60 + step * kSecondsPerStep / 60;
  int d = static_cast<int>(minutes / 1440), m = static_cast<int>(minutes % 1440);
  int weekday = 0, month = 1, day = 1, year = 2023;
  for (int k = 0; k < 7; ++k)
    if (env.startDate.find(kDays[k]) != std::string::npos) weekday = k;
  for (int k = 0; k < 12; ++k)
    if (env.startDate.find(kMonths[k]) != std::string::npos) month = k;
  std::sscanf(env.startDate.c_str() + std::min(env.startDate.size(), env.startDate.find(kMonths[month]) + std::string(kMonths[month]).size()),
              " %d, %d", &day, &year);
  for (int k = 0; k < d; ++k) {
    static const int kLen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int len = kLen[month] + (month == 1 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (++day > len) {
      day = 1;
      if (++month > 11) {
        month = 0;
        ++year;
      }
    }
  }
  int h = m / 60, mm = m % 60;
  char buf[96];
  if (shortForm)
    std::snprintf(buf, sizeof buf, "%.3s %.3s %d, %d:%02d %s", kDays[(weekday + d) % 7], kMonths[month], day,
                  h % 12 == 0 ? 12 : h % 12, mm, h < 12 ? "am" : "pm");
  else
    std::snprintf(buf, sizeof buf, "%s, %s %d, %d -- %d:%02d %s", kDays[(weekday + d) % 7], kMonths[month], day, year,
                  h % 12 == 0 ? 12 : h % 12, mm, h < 12 ? "am" : "pm");
  return buf;
}

Ville::Ville(const VilleOptions& opts, const std::vector<std::shared_ptr<sl::Provider>>& roster,
             std::function<void(const VilleEvent&)> sink)
    : opts_(opts), sink_(std::move(sink)), roster_(roster) {
  world_.generate(opts_.spec);
  for (auto& p : roster_) llm_ = llm_ || p->wantsContext();
  cog_ = llm_ ? makeLlmCognition(opts_.maxConcurrency) : makePersonaCognition();
  cog_->setBehavior(&opts_.spec.behavior);
  setupAgents();
  if (agents_.size() > 200) opts_.logActions = false;
}

Ville::~Ville() = default;

void Ville::setupAgents() {
  auto personas = makePersonas(world_, opts_.spec);
  news_ = seedNews(world_, personas, opts_.spec);
  agents_.resize(personas.size());
  for (size_t i = 0; i < personas.size(); ++i) {
    Agent& a = agents_[i];
    a.p = std::move(personas[i]);
    a.rngState = opts_.spec.env.seed * 2654435761u + static_cast<uint32_t>(i) * 40503u;
    a.provider = roster_.empty() ? nullptr : roster_[i % roster_.size()].get();
    // Start in bed, asleep.
    int bedroom = world_.findArena(a.p.home, a.p.bedArena);
    if (bedroom < 0) bedroom = world_.findArena(a.p.home, "bedroom");
    int bed = -1;
    if (bedroom >= 0)
      for (int o : world_.arenas[bedroom].objects)
        if (world_.objects[o].name == "bed") bed = o;
    if (bed < 0) bed = world_.findObject(a.p.home, "bed");
    auto [x, y] = bed >= 0 ? world_.standTileFor(bed)
                           : std::pair<int, int>{world_.sectors[std::max(0, a.p.home)].rect.cx(),
                                                 world_.sectors[std::max(0, a.p.home)].rect.cy()};
    a.x = x;
    a.y = y;
    a.usingObject = bed;
  }
  for (size_t n = 0; n < news_.size(); ++n) {
    Agent& o = agents_[news_[n].origin];
    o.knows.push_back(static_cast<int>(n));
    if (news_[n].invite) o.attending.push_back(static_cast<int>(n));
    MemoryNode m;
    m.type = NodeType::Thought;
    m.description = news_[n].invite ? "I am hosting " + (news_[n].name.empty() ? std::string("an event") : news_[n].name) +
                                          " and I want to invite everyone. " + news_[n].text
                                    : news_[n].text;
    m.poignancy = 8;
    m.news = static_cast<int>(n);
    o.memory.push_back(m);
  }
  // Relationships from the town spec: they start out knowing each other.
  for (auto& rel : opts_.spec.env.relationships) {
    int a = -1, b = -1;
    for (size_t i = 0; i < agents_.size(); ++i) {
      if (agents_[i].p.name == rel.a) a = static_cast<int>(i);
      if (agents_[i].p.name == rel.b) b = static_cast<int>(i);
    }
    if (a < 0 || b < 0 || a == b) continue;
    agents_[a].familiarity[b] = agents_[b].familiarity[a] = rel.closeness;
    agents_[a].relationNote[b] = rel.note;
    if (!rel.note.empty()) {
      MemoryNode m;
      m.type = NodeType::Thought;
      m.description = agents_[a].p.name + " " + rel.note + " " + agents_[b].p.name + ".";
      m.poignancy = 6;
      m.person = b;
      agents_[a].memory.push_back(m);
    }
  }
  gridW_ = world_.width() / kGridCell + 1;
  gridH_ = world_.height() / kGridCell + 1;
  grid_.assign(gridW_ * gridH_, {});
}

int64_t Ville::minutesSinceStart() const { return static_cast<int64_t>(opts_.spec.env.startHour) * 60 + step_ * kSecondsPerStep / 60; }
int Ville::dayIndex() const { return static_cast<int>(minutesSinceStart() / 1440); }
int Ville::minuteOfDay() const { return static_cast<int>(minutesSinceStart() % 1440); }

std::string Ville::clockText() const { return clockFor(opts_.spec.env, step_, false); }

sl::SeededRandom Ville::rngFor(int agent, uint32_t salt) const {
  return sl::SeededRandom(agents_[agent].rngState ^ static_cast<uint32_t>(step_ * 2246822519u) ^ salt);
}

void Ville::emit(int kind, int agent, const std::string& text, int other) {
  if (!sink_) return;
  std::lock_guard<std::mutex> lock(gEmitMu);
  sink_({step_, kind, agent, other, text});
}

// Resolves a schedule place ("home", "cafe", "event:0", ...) to a sector: the
// agent's own home or workplace, or the nearest place of that kind.
int Ville::resolveSector(const Agent& a, const std::string& place) const {
  if (place == "home") return a.p.home;
  if (place.rfind("event:", 0) == 0) {
    int n = std::atoi(place.c_str() + 6);
    return n >= 0 && n < (int)news_.size() ? news_[n].sector : a.p.home;
  }
  if (place == "work" || place == "office") {
    if (place == "office" && a.p.work >= 0 && world_.sectors[a.p.work].kind == SectorKind::College) return a.p.work;
    return a.p.work >= 0 ? a.p.work : a.p.home;
  }
  SectorKind want = SectorKind::Park;
  if (place == "cafe") want = SectorKind::Cafe;
  else if (place == "pub") want = SectorKind::Pub;
  else if (place == "park") want = SectorKind::Park;
  else if (place == "market") want = SectorKind::Market;
  else if (place == "store") want = SectorKind::Store;
  else if (place == "townhall") want = SectorKind::TownHall;
  else if (place == "classroom" || place == "library" || place == "college") want = SectorKind::College;
  else return a.p.home;
  // The resident's own workplace first, else the nearest of that kind.
  int best = -1, bestD = 1 << 30;
  const Sector& home = world_.sectors[std::max(0, a.p.home)];
  for (size_t s = 0; s < world_.sectors.size(); ++s) {
    if (world_.sectors[s].kind != want) continue;
    int d = std::abs(world_.sectors[s].rect.cx() - home.rect.cx()) + std::abs(world_.sectors[s].rect.cy() - home.rect.cy());
    if (a.p.work == static_cast<int>(s)) d = -1;
    if (d < bestD) {
      bestD = d;
      best = static_cast<int>(s);
    }
  }
  return best >= 0 ? best : a.p.home;
}

// Memory retrieval: recency + importance +
// relevance, each normalized to [0, 1], equally weighted.
std::vector<int> Ville::retrieve(int i, const std::string& focal, int k) const {
  const Agent& a = agents_[i];
  if (a.memory.empty()) return {};
  sl::RelevanceQuery rq(focal);
  std::vector<std::pair<float, int>> scored;
  scored.reserve(a.memory.size());
  for (size_t m = 0; m < a.memory.size(); ++m) {
    const MemoryNode& n = a.memory[m];
    double hours = static_cast<double>(step_ - n.lastAccess) / kStepsPerHour;
    float recency = static_cast<float>(std::pow(0.995, hours * 6));
    float relevance = rq.words.empty() ? 0 : static_cast<float>(rq.score(n.description));
    scored.push_back({recency + n.poignancy / 10.0f + relevance, static_cast<int>(m)});
  }
  size_t take = std::min<size_t>(k, scored.size());
  std::partial_sort(scored.begin(), scored.begin() + take, scored.end(),
                    [](auto& x, auto& y) { return x.first > y.first; });
  std::vector<int> out;
  for (size_t j = 0; j < take; ++j) out.push_back(scored[j].second);
  return out;
}

void Ville::addMemory(int i, MemoryNode node) {
  Agent& a = agents_[i];
  node.created = node.lastAccess = step_;
  a.importanceSinceReflect += node.poignancy;
  a.memory.push_back(std::move(node));
}

void Ville::rebuildGrid() {
  for (auto& c : grid_) c.clear();
  for (size_t i = 0; i < agents_.size(); ++i) {
    const Agent& a = agents_[i];
    grid_[(a.y / kGridCell) * gridW_ + a.x / kGridCell].push_back(static_cast<int>(i));
  }
}

// Perception: other agents within the vision radius in the same arena (or
// both outdoors), and in-use objects nearby. An observation already recorded
// in the agent's recent memory isn't recorded again.
void Ville::perceive(int i, std::vector<MemoryNode>& out) {
  Agent& a = agents_[i];
  if (a.asleep) return;
  const SocialRules& rules = rulesFor(i);
  int r = rules.visionRadius;
  int myArena = world_.arenaAt(a.x, a.y);
  // Retention: only a change in what someone (or something) is doing is news.
  auto changed = [&](int key, const std::string& what) {
    size_t h = std::hash<std::string>()(what);
    auto it = a.lastSeen.find(key);
    if (it != a.lastSeen.end() && it->second == h) return false;
    a.lastSeen[key] = h;
    return true;
  };
  int cx0 = std::max(0, (a.x - r) / kGridCell), cx1 = std::min(gridW_ - 1, (a.x + r) / kGridCell);
  int cy0 = std::max(0, (a.y - r) / kGridCell), cy1 = std::min(gridH_ - 1, (a.y + r) / kGridCell);
  int seen = 0;
  for (int cy = cy0; cy <= cy1; ++cy)
    for (int cx = cx0; cx <= cx1; ++cx)
      for (int j : grid_[cy * gridW_ + cx]) {
        if (j == i || seen >= rules.attention) continue;
        const Agent& b = agents_[j];
        if (std::abs(b.x - a.x) > r || std::abs(b.y - a.y) > r) continue;
        if (world_.arenaAt(b.x, b.y) != myArena) continue;
        if (b.asleep || !changed(j, b.action)) continue;
        std::string desc = b.p.name + " is " + b.action;
        MemoryNode m;
        m.type = NodeType::Event;
        m.subject = b.p.name;
        m.predicate = b.action;
        m.description = desc;
        // Someone else's routine is less poignant than one's own life.
        m.poignancy = std::max(1.0f, cog_->importance(b.action) - 2);
        m.person = j;
        out.push_back(std::move(m));
        ++seen;
      }
  if (myArena >= 0)
    for (int o : world_.arenas[myArena].objects) {
      const GameObject& obj = world_.objects[o];
      if (obj.state == "idle" || std::abs(obj.x - a.x) > r || std::abs(obj.y - a.y) > r) continue;
      if (seen >= rules.attention || !changed(-1 - o, obj.state)) continue;
      ++seen;
      std::string desc = obj.name + " is " + obj.state;
      MemoryNode m;
      m.subject = obj.name;
      m.predicate = obj.state;
      m.description = desc;
      m.poignancy = 1;
      out.push_back(std::move(m));
    }
}

// Waking plan: broad strokes, then the hourly schedule.
void Ville::planDay(int i) {
  Agent& a = agents_[i];
  auto rng = rngFor(i, 0x51ED);
  cog_->planDay(*this, i, rng, a.dailyPlan, a.schedule);
  a.day = dayIndex();
  applyInvites(i);
  std::string summary;
  for (size_t k = 0; k < a.dailyPlan.size() && k < 6; ++k) summary += (k ? "; " : "") + a.dailyPlan[k];
  emit(1, i, "Plan for today: " + summary);
  MemoryNode m;
  m.type = NodeType::Thought;
  m.description = "This is " + a.p.first + "'s plan for " + clockText().substr(0, clockText().find(',')) + ": " + summary;
  m.poignancy = 5;
  addMemory(i, std::move(m));
  a.slot = -1;
}

// An accepted invitation takes over the matching hours of that day's schedule.
void Ville::applyInvites(int i) {
  Agent& a = agents_[i];
  if (a.schedule.size() != 24) return;
  for (int n : a.attending) {
    const News& nw = news_[n];
    if (nw.day != a.day) continue;
    for (int h = nw.startMin / 60; h < nw.endMin / 60 && h < 24; ++h) {
      a.schedule[h].activity = nw.origin == i ? "hosting " + (nw.name.empty() ? std::string("an event") : nw.name) +
                                                    (nw.sector >= 0 ? " at " + world_.sectors[nw.sector].name : std::string())
                                              : nw.activity;
      a.schedule[h].place = "event:" + std::to_string(n);
    }
  }
}

void Ville::startSlot(int i, int slot) {
  Agent& a = agents_[i];
  a.slot = slot;
  a.slotStartMin = slot * 60;
  HourSlot s = a.schedule[slot];
  // Merge identical consecutive hours into one decomposition.
  int end = slot + 1;
  while (end < 24 && a.schedule[end].activity == s.activity && a.schedule[end].place == s.place) ++end;
  s.minutes = std::min(60, (end - slot) * 60);
  auto rng = rngFor(i, 0xDEC0);
  a.tasks = cog_->decompose(*this, i, s, rng);
  a.task = -1;
  startTask(i, 0);
}

void Ville::startTask(int i, int t) {
  Agent& a = agents_[i];
  if (a.tasks.empty()) return;
  if (t >= (int)a.tasks.size()) t = static_cast<int>(a.tasks.size()) - 1;  // hold the last task until the hour ends
  a.task = t;
  const Task& task = a.tasks[t];
  a.taskEnd = step_ + static_cast<int64_t>(task.minutes) * kStepsPerMinute;
  bool changed = a.action != task.desc;
  a.action = task.desc;
  a.emoji = task.emoji;
  a.asleep = task.desc == "sleeping";
  // Where: sector from the schedule place, then the object (preferring the
  // agent's own bedroom for "bed").
  int sector = resolveSector(a, task.place);
  int obj = -1;
  if (!task.objectKw.empty()) {
    if (task.objectKw == "bed" || task.objectKw == "closet" || task.objectKw == "desk") {
      int room = world_.findArena(a.p.home, a.p.bedArena);
      if (room >= 0 && sector == a.p.home)
        for (int o : world_.arenas[room].objects)
          if (world_.objects[o].name.find(task.objectKw) != std::string::npos) obj = o;
    }
    if (obj < 0) obj = world_.findObject(sector, task.objectKw);
  }
  int tx, ty;
  if (obj >= 0) {
    std::tie(tx, ty) = world_.standTileFor(obj);
    a.addressText = world_.address(obj);
  } else if (sector >= 0 && !world_.sectors[sector].arenas.empty()) {
    int arena = world_.sectors[sector].arenas[a.rngState % world_.sectors[sector].arenas.size()];
    std::tie(tx, ty) = world_.freeTileIn(arena, a.rngState + static_cast<uint32_t>(step_));
    a.addressText = world_.name + ":" + world_.sectors[sector].name + ":" + world_.arenas[arena].name;
  } else {
    tx = a.x;
    ty = a.y;
  }
  a.targetObject = obj;
  if (a.x != tx || a.y != ty) a.path = world_.path(a.x, a.y, tx, ty);
  if (changed && opts_.logActions && !a.asleep) emit(0, i, a.action + " at " + a.addressText);
}

void Ville::learn(int i, int n, int from) {
  Agent& a = agents_[i];
  if (std::find(a.knows.begin(), a.knows.end(), n) != a.knows.end()) return;
  a.knows.push_back(n);
  MemoryNode m;
  m.type = NodeType::Event;
  m.description = agents_[from].p.first + " told me that " + news_[n].text;
  m.poignancy = cog_->importance(news_[n].text);
  m.news = n;
  m.person = from;
  addMemory(i, std::move(m));
  emit(4, i, a.p.name + " heard from " + agents_[from].p.name + ": " + news_[n].text, from);
}

void Ville::beginChat(int a, int b) {
  Agent& A = agents_[a];
  Agent& B = agents_[b];
  // What A most wants to share: news B hasn't heard (its own invite first).
  int share = -1;
  for (int n : A.knows)
    if (std::find(B.knows.begin(), B.knows.end(), n) == B.knows.end()) {
      if (share < 0 || news_[n].origin == a) share = n;
    }
  auto rng = rngFor(a, 0xC4A7 + b);
  auto lines = cog_->converse(*this, a, b, share, rng);
  if (lines.empty()) return;
  for (Agent* p : {&A, &B}) {
    p->chatWith = p == &A ? b : a;
    p->chatNews = share;
    p->chatLines = lines;
    p->chatLineAt.clear();
    for (size_t k = 0; k < lines.size(); ++k) p->chatLineAt.push_back(step_ + static_cast<int64_t>(k) * kStepsPerMinute / 2);
    p->chatNext = 0;
    p->chatEnd = step_ + static_cast<int64_t>(lines.size()) * kStepsPerMinute / 2 + 1;
    p->path.clear();
    p->action = "chatting with " + (p == &A ? B.p.first : A.p.first);
    p->emoji = "\xF0\x9F\x92\xAC";
  }
  A.lastChat[b] = B.lastChat[a] = step_;
  // Knowledge transfer happens when the news is actually said.
  if (share >= 0) {
    learn(b, share, a);
    if (news_[share].invite) {
      // Did B accept? The reply right after the invite line decides.
      // The reply that follows the line where the invite is actually made.
      for (size_t k = 0; k + 1 < lines.size(); ++k)
        if (lines[k].first == a && lines[k + 1].first == b &&
            (lines[k].second.find("hosting") != std::string::npos || lines[k].second.find("Did you hear") != std::string::npos ||
             (!news_[share].name.empty() && lines[k].second.find(news_[share].name) != std::string::npos))) {
          std::string reply = lines[k + 1].second;
          for (auto& c : reply) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          bool yes = reply.find("be there") != std::string::npos || reply.find("i'll come") != std::string::npos ||
                     reply.find("love to") != std::string::npos || reply.find("count me in") != std::string::npos ||
                     reply.find("sounds lovely") != std::string::npos || reply.find("sounds great") != std::string::npos ||
                     reply.find("sounds fun") != std::string::npos || reply.find("i'll try") != std::string::npos;
          if (yes && reply.find("not sure") == std::string::npos) {
            B.attending.push_back(share);
            if (B.day == news_[share].day) applyInvites(b);
          }
          break;
        }
    }
  }
}

void Ville::finishChat(int i) {
  Agent& a = agents_[i];
  int other = a.chatWith;
  a.chatWith = -1;
  if (other < 0) return;
  a.familiarity[other] += 1;
  std::string topic;
  if (a.chatNews >= 0) topic = " about " + (news_[a.chatNews].name.empty() ? std::string("town news") : news_[a.chatNews].name);
  a.chatNews = -1;
  MemoryNode m;
  m.type = NodeType::Chat;
  m.subject = a.p.name;
  m.predicate = "chatted with " + agents_[other].p.name + topic;
  m.description = a.p.name + " had a conversation with " + agents_[other].p.name + topic;
  m.poignancy = topic.empty() ? 4 : 6;
  m.person = other;
  addMemory(i, std::move(m));
  a.chatLines.clear();
  a.chatLineAt.clear();
  // Resume the plan where it was.
  if (a.task >= 0) startTask(i, a.task);
}

void Ville::reflect(int i) {
  Agent& a = agents_[i];
  auto rng = rngFor(i, 0x2EF1);
  auto insights = cog_->reflect(*this, i, rng);
  for (auto& s : insights) {
    MemoryNode m;
    m.type = NodeType::Thought;
    m.description = s;
    m.poignancy = 7;
    a.memory.push_back(m);
    a.memory.back().created = a.memory.back().lastAccess = step_;
    emit(3, i, s);
  }
  a.importanceSinceReflect = 0;
}

bool Ville::step() {
  if (dayIndex() >= opts_.spec.env.days) return true;
  int n = static_cast<int>(agents_.size());
  int minute = minuteOfDay();
  // Measured: below ~600 residents a step is too small to amortize the pool
  // (25: 0.18 s serial vs 1.3 s parallel for two game days; 250: 2.5 vs 2.9 s
  // per day; 1000: 29 vs 24.5 s), so
  // small towns run serially; LLM calls always fan out.
  auto par = [&](const std::function<void(size_t)>& fn) {
    if (opts_.parallel && !llm_ && n >= 600) sl::ThreadPool::shared().parallelFor(n, fn, 16);
    else if (!llm_) {
      for (int i = 0; i < n; ++i) fn(i);
    }
    else if (opts_.parallel) sl::ThreadPool::shared().parallelFor(n, fn, 1);  // LLM calls: one agent per chunk
    else
      for (int i = 0; i < n; ++i) fn(i);
  };

  // 1. Perceive (parallel: each agent reads the shared world, writes its own memory).
  rebuildGrid();
  std::vector<std::vector<MemoryNode>> seen(n);
  par([&](size_t i) { perceive(static_cast<int>(i), seen[i]); });
  for (int i = 0; i < n; ++i)
    for (auto& m : seen[i]) addMemory(i, std::move(m));

  // 2. React: someone perceived nearby may start a conversation (sequential,
  //    so pairings are deterministic).
  for (int i = 0; i < n; ++i) {
    if (agents_[i].asleep || agents_[i].chatWith >= 0) continue;
    for (auto& m : seen[i]) {
      if (m.person < 0) continue;
      auto rng = rngFor(i, 0x7A1C + m.person);
      if (cog_->wantsToChat(*this, i, m.person, rng)) {
        beginChat(i, m.person);
        break;
      }
    }
  }

  // 3. Conversations: say lines on schedule, end finished ones.
  for (int i = 0; i < n; ++i) {
    Agent& a = agents_[i];
    if (a.chatWith < 0) continue;
    while (a.chatNext < a.chatLines.size() && a.chatLineAt[a.chatNext] <= step_) {
      auto& [speaker, text] = a.chatLines[a.chatNext];
      if (speaker == i) emit(2, i, agents_[i].p.first + ": " + text, a.chatWith);
      ++a.chatNext;
    }
    if (step_ >= a.chatEnd) finishChat(i);
  }

  // 4. Plan: new day on waking, next hour slot, next task (parallel; each
  //    agent only touches its own plan, path finding uses per-thread scratch).
  par([&](size_t idx) {
    int i = static_cast<int>(idx);
    Agent& a = agents_[i];
    if (a.chatWith >= 0) return;
    bool awakeTime = minute >= a.p.wakeHour * 60 && minute < std::min(a.p.sleepHour, 24) * 60;
    if (a.day != dayIndex() && awakeTime) planDay(i);
    if (a.schedule.size() != 24) return;
    int slot = minute / 60;
    if (slot != a.slot) startSlot(i, slot);
    else if (step_ >= a.taskEnd) startTask(i, a.task + 1);
  });

  // 5. Move one tile along the path (parallel).
  par([&](size_t i) {
    Agent& a = agents_[i];
    if (a.chatWith >= 0 || a.path.empty()) return;
    a.x = a.path.front().first;
    a.y = a.path.front().second;
    a.path.erase(a.path.begin());
  });

  // 6. Object states follow who's using them.
  for (auto& o : world_.objects) o.state = "idle";
  for (int i = 0; i < n; ++i) {
    Agent& a = agents_[i];
    a.usingObject = -1;
    if (a.targetObject >= 0 && a.path.empty() && a.chatWith < 0) {
      a.usingObject = a.targetObject;
      world_.objects[a.targetObject].state = a.asleep ? "occupied" : "being used by " + a.p.first + " (" + a.action + ")";
    }
  }

  // 7. Reflect once enough has happened (parallel -- LLM-heavy when live).
  std::vector<int> reflecting;
  for (int i = 0; i < n; ++i)
    if (agents_[i].importanceSinceReflect >= rulesFor(i).reflectThreshold && !agents_[i].asleep) reflecting.push_back(i);
  if (!reflecting.empty()) {
    auto fn = [&](size_t k) { reflect(reflecting[k]); };
    if (opts_.parallel) sl::ThreadPool::shared().parallelFor(reflecting.size(), fn, 1);
    else
      for (size_t k = 0; k < reflecting.size(); ++k) fn(k);
  }

  ++step_;
  return dayIndex() >= opts_.spec.env.days || cancel.load();
}

}  // namespace ville
