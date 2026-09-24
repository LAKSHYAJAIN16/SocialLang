#include "ville/spec.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/ast.h"
#include "ville/config.h"

namespace ville {

namespace fs = std::filesystem;

namespace {

// ---- small helpers

unsigned parseColor(const std::string& s) {
  if (s.size() == 7 && s[0] == '#') return static_cast<unsigned>(std::stoul(s.substr(1), nullptr, 16));
  return 0;
}

std::string colorText(unsigned c) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "#%06X", c & 0xFFFFFF);
  return buf;
}

int parseSize(const std::string& s) { return s == "small" ? 0 : s == "regular" ? 1 : s == "large" ? 2 : -1; }
const char* sizeText(int s) { return s == 0 ? "small" : s == 2 ? "large" : "regular"; }

std::vector<std::string> strings(const CValue* v) {
  std::vector<std::string> out;
  if (!v) return out;
  if (v->kind == CValue::List)
    for (auto& i : v->items) out.push_back(i.text());
  else
    out.push_back(v->text());
  return out;
}

std::string list(const std::vector<std::string>& v) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + quote(v[i]);
  return s + "]";
}

bool truthy(const CNode& n, const std::string& key, bool fallback) {
  const CValue* v = n.field(key);
  if (!v) return fallback;
  return v->text() == "true" || v->text() == "yes";
}

