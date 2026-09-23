// A town's customizations: how buildings look and are laid out, edits to
// residents, their relationships, and the social rules they follow. Stored as
// JSON next to the games (the_ville.town.json) and applied on top of the
// generated town, so an empty spec is the default Ville.
//
// Customizations have a scope, and the more specific one wins:
//   buildings: the whole town  <  every building of a type  <  one building
//   rules:     the whole town  <  a group (residents with one routine)  <  one resident
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ville {

// How residents interact. Read every step, so changes apply mid-run.
struct SocialRules {
  float chattiness = 1.0f;         // multiplies the chance two residents who meet start talking
  float chatCooldownHours = 3.0f;  // game-hours before the same pair talks again
  int visionRadius = 4;            // tiles a resident notices others within
  int attention = 3;               // new observations a resident records per step
  int conversationLength = 1;      // extra exchanges in a conversation (0 = brief, 3 = long)
  float newsEagerness = 4.0f;      // how much likelier someone with news is to start talking
  float inviteAcceptance = 1.0f;   // multiplies the chance an invitation is accepted
  bool strangersTalk = true;       // whether residents who've never met start conversations
  float reflectThreshold = 150;    // summed importance before reflecting (paper: 150)
};

struct RoomSpec {
  std::string name;
  std::vector<std::string> objects;
};

struct BuildingSpec {
  std::string name;              // display name ("" keeps the original)
  std::string kind;              // "home", "cafe", ... ("" keeps the original)
  unsigned floorColor = 0;       // 0xRRGGBB, 0 = inherit (the type's default)
  unsigned wallColor = 0;
  int size = -1;                 // -1 inherit, 0 small, 1 regular, 2 large
  bool customRooms = false;      // use `rooms` instead of the kind's layout
  std::vector<RoomSpec> rooms;   // first = the hall the front door opens into
};

struct ResidentSpec {
  std::string innate, learned, currently, archetype, home, work;  // "" = unchanged
  int wakeHour = -1, sleepHour = -1;
  float sociability = -1;
};

struct RelationshipSpec {
  std::string a, b;        // resident names
  float closeness = 3;     // how well they know each other (0-10)
  std::string note;        // "has a crush on", "rivals", "old friends", ...
};

struct TownSpec {
  SocialRules rules;                                     // the whole town
  std::map<std::string, SocialRules> groupRules;         // keyed by daily routine ("student", ...)
  std::map<std::string, SocialRules> residentRules;      // keyed by resident name
  BuildingSpec townStyle;                                // every building
  std::map<std::string, BuildingSpec> typeStyles;        // keyed by type ("home", "cafe", ...)
  std::map<std::string, BuildingSpec> buildings;  // keyed by the building's original name
  std::map<std::string, ResidentSpec> residents;  // keyed by name
  std::vector<RelationshipSpec> relationships;

  // The rules one resident follows: theirs, else their group's, else the town's.
  const SocialRules& rulesFor(const std::string& name, const std::string& routine) const;
  // A building's effective look: town < type < building, field by field.
  // `kind` is the building's type after its own override.
  BuildingSpec styleFor(const std::string& key, const std::string& kind) const;

  static TownSpec load(const std::string& path);
  bool save(const std::string& path) const;
};

}  // namespace ville
