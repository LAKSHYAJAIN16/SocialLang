// A town is two SocialLang files:
//
//   environment OakHill { ... }     -- the world: buildings (type, size,
//     colors, rooms, furniture), building-type templates, residents (who they
//     are, their routine, home, work, sleep hours), relationships, events and
//     news, and how to generate extra residents and buildings for big towns.
//   behavior OakHill { ... }        -- how residents behave: social rules
//     (whole town < a group < one resident), daily routines, everyday meals,
//     free-time choices, how each activity breaks into timed tasks at named
//     objects, and the emoji / importance of activities.
//
// The environment names its behavior file; either can be swapped. Nothing
// about a town is hard-coded -- the Oak Hill files ship as examples.
//
// Scopes: the more specific setting wins.
//   building look:  style { } (every building) < type X { } < building "Y" { }
//   rules:          rules { } (whole town) < rules routine { } < rules "Name" { }
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ville {

struct Persona;

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
  float reflectThreshold = 150;    // summed importance before reflecting (default 150)
};

// ---------------- environment

struct RoomSpec {
  std::string name;
  std::vector<std::string> objects;
};

// A building's look and layout. At the `style` and `type` scopes, unset
// fields (0 colors, size -1, no rooms) defer to the broader scope.
struct BuildingSpec {
  std::string name;              // building "Name" (a building's key)
  std::string kind;              // home, cafe, pub, store, market, park, college, dorm, town hall, office
  unsigned floorColor = 0;       // 0xRRGGBB, 0 = inherit
  unsigned wallColor = 0;
  int size = -1;                 // -1 inherit, 0 small, 1 regular, 2 large
  int bedrooms = -1;             // homes: how many copies of the type's bedroom
  bool customRooms = false;      // rooms below replace the type's layout
  std::vector<RoomSpec> rooms;   // first = the hall the front door opens into
  RoomSpec bedroom;              // type templates: the room repeated `bedrooms` times
  bool hasBedroom = false;
  std::vector<std::string> parkObjects;  // parks: benches, gardens, ...
};

struct ResidentSpec {
  std::string name, innate, learned, currently, routine, home, bedroom, work;
  int age = 30, wakeHour = 7, sleepHour = 23;
  float sociability = 0.5f;
};

struct RelationshipSpec {
  std::string a, b;        // resident names
  float closeness = 3;     // how well they know each other (0-10)
  std::string note;        // "has a crush on", "is rivals with", ...
};

struct EventSpec {
  std::string name, text, host, at, activity;
  bool invite = false;     // guests who accept add it to their schedule
  int day = 0, startMin = 0, endMin = 0;
};

// Extra residents for towns bigger than the declared cast.
struct GenRoutine {
  std::string name, worksAt;  // worksAt: a building type, or "home"
  std::string learned, currently;
  float weight = 1;
  int minAge = 24, maxAge = 74;
};

struct GenerateSpec {
  int residents = 0;                         // generated on top of the declared ones
  std::map<std::string, int> onePer;         // building type -> one per N generated residents
  std::vector<std::string> firstNames, lastNames, traits;
  std::vector<GenRoutine> routines;
};

struct EnvironmentSpec {
  std::vector<std::string> imports;     // libraries this file starts from
  std::string name = "Town", behavior;  // behavior: file name, next to this one (or in lib/)
  int startHour = 6, days = 2;
  uint32_t seed = 1;
  std::string startDate = "Monday, February 13, 2023";
  BuildingSpec style;                          // every building
  std::map<std::string, BuildingSpec> types;   // type templates (rooms, colors, size)
  std::vector<BuildingSpec> buildings;         // laid out in this order
  std::vector<ResidentSpec> residents;
  std::vector<RelationshipSpec> relationships;
  std::vector<EventSpec> events;
  GenerateSpec generate;
};

// ---------------- behavior

// An hour in a routine: a number, or relative to the resident's wake /
// sleep time ("wake", "wake+1", "sleep").
struct HourRef {
  int base = 0;  // 0 absolute, 1 wake, 2 sleep
  int offset = 0;
  int at(int wake, int sleep) const { return (base == 1 ? wake : base == 2 ? sleep : 0) + offset; }
};

struct RoutineEntry {
  HourRef from, to;
  // One option is picked per day, weighted: "12..13 "lunch at the cafe" at
  // cafe or "lunch at home" at home".
  std::vector<std::pair<std::string, std::string>> options;  // (activity, place)
};

struct TaskStep {
  std::string desc, object;  // "{activity}" in desc stands for the hour's activity
  int minutes = 10;
};

struct ActivitySpec {
  std::vector<std::string> match;  // matches an hour whose activity contains any of these
  std::string place;               // optional: only at this place
  std::vector<TaskStep> steps;
};

struct FreeTimeSpec {
  std::string activity, place;
  float weight = 1;
  bool social = false;  // weighted up for sociable residents
};

struct BehaviorSpec {
  std::vector<std::string> imports;
  std::string name = "Behavior";
  SocialRules rules;
  std::map<std::string, SocialRules> groupRules;     // keyed by routine
  std::map<std::string, SocialRules> residentRules;  // keyed by resident name
  std::map<std::string, std::vector<RoutineEntry>> routines;
  std::vector<RoutineEntry> everyday;  // fills hours a routine leaves open (meals)
  std::vector<FreeTimeSpec> freeTime;  // fills what's still open
  std::vector<ActivitySpec> activities;
  std::vector<std::pair<std::string, std::string>> emoji;  // keyword -> emoji
  std::vector<std::pair<std::string, float>> importance;   // keyword -> 1-10
};

struct TownSpec {
  EnvironmentSpec env;
  BehaviorSpec behavior;
  std::string envPath, behaviorPath;

  const SocialRules& rulesFor(const std::string& name, const std::string& routine) const;
  // A building's effective look: style < type < building, field by field.
  BuildingSpec styleFor(const BuildingSpec& building) const;
  BuildingSpec* findBuilding(const std::string& name);

  // Loads an environment file and the behavior file it names (throws
  // sl::ParseError with the file and line on a syntax error).
  static TownSpec load(const std::string& envPath);
  // Writes both files back in canonical form (used by the editor's panels).
  bool save() const;
};

// `dir` is the file's folder, where `import name` looks for lib/name.env.sl
// (then name.env.sl); likewise .behavior.sl for behavior files.
EnvironmentSpec parseEnvironment(const std::string& source, const std::string& dir = "");
BehaviorSpec parseBehavior(const std::string& source, const std::string& dir = "");
// The file an import resolves to, or "" if there is none.
std::string findLibrary(const std::string& dir, const std::string& name, const std::string& ext);
std::string writeEnvironment(const EnvironmentSpec& e);
std::string writeBehavior(const BehaviorSpec& b);
// "environment" / "behavior" / "sim" / "" -- what kind of .sl a file is.
std::string slFileKind(const std::string& source);

}  // namespace ville