// A bare word if it lexes as one, otherwise a quoted string.
std::string ident(const std::string& s) {
  bool ok = !s.empty() && (std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_');
  for (char c : s) ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
  static const char* kReserved[] = {"sim", "agents", "memory", "role", "fn", "phase", "win_condition", "loop",
                                    "world", "let", "if", "else", "while", "for", "in", "return", "run", "break",
                                    "true", "false", "null", "and", "or", "not"};
  for (auto* r : kReserved) ok = ok && s != r;
  return ok ? s : quote(s);
}

// ---- building look, shared by style / type / building

void readLook(const CNode& n, BuildingSpec& b) {
  b.floorColor = parseColor(n.str("floor"));
  b.wallColor = parseColor(n.str("wall"));
  if (n.field("size")) b.size = parseSize(n.str("size"));
  if (n.field("bedrooms")) b.bedrooms = static_cast<int>(n.num("bedrooms", -1));
  if (n.field("type")) b.kind = n.str("type");
  b.parkObjects = strings(n.field("objects"));
  for (auto& c : n.children) {
    if (c.kind == "room") {
      b.rooms.push_back({c.arg(0, "room"), strings(c.field("objects"))});
      b.customRooms = true;
    } else if (c.kind == "bedroom") {
      b.bedroom = {c.arg(0, "bedroom"), strings(c.field("objects"))};
      b.hasBedroom = true;
    }
  }
}

void writeLook(std::ostringstream& o, const BuildingSpec& b, const std::string& ind, bool withType) {
  if (withType && !b.kind.empty()) o << ind << "type: " << ident(b.kind) << "\n";
  if (b.size >= 0) o << ind << "size: " << sizeText(b.size) << "\n";
  if (b.floorColor) o << ind << "floor: " << quote(colorText(b.floorColor)) << "\n";
  if (b.wallColor) o << ind << "wall: " << quote(colorText(b.wallColor)) << "\n";
  if (b.bedrooms >= 0) o << ind << "bedrooms: " << b.bedrooms << "\n";
  if (!b.parkObjects.empty()) o << ind << "objects: " << list(b.parkObjects) << "\n";
  if (b.customRooms)
    for (auto& r : b.rooms) o << ind << "room " << quote(r.name) << " { objects: " << list(r.objects) << " }\n";
  if (b.hasBedroom)
    o << ind << "bedroom " << (b.bedroom.name == "bedroom" ? "" : quote(b.bedroom.name) + " ") << "{ objects: "
      << list(b.bedroom.objects) << " }\n";
}

bool lookIsEmpty(const BuildingSpec& b) {
  return b.kind.empty() && !b.floorColor && !b.wallColor && b.size < 0 && b.bedrooms < 0 && !b.customRooms &&
         !b.hasBedroom && b.parkObjects.empty();
}

// ---- rules

SocialRules readRules(const CNode& n, const SocialRules& base) {
  SocialRules r = base;
  r.chattiness = static_cast<float>(n.num("chattiness", r.chattiness));
  r.chatCooldownHours = static_cast<float>(n.num("time_between_chats", r.chatCooldownHours));
  r.visionRadius = static_cast<int>(n.num("vision", r.visionRadius));
  r.attention = static_cast<int>(n.num("attention", r.attention));
  r.conversationLength = static_cast<int>(n.num("conversation_length", r.conversationLength));
  r.newsEagerness = static_cast<float>(n.num("news_eagerness", r.newsEagerness));
  r.inviteAcceptance = static_cast<float>(n.num("invite_acceptance", r.inviteAcceptance));
  r.strangersTalk = truthy(n, "strangers_talk", r.strangersTalk);
  r.reflectThreshold = static_cast<float>(n.num("reflect_after", r.reflectThreshold));
  return r;
}

void writeRules(std::ostringstream& o, const std::string& head, const SocialRules& r) {
  o << "  " << head << " {\n"
    << "    chattiness: " << fmtNum(r.chattiness) << "\n"
    << "    time_between_chats: " << fmtNum(r.chatCooldownHours) << "   // game-hours\n"
    << "    conversation_length: " << r.conversationLength << "   // extra exchanges: 0 brief .. 3 long\n"
    << "    strangers_talk: " << (r.strangersTalk ? "true" : "false") << "\n"
    << "    news_eagerness: " << fmtNum(r.newsEagerness) << "\n"
    << "    invite_acceptance: " << fmtNum(r.inviteAcceptance) << "\n"
    << "    vision: " << r.visionRadius << "   // tiles\n"
    << "    attention: " << r.attention << "   // new observations per step\n"
    << "    reflect_after: " << fmtNum(r.reflectThreshold) << "   // summed importance of new memories\n"
    << "  }\n";
}

// ---- routines

bool isWord(const CValue& v, const char* w) { return v.kind == CValue::Word && v.s == w; }
bool isSymbol(const CValue& v, const char* s) { return v.kind == CValue::Symbol && v.s == s; }

// Parses "wake", "wake+1", "sleep", "9" starting at vals[i]; a range token
// like 1..8 after "wake+" also yields the span's end.
HourRef hourAt(const std::vector<CValue>& vals, size_t& i, int* rangeEnd) {
  HourRef h;
  if (i >= vals.size()) return h;
  const CValue& v = vals[i];
  if (v.kind == CValue::Range) {
    h.offset = static_cast<int>(v.n);
    if (rangeEnd) *rangeEnd = static_cast<int>(v.n2);
    ++i;
    return h;
  }
  if (v.kind == CValue::Num) {
    h.offset = static_cast<int>(v.n);
    ++i;
    return h;
  }
  if (isWord(v, "wake") || isWord(v, "sleep")) {
    h.base = isWord(v, "wake") ? 1 : 2;
    ++i;
    if (i < vals.size() && isSymbol(vals[i], "+")) {
      ++i;
      if (i < vals.size() && vals[i].kind == CValue::Num) {
        h.offset = static_cast<int>(vals[i++].n);
      } else if (i < vals.size() && vals[i].kind == CValue::Range) {
        h.offset = static_cast<int>(vals[i].n);
        if (rangeEnd) *rangeEnd = static_cast<int>(vals[i].n2);
        ++i;
      }
    }
  }
  return h;
}

bool parseRoutineLine(const std::vector<CValue>& vals, RoutineEntry& e) {
  size_t i = 0;
  int end = -1;
  e.from = hourAt(vals, i, &end);
  if (end >= 0) {
    e.to.base = 0;
    e.to.offset = end;
  } else {
    if (i >= vals.size() || !isSymbol(vals[i], "..")) return false;
    ++i;
    e.to = hourAt(vals, i, nullptr);
  }
  // "activity" at place (or "activity" at place)*
  while (i < vals.size()) {
    if (isWord(vals[i], "or")) {
      ++i;
      continue;
    }
    if (vals[i].kind != CValue::Str) return false;
    std::string act = vals[i++].s, place = "home";
    if (i + 1 < vals.size() && isWord(vals[i], "at")) {
      place = vals[i + 1].text();
      i += 2;
    }
    e.options.push_back({act, place});
  }
  return !e.options.empty();
}

std::string hourText(const HourRef& h) {
  if (h.base == 0) return std::to_string(h.offset);
  std::string s = h.base == 1 ? "wake" : "sleep";
  if (h.offset) s += "+" + std::to_string(h.offset);
  return s;
}

void writeRoutineLine(std::ostringstream& o, const RoutineEntry& e) {
  std::string span = hourText(e.from) + ".." + hourText(e.to);
  o << "    " << span << std::string(span.size() < 14 ? 14 - span.size() : 1, ' ');
  for (size_t k = 0; k < e.options.size(); ++k)
    o << (k ? " or " : "") << quote(e.options[k].first) << " at " << ident(e.options[k].second);
  o << "\n";
}

}  // namespace

