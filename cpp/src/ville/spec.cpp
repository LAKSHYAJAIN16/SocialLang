#include "ville/spec.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace ville {

using json = nlohmann::json;

namespace {

void overlay(BuildingSpec& out, const BuildingSpec& in) {
  if (in.floorColor) out.floorColor = in.floorColor;
  if (in.wallColor) out.wallColor = in.wallColor;
  if (in.size >= 0) out.size = in.size;
  if (in.customRooms) {
    out.customRooms = true;
    out.rooms = in.rooms;
  }
}

SocialRules rulesFrom(const json& r) {
  SocialRules s, d;
  s.chattiness = r.value("chattiness", d.chattiness);
  s.chatCooldownHours = r.value("chatCooldownHours", d.chatCooldownHours);
  s.visionRadius = r.value("visionRadius", d.visionRadius);
  s.attention = r.value("attention", d.attention);
  s.conversationLength = r.value("conversationLength", d.conversationLength);
  s.newsEagerness = r.value("newsEagerness", d.newsEagerness);
  s.inviteAcceptance = r.value("inviteAcceptance", d.inviteAcceptance);
  s.strangersTalk = r.value("strangersTalk", d.strangersTalk);
  s.reflectThreshold = r.value("reflectThreshold", d.reflectThreshold);
  return s;
}

json rulesTo(const SocialRules& r) {
  return {{"chattiness", r.chattiness},         {"chatCooldownHours", r.chatCooldownHours},
          {"visionRadius", r.visionRadius},     {"attention", r.attention},
          {"conversationLength", r.conversationLength}, {"newsEagerness", r.newsEagerness},
          {"inviteAcceptance", r.inviteAcceptance}, {"strangersTalk", r.strangersTalk},
          {"reflectThreshold", r.reflectThreshold}};
}

BuildingSpec buildingFrom(const json& b) {
  BuildingSpec bs;
  bs.name = b.value("name", "");
  bs.kind = b.value("kind", "");
  bs.floorColor = b.value("floorColor", 0u);
  bs.wallColor = b.value("wallColor", 0u);
  bs.size = b.value("size", -1);
  bs.customRooms = b.value("customRooms", false);
  for (auto& r : b.value("rooms", json::array())) {
    RoomSpec rs;
    rs.name = r.value("name", "room");
    for (auto& o : r.value("objects", json::array())) rs.objects.push_back(o.get<std::string>());
    bs.rooms.push_back(std::move(rs));
  }
  return bs;
}

json buildingTo(const BuildingSpec& b) {
  json jb = {{"name", b.name}, {"kind", b.kind}, {"floorColor", b.floorColor}, {"wallColor", b.wallColor},
             {"size", b.size}, {"customRooms", b.customRooms}, {"rooms", json::array()}};
  for (auto& r : b.rooms) jb["rooms"].push_back({{"name", r.name}, {"objects", r.objects}});
  return jb;
}

}  // namespace

const SocialRules& TownSpec::rulesFor(const std::string& name, const std::string& routine) const {
  if (auto it = residentRules.find(name); it != residentRules.end()) return it->second;
  if (auto it = groupRules.find(routine); it != groupRules.end()) return it->second;
  return rules;
}

BuildingSpec TownSpec::styleFor(const std::string& key, const std::string& kind) const {
  BuildingSpec out;
  overlay(out, townStyle);
  if (auto it = typeStyles.find(kind); it != typeStyles.end()) overlay(out, it->second);
  if (auto it = buildings.find(key); it != buildings.end()) {
    overlay(out, it->second);
    out.name = it->second.name;
    out.kind = it->second.kind;
  }
  return out;
}

TownSpec TownSpec::load(const std::string& path) {
  TownSpec s;
  std::ifstream in(path);
  if (!in) return s;
  try {
    json j = json::parse(in);
    if (j.contains("rules")) s.rules = rulesFrom(j["rules"]);
    json groups = j.value("groupRules", json::object());
    for (auto& [key, r] : groups.items()) s.groupRules[key] = rulesFrom(r);
    json people = j.value("residentRules", json::object());
    for (auto& [key, r] : people.items()) s.residentRules[key] = rulesFrom(r);
    if (j.contains("townStyle")) s.townStyle = buildingFrom(j["townStyle"]);
    json types = j.value("typeStyles", json::object());
    for (auto& [key, b] : types.items()) s.typeStyles[key] = buildingFrom(b);
    // Bind to named objects: ranging over .items() of a temporary would
    // dangle (the temporary dies before the loop body runs).
    json buildings = j.value("buildings", json::object());
    for (auto& [key, b] : buildings.items()) s.buildings[key] = buildingFrom(b);
    json residents = j.value("residents", json::object());
    for (auto& [key, r] : residents.items()) {
      ResidentSpec rs;
      rs.innate = r.value("innate", "");
      rs.learned = r.value("learned", "");
      rs.currently = r.value("currently", "");
      rs.archetype = r.value("archetype", "");
      rs.home = r.value("home", "");
      rs.work = r.value("work", "");
      rs.wakeHour = r.value("wakeHour", -1);
      rs.sleepHour = r.value("sleepHour", -1);
      rs.sociability = r.value("sociability", -1.0f);
      s.residents[key] = std::move(rs);
    }
    for (auto& r : j.value("relationships", json::array())) {
      RelationshipSpec rs;
      rs.a = r.value("a", "");
      rs.b = r.value("b", "");
      rs.closeness = r.value("closeness", 3.0f);
      rs.note = r.value("note", "");
      if (!rs.a.empty() && !rs.b.empty()) s.relationships.push_back(std::move(rs));
    }
  } catch (...) {
    return TownSpec{};
  }
  return s;
}

bool TownSpec::save(const std::string& path) const {
  json j;
  j["rules"] = rulesTo(rules);
  j["groupRules"] = json::object();
  for (auto& [key, r] : groupRules) j["groupRules"][key] = rulesTo(r);
  j["residentRules"] = json::object();
  for (auto& [key, r] : residentRules) j["residentRules"][key] = rulesTo(r);
  j["townStyle"] = buildingTo(townStyle);
  j["typeStyles"] = json::object();
  for (auto& [key, b] : typeStyles) j["typeStyles"][key] = buildingTo(b);
  j["buildings"] = json::object();
  for (auto& [key, b] : buildings) j["buildings"][key] = buildingTo(b);
  j["residents"] = json::object();
  for (auto& [key, r] : residents)
    j["residents"][key] = {{"innate", r.innate},       {"learned", r.learned},     {"currently", r.currently},
                           {"archetype", r.archetype}, {"home", r.home},           {"work", r.work},
                           {"wakeHour", r.wakeHour},   {"sleepHour", r.sleepHour}, {"sociability", r.sociability}};
  j["relationships"] = json::array();
  for (auto& r : relationships)
    j["relationships"].push_back({{"a", r.a}, {"b", r.b}, {"closeness", r.closeness}, {"note", r.note}});
  std::ofstream out(path);
  if (!out) return false;
  out << j.dump(2);
  return static_cast<bool>(out);
}

}  // namespace ville