// ---------------- imports

namespace {

std::string readText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw sl::ParseError("cannot open " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  std::string s = ss.str();
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}

// The file's tree with its imports merged underneath it.
CNode loadTree(const std::string& source, const std::string& dir, const std::string& ext, int depth) {
  CNode root = parseConfig(source);
  if (root.imports.empty()) return root;
  if (depth > 8) throw sl::ParseError("line 1: imports nest too deeply (a library importing itself?)");
  CNode merged;
  bool first = true;
  for (auto& name : root.imports) {
    std::string path = findLibrary(dir, name, ext);
    if (path.empty()) throw sl::ParseError("line 1: no library '" + name + "' (looked for lib/" + name + ext + ")");
    CNode lib;
    try {
      lib = loadTree(readText(path), fs::path(path).parent_path().string(), ext, depth + 1);
    } catch (const sl::ParseError& e) {
      throw sl::ParseError(fs::path(path).filename().string() + ", " + e.what());
    }
    if (first) merged = std::move(lib), first = false;
    else mergeConfig(merged, lib);
  }
  if (merged.kind != root.kind)
    throw sl::ParseError("line 1: '" + root.imports[0] + "' is " + (merged.kind.empty() ? "empty" : "a " + merged.kind + " library"));
  std::vector<std::string> imports = root.imports;
  mergeConfig(merged, root);
  merged.imports = imports;
  return merged;
}

// The imports' tree alone (what a file's own text is a diff against).
CNode importsTree(const std::vector<std::string>& imports, const std::string& dir, const std::string& kind, const std::string& ext) {
  std::string src;
  for (auto& i : imports) src += "import " + i + "\n";
  src += kind + " _ { }\n";
  CNode t = loadTree(src, dir, ext, 0);
  return t;
}

}  // namespace

std::string findLibrary(const std::string& dir, const std::string& name, const std::string& ext) {
  std::error_code ec;
  fs::path d = dir.empty() ? fs::path(".") : fs::path(dir);
  std::string file = name.size() > 3 && name.ends_with(".sl") ? name : name + ext;
  for (fs::path p : {d / "lib" / file, d / file, d.parent_path() / "lib" / file})
    if (fs::exists(p, ec)) return p.string();
  return "";
}

// ---------------- environment

EnvironmentSpec parseEnvironment(const std::string& source, const std::string& dir) {
  CNode root = loadTree(source, dir, ".env.sl", 0);
  if (root.kind != "environment") throw sl::ParseError("line 1: expected 'environment Name { ... }'");
  EnvironmentSpec e;
  e.imports = root.imports;
  e.name = root.arg(0, "Town");
  e.behavior = root.str("behavior");
  e.startHour = static_cast<int>(root.num("start_hour", 6));
  e.days = static_cast<int>(root.num("days", 2));
  e.seed = static_cast<uint32_t>(root.num("seed", 1));
  e.startDate = root.str("start", e.startDate);
  for (auto& c : root.children) {
    if (c.kind == "style") {
      readLook(c, e.style);
    } else if (c.kind == "type") {
      BuildingSpec t;
      readLook(c, t);
      t.kind = c.arg(0);
      e.types[t.kind] = t;
    } else if (c.kind == "building") {
      BuildingSpec b;
      b.name = c.arg(0);
      readLook(c, b);
      if (b.kind.empty()) b.kind = "home";
      e.buildings.push_back(b);
    } else if (c.kind == "resident") {
      ResidentSpec r;
      r.name = c.arg(0);
      r.age = static_cast<int>(c.num("age", 30));
      r.innate = c.str("traits");
      r.learned = c.str("background");
      r.currently = c.str("currently");
      r.routine = c.str("routine", "writer");
      r.home = c.str("home");
      r.bedroom = c.str("bedroom", "bedroom");
      r.work = c.str("work");
      r.wakeHour = static_cast<int>(c.num("wakes", 7));
      r.sleepHour = static_cast<int>(c.num("sleeps", 23));
      r.sociability = static_cast<float>(c.num("sociability", 0.5));
      e.residents.push_back(r);
    } else if (c.kind == "relationship") {
      RelationshipSpec r;
      r.a = c.arg(0);
      r.b = c.arg(1);
      r.note = c.str("note");
      r.closeness = static_cast<float>(c.num("closeness", 3));
      e.relationships.push_back(r);
    } else if (c.kind == "event" || c.kind == "news") {
      EventSpec ev;
      ev.name = c.kind == "event" ? c.arg(0) : "";
      ev.text = c.kind == "news" ? c.arg(0) : c.str("text");
      ev.host = c.kind == "news" ? c.str("from") : c.str("host");
      ev.at = c.str("at");
      ev.activity = c.str("activity");
      ev.invite = c.kind == "event" && truthy(c, "invite", true);
      ev.day = static_cast<int>(c.num("day", 0));
      if (const CValue* h = c.field("hours"); h && h->kind == CValue::Range) {
        ev.startMin = static_cast<int>(h->n * 60);
        ev.endMin = static_cast<int>(h->n2 * 60);
      }
      e.events.push_back(ev);
    } else if (c.kind == "generate") {
      GenerateSpec& g = e.generate;
      g.residents = static_cast<int>(c.num("residents", 0));
      g.firstNames = strings(c.field("first_names"));
      g.lastNames = strings(c.field("last_names"));
      g.traits = strings(c.field("traits"));
      for (auto& gc : c.children) {
        if (gc.kind == "one_per")
          for (auto& [k, v] : gc.fields) g.onePer[k] = static_cast<int>(v.number(0));
        if (gc.kind == "routine") {
          GenRoutine r;
          r.name = gc.arg(0);
          r.weight = static_cast<float>(gc.num("weight", 1));
          r.worksAt = gc.str("works_at", "home");
          r.learned = gc.str("background");
          r.currently = gc.str("currently");
          if (const CValue* a = gc.field("ages"); a && a->kind == CValue::Range) {
            r.minAge = static_cast<int>(a->n);
            r.maxAge = static_cast<int>(a->n2);
          }
          g.routines.push_back(r);
        }
      }
    }
  }
  return e;
}

std::string writeEnvironment(const EnvironmentSpec& e) {
  std::ostringstream o;
  o << "// The world of " << e.name << ": buildings, residents, relationships, and events.\n"
    << "// How its residents behave lives in " << (e.behavior.empty() ? "a behavior file" : e.behavior) << ".\n"
    << "environment " << ident(e.name) << " {\n";
  if (!e.behavior.empty()) o << "  behavior: " << quote(e.behavior) << "\n";
  o << "  start: " << quote(e.startDate) << "\n  start_hour: " << e.startHour << "\n  days: " << e.days
    << "\n  seed: " << e.seed << "\n";
  if (!lookIsEmpty(e.style)) {
    o << "\n  // every building\n  style {\n";
    writeLook(o, e.style, "    ", false);
    o << "  }\n";
  }
  if (!e.types.empty()) o << "\n  // building types: the rooms, furniture, and look every building of a type starts with\n";
  for (auto& [k, t] : e.types) {
    o << "  type " << ident(k) << " {\n";
    writeLook(o, t, "    ", false);
    o << "  }\n";
  }
  o << "\n  // buildings, laid out on the street grid in this order\n";
  for (auto& b : e.buildings) {
    bool simple = !b.customRooms && !b.hasBedroom && b.parkObjects.empty();
    if (simple) {
      o << "  building " << quote(b.name) << " { type: " << ident(b.kind);
      if (b.size >= 0) o << "  size: " << sizeText(b.size);
      if (b.bedrooms >= 0) o << "  bedrooms: " << b.bedrooms;
      if (b.floorColor) o << "  floor: " << quote(colorText(b.floorColor));
      if (b.wallColor) o << "  wall: " << quote(colorText(b.wallColor));
      o << " }\n";
    } else {
      o << "  building " << quote(b.name) << " {\n";
      writeLook(o, b, "    ", true);
      o << "  }\n";
    }
  }
  o << "\n  // residents\n";
  for (auto& r : e.residents) {
    o << "  resident " << quote(r.name) << " {\n"
      << "    age: " << r.age << "\n    traits: " << quote(r.innate) << "\n    background: " << quote(r.learned)
      << "\n    currently: " << quote(r.currently) << "\n    routine: " << ident(r.routine) << "\n    home: "
      << quote(r.home) << "\n    bedroom: " << quote(r.bedroom) << "\n";
    if (!r.work.empty()) o << "    work: " << quote(r.work) << "\n";
    o << "    wakes: " << r.wakeHour << "\n    sleeps: " << r.sleepHour << "\n    sociability: " << fmtNum(r.sociability)
      << "\n  }\n";
  }
  if (!e.relationships.empty()) o << "\n  // who already knows whom\n";
  for (auto& r : e.relationships)
    o << "  relationship " << quote(r.a) << " " << quote(r.b) << " { note: " << quote(r.note)
      << "  closeness: " << fmtNum(r.closeness) << " }\n";
  if (!e.events.empty()) o << "\n  // events and news residents start out knowing (and pass on)\n";
  for (auto& ev : e.events) {
    if (!ev.invite && ev.name.empty()) {
      o << "  news " << quote(ev.text) << " { from: " << quote(ev.host) << " }\n";
      continue;
    }
    o << "  event " << quote(ev.name) << " {\n    host: " << quote(ev.host) << "\n    text: " << quote(ev.text)
      << "\n    at: " << quote(ev.at) << "\n    invite: " << (ev.invite ? "true" : "false") << "\n    day: " << ev.day
      << "   // days after the start\n    hours: " << ev.startMin / 60 << ".." << ev.endMin / 60
      << "\n    activity: " << quote(ev.activity) << "\n  }\n";
  }
  const GenerateSpec& g = e.generate;
  o << "\n  // more residents (and buildings for them) on top of the ones above\n  generate {\n    residents: "
    << g.residents << "\n";
  if (!g.onePer.empty()) {
    o << "    one_per {";
    for (auto& [k, v] : g.onePer) o << " " << ident(k) << ": " << v;
    o << " }   // one building of each type per N generated residents\n";
  }
  if (!g.firstNames.empty()) o << "    first_names: " << list(g.firstNames) << "\n";
  if (!g.lastNames.empty()) o << "    last_names: " << list(g.lastNames) << "\n";
  if (!g.traits.empty()) o << "    traits: " << list(g.traits) << "\n";
  for (auto& r : g.routines)
    o << "    routine " << ident(r.name) << " { weight: " << fmtNum(r.weight) << "  works_at: " << ident(r.worksAt)
      << "  ages: " << r.minAge << ".." << r.maxAge << "  background: " << quote(r.learned)
      << "  currently: " << quote(r.currently) << " }\n";
  o << "  }\n}\n";
  return o.str();
}

// ---------------- behavior

BehaviorSpec parseBehavior(const std::string& source, const std::string& dir) {
  CNode root = loadTree(source, dir, ".behavior.sl", 0);
  if (root.kind != "behavior") throw sl::ParseError("line 1: expected 'behavior Name { ... }'");
  BehaviorSpec b;
  b.imports = root.imports;
  b.name = root.arg(0, "Behavior");
  // Town rules first, so group / resident rules start from them.
  for (auto& c : root.children)
    if (c.kind == "rules" && c.args.empty()) b.rules = readRules(c, SocialRules{});
  for (auto& c : root.children) {
    if (c.kind == "rules" && !c.args.empty()) {
      // rules student { } -- a group (a routine); rules "Name" { } -- one resident
      if (c.args[0].kind == CValue::Str) b.residentRules[c.arg(0)] = readRules(c, b.rules);
      else b.groupRules[c.arg(0)] = readRules(c, b.rules);
    } else if (c.kind == "routine" || c.kind == "everyday") {
      std::vector<RoutineEntry> entries;
      for (auto& l : c.lines) {
        RoutineEntry e;
        if (!parseRoutineLine(l, e))
          throw sl::ParseError("line " + std::to_string(l.empty() ? c.line : l[0].line) +
                               ": expected 'from..to \"activity\" at place' in " + c.kind);
        entries.push_back(e);
      }
      if (c.kind == "everyday") b.everyday = entries;
      else b.routines[c.arg(0)] = entries;
    } else if (c.kind == "free_time") {
      for (auto& l : c.lines) {
        FreeTimeSpec f;
        size_t i = 0;
        if (i < l.size()) f.activity = l[i++].text();
        if (i + 1 < l.size() && isWord(l[i], "at")) {
          f.place = l[i + 1].text();
          i += 2;
        }
        if (i < l.size() && l[i].kind == CValue::Num) f.weight = static_cast<float>(l[i++].n);
        if (i < l.size() && isWord(l[i], "social")) f.social = true;
        b.freeTime.push_back(f);
      }
    } else if (c.kind == "activity") {
      ActivitySpec a;
      for (size_t i = 0; i < c.args.size(); ++i) {
        if (isWord(c.args[i], "at") && i + 1 < c.args.size()) {
          a.place = c.args[i + 1].text();
          break;
        }
        if (isWord(c.args[i], "default")) continue;
        a.match.push_back(c.args[i].text());
      }
      for (auto& l : c.lines) {
        TaskStep s;
        if (l.size() > 0) s.desc = l[0].text();
        if (l.size() > 1) s.object = l[1].text();
        if (l.size() > 2) s.minutes = static_cast<int>(l[2].number(10));
        a.steps.push_back(s);
      }
      b.activities.push_back(a);
    } else if (c.kind == "emoji") {
      for (auto& l : c.lines)
        if (l.size() >= 2) b.emoji.push_back({l[0].text(), l[1].text()});
    } else if (c.kind == "mystery") {
      MysterySpec& m = b.mystery;
      m.on = true;
      m.name = c.arg(0, m.name);
      m.killer = c.str("killer", m.killer);
      m.victim = c.str("victim", m.victim);
      m.at = c.str("at", m.at);
      m.meeting = c.str("meeting", m.meeting);
      m.day = static_cast<int>(c.num("day", m.day));
      m.minute = static_cast<int>(c.num("time", m.minute / 60.0) * 60 + 0.5);
      m.rounds = static_cast<int>(c.num("rounds", m.rounds));
      if (const CValue* h = c.field("meeting_hours"); h && h->kind == CValue::Range) {
        m.meetingStart = static_cast<int>(h->n * 60);
        m.meetingEnd = static_cast<int>(h->n2 * 60);
      }
    } else if (c.kind == "importance") {
      for (auto& l : c.lines)
        if (l.size() >= 2) b.importance.push_back({l[0].text(), static_cast<float>(l[1].number(3))});
    }
  }
  return b;
}

std::string writeBehavior(const BehaviorSpec& b) {
  std::ostringstream o;
  o << "// How residents behave: social rules, daily routines, and how activities\n"
    << "// break into tasks. Scopes: rules { } is the whole town, rules <routine> { }\n"
    << "// a group, rules \"Name\" { } one resident -- the most specific one applies.\n"
    << "behavior " << ident(b.name) << " {\n";
  writeRules(o, "rules", b.rules);
  for (auto& [g, r] : b.groupRules) writeRules(o, "rules " + ident(g), r);
  for (auto& [n, r] : b.residentRules) writeRules(o, "rules " + quote(n), r);
  o << "\n  // daily routines: from..to (hours, or wake / sleep relative) \"activity\" at place\n";
  for (auto& [name, entries] : b.routines) {
    o << "  routine " << ident(name) << " {\n";
    for (auto& e : entries) writeRoutineLine(o, e);
    o << "  }\n";
  }
  o << "\n  // fills whatever a routine leaves open\n  everyday {\n";
  for (auto& e : b.everyday) writeRoutineLine(o, e);
  o << "  }\n\n  // still-open hours: \"activity\" at place weight [social]\n  free_time {\n";
  for (auto& f : b.freeTime)
    o << "    " << quote(f.activity) << " at " << ident(f.place) << " " << fmtNum(f.weight) << (f.social ? " social" : "")
      << "\n";
  o << "  }\n\n  // how an hour's activity breaks into tasks: \"task\" object minutes\n"
    << "  // (the first activity whose name matches wins; \"{activity}\" = the hour's activity)\n";
  for (auto& a : b.activities) {
    o << "  activity";
    if (a.match.empty()) o << " default";
    for (auto& m : a.match) o << " " << quote(m);
    if (!a.place.empty()) o << " at " << ident(a.place);
    o << " {\n";
    for (auto& s : a.steps) o << "    " << quote(s.desc) << " " << quote(s.object) << " " << s.minutes << "\n";
    o << "  }\n";
  }
  o << "\n  // emoji shown over residents, by keyword in what they're doing\n  emoji {\n";
  for (auto& [k, e] : b.emoji) o << "    " << quote(k) << " " << quote(e) << "\n";
  o << "  }\n\n  // how important a memory of an activity is (1-10), by keyword\n  importance {\n";
  for (auto& [k, v] : b.importance) o << "    " << quote(k) << " " << fmtNum(v) << "\n";
  o << "  }\n";
  if (const MysterySpec& m = b.mystery; m.on) {
    auto who = [](const std::string& s) { return s == "random" ? s : quote(s); };
    o << "\n  mystery " << quote(m.name) << " {\n"
      << "    killer: " << who(m.killer) << "\n    victim: " << who(m.victim) << "\n";
    if (!m.at.empty()) o << "    at: " << quote(m.at) << "\n";
    o << "    day: " << m.day << "\n    time: " << fmtNum(m.minute / 60.0) << "\n    meeting: " << quote(m.meeting)
      << "\n    meeting_hours: " << fmtNum(m.meetingStart / 60.0) << ".." << fmtNum(m.meetingEnd / 60.0)
      << "\n    rounds: " << m.rounds << "\n  }\n";
  }
  o << "}\n";
  return o.str();
}

std::string slFileKind(const std::string& source) {
  size_t i = 0;
  while (i < source.size()) {
    if (std::isspace(static_cast<unsigned char>(source[i]))) {
      ++i;
      continue;
    }
    if (source.compare(i, 2, "//") == 0) {
      i = source.find('\n', i);
      if (i == std::string::npos) return "";
      continue;
    }
    size_t j = i;
    while (j < source.size() && (std::isalnum(static_cast<unsigned char>(source[j])) || source[j] == '_')) ++j;
    if (source.compare(i, j - i, "import") == 0) {  // `import name` lines come first
      i = source.find('\n', j);
      if (i == std::string::npos) return "";
      continue;
    }
    return source.substr(i, j - i);
  }
  return "";
}

// ---------------- TownSpec

const SocialRules& TownSpec::rulesFor(const std::string& name, const std::string& routine) const {
  if (auto it = behavior.residentRules.find(name); it != behavior.residentRules.end()) return it->second;
  if (auto it = behavior.groupRules.find(routine); it != behavior.groupRules.end()) return it->second;
  return behavior.rules;
}

BuildingSpec TownSpec::styleFor(const BuildingSpec& building) const {
  BuildingSpec out;
  auto overlay = [&](const BuildingSpec& in) {
    if (in.floorColor) out.floorColor = in.floorColor;
    if (in.wallColor) out.wallColor = in.wallColor;
    if (in.size >= 0) out.size = in.size;
    if (in.bedrooms >= 0) out.bedrooms = in.bedrooms;
    if (in.customRooms) {
      out.customRooms = true;
      out.rooms = in.rooms;
    }
    if (in.hasBedroom) {
      out.hasBedroom = true;
      out.bedroom = in.bedroom;
    }
    if (!in.parkObjects.empty()) out.parkObjects = in.parkObjects;
  };
  overlay(env.style);
  if (auto it = env.types.find(building.kind); it != env.types.end()) overlay(it->second);
  overlay(building);
  out.name = building.name;
  out.kind = building.kind;
  return out;
}

BuildingSpec* TownSpec::findBuilding(const std::string& name) {
  for (auto& b : env.buildings)
    if (b.name == name) return &b;
  return nullptr;
}

TownSpec TownSpec::load(const std::string& envPath) {
  TownSpec t;
  t.envPath = envPath;
  std::string file = fs::path(envPath).filename().string();
  try {
    t.env = parseEnvironment(readText(envPath), fs::path(envPath).parent_path().string());
  } catch (const sl::ParseError& e) {
    throw sl::ParseError(file + ", " + e.what());
  }
  if (!t.env.behavior.empty()) {
    // Next to the environment file, else in lib/ (a library's own behaviors).
    fs::path dir = fs::path(envPath).parent_path();
    std::error_code ec;
    t.behaviorPath = (dir / t.env.behavior).string();
    if (!fs::exists(t.behaviorPath, ec) && fs::exists(dir / "lib" / t.env.behavior, ec))
      t.behaviorPath = (dir / "lib" / t.env.behavior).string();
    try {
      t.behavior = parseBehavior(readText(t.behaviorPath), fs::path(t.behaviorPath).parent_path().string());
    } catch (const sl::ParseError& e) {
      throw sl::ParseError(t.env.behavior + ", " + e.what());
    }
  }
  return t;
}

// A file that imports a library is written as its difference from it, so it
// keeps saying `import smallville` plus only what it changes.
namespace {
std::string withImports(const std::string& full, const std::vector<std::string>& imports, const std::string& path,
                        const std::string& kind, const std::string& ext) {
  if (imports.empty()) return full;
  std::string dir = fs::path(path).parent_path().string();
  CNode base = importsTree(imports, dir, kind, ext);
  CNode d = diffConfig(base, parseConfig(full));
  d.imports = imports;
  return writeConfig(d);
}
}  // namespace

bool TownSpec::save() const {
  bool ok = true;
  if (!envPath.empty()) {
    std::string text = withImports(writeEnvironment(env), env.imports, envPath, "environment", ".env.sl");
    std::ofstream out(envPath, std::ios::binary);
    out << text;
    ok = ok && static_cast<bool>(out);
  }
  if (!behaviorPath.empty()) {
    std::string text = withImports(writeBehavior(behavior), behavior.imports, behaviorPath, "behavior", ".behavior.sl");
    std::ofstream out(behaviorPath, std::ios::binary);
    out << text;
    ok = ok && static_cast<bool>(out);
  }
  return ok;
}

}  // namespace ville
