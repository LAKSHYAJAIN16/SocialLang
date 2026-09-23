// Customizing the town: how each building looks and is laid out, who the
// residents are, their relationships, and the social rules they follow. Edits
// go into the town spec (games/the_ville.town.json). Building and resident
// edits rebuild the town from the start; social rules apply live, mid-run.
#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>

#include "app/editor.h"
#include "ville/world.h"

namespace app {

namespace {

const char* kKinds[] = {"home", "cafe", "pub", "store", "market", "park", "college", "dorm", "town hall", "office"};
const char* kSizes[] = {"small", "regular", "large"};
const char* kArchetypes[] = {"cafe_owner", "student", "professor",   "pharmacist", "shopkeeper",
                             "bartender",  "politician", "comedian", "photographer", "artist",
                             "writer",     "engineer",  "lawyer",    "mathematician", "retiree"};
const char* kNotes[] = {"is friends with", "is family with", "is married to", "has a crush on", "is rivals with",
                        "works with", "is neighbors with"};

std::string joinObjects(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
  return s;
}

std::vector<std::string> splitObjects(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ',')) {
    size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
    if (a != std::string::npos) out.push_back(part.substr(a, b - a + 1));
  }
  return out;
}

ImVec4 toColor(unsigned rgb) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}
unsigned fromColor(const float c[3]) {
  auto b = [](float v) { return static_cast<unsigned>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return (b(c[0]) << 16) | (b(c[1]) << 8) | b(c[2]);
}

// Labeled row: label in the left column, widget filling the right.
void label(const char* text) {
  float labelW = std::max(96.0f, ImGui::GetContentRegionAvail().x * 0.34f);
  float x0 = ImGui::GetCursorPosX();
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", text);
  ImGui::SameLine(x0 + labelW);
  ImGui::SetNextItemWidth(-1);
}

// Shows `shown`, writes every keystroke into `target` (the town spec), and
// returns true when the edit is finished -- the moment to rebuild the town.
bool editText(const char* id, std::string shown, std::string& target, bool multiline = false) {
  bool edited = multiline ? ImGui::InputTextMultiline(id, &shown, ImVec2(-1, ImGui::GetTextLineHeight() * 3.4f))
                          : ImGui::InputText(id, &shown);
  if (edited) target = shown;
  return ImGui::IsItemDeactivatedAfterEdit();
}

}  // namespace

// Rebuild the town with the current spec (back to the start), and save it.
void applyTownEdits(EditorState& ed, const char* what) {
  if (!ed.townSpecPath.empty()) ed.townSpec.save(ed.townSpecPath);
  Selection keep = ed.sel;
  bool wasPlaying = ed.playMode;
  float zoom = ed.zoom;
  ImVec2 cam = ed.camCenter;
  loadVille(ed, ed.villePopulation);
  ed.sel = keep;  // building / resident indices are stable across rebuilds
  ed.fitView = false;
  ed.zoom = zoom;
  ed.camCenter = cam;
  notify(ed, std::string(what) + (wasPlaying ? " -- town restarted from Feb 13, 6:00 am" : ""));
}

void inspectBuilding(EditorState& ed, int sector) {
  if (!ed.info.villeWorld || sector < 0 || sector >= (int)ed.info.villeWorld->sectors.size()) return;
  const ville::World& w = *ed.info.villeWorld;
  const ville::Sector& s = w.sectors[sector];
  std::string kindName = ville::sectorKindName(s.kind);
  bool changed = false;

  ImGui::Spacing();
  ImGui::PushFont(ed.fonts.bold, ImGui::GetStyle().FontSizeBase * 1.15f);
  ImGui::TextUnformatted(s.name.c_str());
  ImGui::PopFont();
  ImGui::TextDisabled("%s  |  %zu rooms  |  edits rebuild the town", kindName.c_str(), s.arenas.size());
  ImGui::Separator();

  // Scope: how widely an edit applies. More specific wins.
  ImGui::TextDisabled("Apply changes to");
  std::string typeLabel = "Every " + kindName;
  const char* scopes[] = {"This building", typeLabel.c_str(), "Every building"};
  for (int k = 0; k < 3; ++k) {
    if (k) ImGui::SameLine(0, 2);
    bool on = ed.buildingScope == k;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (ImGui::Button(scopes[k], ImVec2((ImGui::GetContentRegionAvail().x - (2 - k) * 2) / (3 - k), 0))) ed.buildingScope = k;
    ImGui::PopStyleColor();
  }
  ville::BuildingSpec& b = ed.buildingScope == 0 ? ed.townSpec.buildings[s.key]
                           : ed.buildingScope == 1 ? ed.townSpec.typeStyles[kindName]
                                                   : ed.townSpec.townStyle;
  ville::BuildingSpec eff = ed.townSpec.styleFor(s.key, kindName);  // what this building shows now
  const char* from = ed.buildingScope == 0 ? "" : ed.buildingScope == 1 ? "  (all of this type)" : "  (whole town)";
  ImGui::Spacing();

  ImGui::PushFont(ed.fonts.bold, 0.0f);
  bool look = ImGui::CollapsingHeader("Look", ImGuiTreeNodeFlags_DefaultOpen);
  ImGui::PopFont();
  if (look) {
    if (ed.buildingScope == 0) {
      label("Name");
      if (editText("##name", b.name.empty() ? s.name : b.name, b.name)) {
        if (b.name == s.key) b.name.clear();
        changed = true;
      }
      int kind = static_cast<int>(s.kind);
      label("Type");
      if (ImGui::Combo("##kind", &kind, kKinds, IM_ARRAYSIZE(kKinds))) {
        b.kind = kKinds[kind];
        b.customRooms = false;  // a new type gets that type's rooms
        changed = true;
      }
    }
    // Size: "inherit" defers to the broader scope.
    static const char* kSizeItems[] = {"inherit", "small", "regular", "large"};
    int size = b.size + 1;
    label("Size");
    if (ImGui::Combo("##size", &size, kSizeItems, IM_ARRAYSIZE(kSizeItems))) {
      b.size = size - 1;
      changed = true;
    }
    float floor[3], wall[3];
    ImVec4 fc = toColor(b.floorColor ? b.floorColor : eff.floorColor ? eff.floorColor : 0xC49A6C);
    ImVec4 wc = toColor(b.wallColor ? b.wallColor : eff.wallColor ? eff.wallColor : 0x3A3A44);
    floor[0] = fc.x, floor[1] = fc.y, floor[2] = fc.z;
    wall[0] = wc.x, wall[1] = wc.y, wall[2] = wc.z;
    static bool colorPending = false;
    label("Floor color");
    if (ImGui::ColorEdit3("##floor", floor, ImGuiColorEditFlags_NoInputs)) b.floorColor = fromColor(floor), colorPending = true;
    ImGui::SameLine();
    if (b.floorColor) {
      if (ImGui::SmallButton("inherit##f")) b.floorColor = 0, changed = true;
    } else {
      ImGui::TextDisabled("inherited");
    }
    label("Wall color");
    if (ImGui::ColorEdit3("##wall", wall, ImGuiColorEditFlags_NoInputs)) b.wallColor = fromColor(wall), colorPending = true;
    ImGui::SameLine();
    if (b.wallColor) {
      if (ImGui::SmallButton("inherit##w")) b.wallColor = 0, changed = true;
    } else {
      ImGui::TextDisabled("inherited");
    }
    if (colorPending && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      colorPending = false;
      changed = true;
    }
    ImGui::Spacing();
  }

  if (s.kind != ville::SectorKind::Park) {
    ImGui::PushFont(ed.fonts.bold, 0.0f);
    bool roomsOpen = ImGui::CollapsingHeader("Rooms & furniture", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopFont();
    if (roomsOpen) {
      if (!b.customRooms) {
        // Show the current layout; editing starts from it.
        for (int a : s.arenas) {
          ImGui::TextUnformatted(w.arenas[a].name.c_str());
          std::vector<std::string> objs;
          for (int o : w.arenas[a].objects) objs.push_back(w.objects[o].name);
          ImGui::SameLine();
          ImGui::TextDisabled("%s", joinObjects(objs).c_str());
        }
        std::string btn = std::string("Customize rooms") + from;
        if (ImGui::Button(btn.c_str(), ImVec2(-1, 0))) {
          b.customRooms = true;
          b.rooms.clear();
          for (int a : s.arenas) {
            ville::RoomSpec r;
            r.name = w.arenas[a].name;
            for (int o : w.arenas[a].objects) r.objects.push_back(w.objects[o].name);
            b.rooms.push_back(r);
          }
        }
      } else {
        ImGui::TextDisabled("First room is the hall the front door opens into. Objects: comma-separated.");
        int removeAt = -1;
        for (size_t k = 0; k < b.rooms.size(); ++k) {
          ImGui::PushID(static_cast<int>(k));
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.34f);
          if (editText("##room", b.rooms[k].name, b.rooms[k].name)) changed = true;
          ImGui::SameLine();
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28);
          std::string objs = joinObjects(b.rooms[k].objects);
          if (ImGui::InputText("##objs", &objs)) b.rooms[k].objects = splitObjects(objs);
          if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
          ImGui::SameLine();
          ImGui::BeginDisabled(b.rooms.size() <= 1);
          if (ImGui::SmallButton("x")) removeAt = static_cast<int>(k);
          ImGui::EndDisabled();
          ImGui::SetItemTooltip("Remove this room");
          ImGui::PopID();
        }
        if (removeAt >= 0) {
          b.rooms.erase(b.rooms.begin() + removeAt);
          changed = true;
        }
        if (ImGui::Button("+ Add room")) {
          b.rooms.push_back({"new room", {"chair", "table"}});
          changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Use standard layout")) {
          b.customRooms = false;
          b.rooms.clear();
          changed = true;
        }
        ImGui::TextDisabled("Residents look for objects by name: \"bed\" to sleep, \"stove\" to cook,");
        ImGui::TextDisabled("\"table\" to eat, \"desk\" to work, \"couch\" / \"tv\" to relax.");
      }
      ImGui::Spacing();
    }
  }

  if (ed.buildingScope == 0) {
    ImGui::PushFont(ed.fonts.bold, 0.0f);
    bool who = ImGui::CollapsingHeader("Residents", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopFont();
    if (who) {
      int n = 0;
      for (size_t i = 0; i < ed.info.agents.size(); ++i)
        if (ed.info.agents[i].team == s.name || ed.info.agents[i].team == s.key) {
          ++n;
          ImGui::PushID(static_cast<int>(i));
          if (ImGui::TextLink(ed.info.agents[i].seat.c_str())) ed.sel = {SelKind::Agent, static_cast<int>(i)};
          ImGui::PopID();
        }
      if (n == 0) ImGui::TextDisabled("Nobody lives here.");
      ImGui::Spacing();
    }
  }

  std::string reset = ed.buildingScope == 0 ? "Reset this building"
                      : ed.buildingScope == 1 ? "Reset every " + kindName
                                              : "Reset every building's style";
  if (ImGui::Button(reset.c_str(), ImVec2(-1, 0))) {
    if (ed.buildingScope == 0) ed.townSpec.buildings.erase(s.key);
    else if (ed.buildingScope == 1) ed.townSpec.typeStyles.erase(kindName);
    else ed.townSpec.townStyle = ville::BuildingSpec{};
    applyTownEdits(ed, "Style reset");
    return;
  }
  // Drop entries that customize nothing.
  auto isDefault = [](const ville::BuildingSpec& x) {
    return x.name.empty() && x.kind.empty() && !x.floorColor && !x.wallColor && x.size < 0 && !x.customRooms;
  };
  if (ed.buildingScope == 0 && isDefault(b)) ed.townSpec.buildings.erase(s.key);
  else if (ed.buildingScope == 1 && isDefault(b)) ed.townSpec.typeStyles.erase(kindName);
  if (changed) applyTownEdits(ed, ed.buildingScope == 0 ? "Building updated" : ed.buildingScope == 1 ? "Every building of this type updated" : "Every building updated");
}

// A resident's editable persona and relationships, below their live state.
void editResident(EditorState& ed, int i) {
  if (!ed.info.villeWorld || !ed.info.details || i >= (int)ed.info.details->size()) return;
  const ville::World& w = *ed.info.villeWorld;
  const AgentDetail& d = (*ed.info.details)[i];
  const std::string& name = ed.info.agents[i].seat;
  bool existed = ed.townSpec.residents.count(name) > 0;
  ville::ResidentSpec& r = ed.townSpec.residents[name];
  bool changed = false;

  if (ImGui::Button(("Interaction rules for " + name.substr(0, name.find(' '))).c_str(), ImVec2(-1, 0))) {
    ed.showRules = true;
    ed.rulesScope = 2;
    ed.rulesResident = name;
    ImGui::SetWindowFocus("Rules");
  }
  ImGui::PushFont(ed.fonts.bold, 0.0f);
  bool open = ImGui::CollapsingHeader("Edit resident");
  ImGui::PopFont();
  if (open) {
    ImGui::TextDisabled("Changes rebuild the town from the start.");
    label("Traits");
    if (editText("##innate", r.innate.empty() ? d.innate : r.innate, r.innate)) changed = true;
    label("Background");
    if (editText("##learned", r.learned.empty() ? d.learned : r.learned, r.learned, true)) changed = true;
    std::string currently = r.currently;
    if (currently.empty()) {
      currently = d.currently;
      std::string prefix = name + " is ";
      if (currently.rfind(prefix, 0) == 0) currently = currently.substr(prefix.size());
      if (!currently.empty() && currently.back() == '.') currently.pop_back();
    }
    label("Currently");
    if (editText("##currently", currently, r.currently, true)) changed = true;

    std::string role = ed.info.agents[i].role;
    std::replace(role.begin(), role.end(), ' ', '_');
    int arch = 0;
    for (int k = 0; k < IM_ARRAYSIZE(kArchetypes); ++k)
      if (role == kArchetypes[k] || r.archetype == kArchetypes[k]) arch = k;
    label("Daily routine");
    if (ImGui::Combo("##arch", &arch, kArchetypes, IM_ARRAYSIZE(kArchetypes))) r.archetype = kArchetypes[arch], changed = true;

    // Home and workplace pickers over the town's buildings.
    std::vector<const char*> homes, works = {"home (works from home)"};
    std::vector<std::string> homeKeys, workKeys = {"home"};
    for (auto& s : w.sectors) {
      if (s.kind == ville::SectorKind::Home || s.kind == ville::SectorKind::Dorm) {
        homes.push_back(s.name.c_str());
        homeKeys.push_back(s.key);
      } else {
        works.push_back(s.name.c_str());
        workKeys.push_back(s.key);
      }
    }
    int home = 0, work = 0;
    for (size_t k = 0; k < homeKeys.size(); ++k)
      if (w.sectors[w.findSector(homeKeys[k])].name == d.homeName) home = static_cast<int>(k);
    for (size_t k = 1; k < workKeys.size(); ++k)
      if (w.sectors[w.findSector(workKeys[k])].name == d.workName) work = static_cast<int>(k);
    label("Lives at");
    if (ImGui::Combo("##home", &home, homes.data(), static_cast<int>(homes.size()))) r.home = homeKeys[home], changed = true;
    label("Works at");
    if (ImGui::Combo("##work", &work, works.data(), static_cast<int>(works.size()))) r.work = workKeys[work], changed = true;

    int wake = r.wakeHour >= 0 ? r.wakeHour : 7, sleep = r.sleepHour >= 0 ? r.sleepHour : 23;
    label("Wakes up");
    if (ImGui::SliderInt("##wake", &wake, 4, 11, "%d am")) r.wakeHour = wake;
    if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    label("Goes to bed");
    if (ImGui::SliderInt("##sleep", &sleep, 20, 24, "%d:00")) r.sleepHour = sleep;
    if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    float soc = r.sociability >= 0 ? r.sociability : 0.5f;
    label("Sociability");
    if (ImGui::SliderFloat("##soc", &soc, 0.0f, 1.0f, soc < 0.35f ? "reserved (%.2f)" : soc < 0.7f ? "average (%.2f)" : "outgoing (%.2f)"))
      r.sociability = soc;
    if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    if (existed && ImGui::Button("Reset resident", ImVec2(-1, 0))) {
      ed.townSpec.residents.erase(name);
      applyTownEdits(ed, "Resident reset");
      return;
    }
    ImGui::Spacing();
  }
  bool isDefault = r.innate.empty() && r.learned.empty() && r.currently.empty() && r.archetype.empty() && r.home.empty() &&
                   r.work.empty() && r.wakeHour < 0 && r.sleepHour < 0 && r.sociability < 0;
  if (isDefault) ed.townSpec.residents.erase(name);  // r is dangling after this
  if (changed) {
    applyTownEdits(ed, "Resident updated");
    return;
  }

  // Relationships: who they already know, and how.
  ImGui::PushFont(ed.fonts.bold, 0.0f);
  bool relOpen = ImGui::CollapsingHeader("Edit relationships");
  ImGui::PopFont();
  if (!relOpen) return;
  ImGui::TextDisabled("Residents start out knowing each other; closeness raises how often they talk,");
  ImGui::TextDisabled("and the note shapes how they greet each other (crush, rivals, friends...).");
  bool relChanged = false;
  int removeAt = -1;
  for (size_t k = 0; k < ed.townSpec.relationships.size(); ++k) {
    auto& rel = ed.townSpec.relationships[k];
    if (rel.a != name && rel.b != name) continue;
    ImGui::PushID(static_cast<int>(k));
    bool mine = rel.a == name;
    ImGui::TextUnformatted(mine ? name.substr(0, name.find(' ')).c_str() : rel.a.c_str());
    ImGui::SameLine();
    int note = 0;
    for (int n = 0; n < IM_ARRAYSIZE(kNotes); ++n)
      if (rel.note == kNotes[n]) note = n;
    ImGui::SetNextItemWidth(130);
    if (ImGui::Combo("##note", &note, kNotes, IM_ARRAYSIZE(kNotes))) rel.note = kNotes[note], relChanged = true;
    ImGui::SameLine();
    ImGui::TextUnformatted(mine ? rel.b.c_str() : name.substr(0, name.find(' ')).c_str());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28);
    ImGui::SliderFloat("##close", &rel.closeness, 0.0f, 10.0f, "closeness %.0f");
    if (ImGui::IsItemDeactivatedAfterEdit()) relChanged = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) removeAt = static_cast<int>(k);
    ImGui::PopID();
  }
  if (removeAt >= 0) {
    ed.townSpec.relationships.erase(ed.townSpec.relationships.begin() + removeAt);
    relChanged = true;
  }
  static int pick = 0;
  static int pickNote = 0;
  std::vector<const char*> others;
  std::vector<int> otherIdx;
  for (size_t k = 0; k < ed.info.agents.size() && k < 400; ++k)
    if ((int)k != i) {
      others.push_back(ed.info.agents[k].seat.c_str());
      otherIdx.push_back(static_cast<int>(k));
    }
  pick = std::clamp(pick, 0, std::max(0, (int)others.size() - 1));
  ImGui::SetNextItemWidth(130);
  ImGui::Combo("##newnote", &pickNote, kNotes, IM_ARRAYSIZE(kNotes));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60);
  if (!others.empty()) ImGui::Combo("##newwho", &pick, others.data(), static_cast<int>(others.size()));
  ImGui::SameLine();
  if (ImGui::Button("Add") && !others.empty()) {
    ed.townSpec.relationships.push_back({name, ed.info.agents[otherIdx[pick]].seat, 5, kNotes[pickNote]});
    relChanged = true;
  }
  if (relChanged) applyTownEdits(ed, "Relationships updated");
}

// Rules panel: how residents interact, applied live. Scope: the whole town,
// a group (everyone with one daily routine), or one resident -- the most
// specific rules that exist for a resident are the ones they follow.
void drawRules(EditorState& ed) {
  if (!ed.showRules) return;
  if (!beginPanel("Rules", &ed.showRules)) {
    ImGui::End();
    return;
  }
  if (!ed.info.isVille) {
    ImGui::TextDisabled("Social rules apply to The Ville. Load it from Scenarios.");
    ImGui::End();
    return;
  }
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("How residents interact. Changes apply immediately, even mid-run, and save with the town.");
  ImGui::PopTextWrapPos();

  // Scope picker
  const char* scopes[] = {"Whole town", "A group", "One resident"};
  for (int k = 0; k < 3; ++k) {
    if (k) ImGui::SameLine(0, 2);
    bool on = ed.rulesScope == k;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (ImGui::Button(scopes[k], ImVec2((ImGui::GetContentRegionAvail().x - (2 - k) * 2) / (3 - k), 0))) ed.rulesScope = k;
    ImGui::PopStyleColor();
  }
  // Groups are the daily routines present in this town.
  std::vector<std::string> groups;
  for (auto& a : ed.info.agents) {
    std::string g = a.role;
    std::replace(g.begin(), g.end(), ' ', '_');
    if (std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
  }
  std::sort(groups.begin(), groups.end());
  if (ed.rulesGroup.empty() && !groups.empty()) ed.rulesGroup = groups[0];
  if (ed.rulesResident.empty() && !ed.info.agents.empty()) ed.rulesResident = ed.info.agents[0].seat;
  if (ed.sel.kind == SelKind::Agent && ed.sel.index >= 0 && ed.sel.index < (int)ed.info.agents.size() && ed.rulesScope == 2)
    ed.rulesResident = ed.info.agents[ed.sel.index].seat;

  ville::SocialRules* target = &ed.townSpec.rules;
  std::map<std::string, ville::SocialRules>* table = nullptr;
  std::string key, who;
  if (ed.rulesScope == 1) {
    label("Group");
    if (ImGui::BeginCombo("##group", ed.rulesGroup.c_str())) {
      for (auto& g : groups)
        if (ImGui::Selectable(g.c_str(), g == ed.rulesGroup)) ed.rulesGroup = g;
      ImGui::EndCombo();
    }
    table = &ed.townSpec.groupRules;
    key = ed.rulesGroup;
    who = "every " + key;
  } else if (ed.rulesScope == 2) {
    label("Resident");
    if (ImGui::BeginCombo("##resident", ed.rulesResident.c_str())) {
      for (size_t i = 0; i < ed.info.agents.size() && i < 2000; ++i)
        if (ImGui::Selectable(ed.info.agents[i].seat.c_str(), ed.info.agents[i].seat == ed.rulesResident)) {
          ed.rulesResident = ed.info.agents[i].seat;
          ed.sel = {SelKind::Agent, static_cast<int>(i)};
        }
      ImGui::EndCombo();
    }
    table = &ed.townSpec.residentRules;
    key = ed.rulesResident;
    who = key;
  }
  bool changed = false;
  if (table) {
    auto it = table->find(key);
    if (it == table->end()) {
      ImGui::Spacing();
      ImGui::PushTextWrapPos(0);
      ImGui::TextDisabled("%s follows %s rules.", who.c_str(),
                          ed.rulesScope == 2 && ed.townSpec.groupRules.count([&] {
                            int idx = seatIndex(ed, key);
                            std::string g = idx >= 0 ? ed.info.agents[idx].role : "";
                            std::replace(g.begin(), g.end(), ' ', '_');
                            return g;
                          }())
                              ? "their group's"
                              : "the town's");
      ImGui::PopTextWrapPos();
      if (ImGui::Button(("Give " + who + " their own rules").c_str(), ImVec2(-1, 0))) {
        // Start from whatever they follow now.
        ville::SocialRules base = ed.townSpec.rules;
        if (ed.rulesScope == 2) {
          int idx = seatIndex(ed, key);
          std::string g = idx >= 0 ? ed.info.agents[idx].role : "";
          std::replace(g.begin(), g.end(), ' ', '_');
          base = ed.townSpec.rulesFor(key, g);
        }
        (*table)[key] = base;
        changed = true;
      }
      if (changed) {
        ed.sim.setRules(ed.townSpec);
        if (!ed.townSpecPath.empty()) ed.townSpec.save(ed.townSpecPath);
      }
      ImGui::End();
      return;
    }
    target = &it->second;
    if (ImGui::Button(("Remove " + who + "'s own rules").c_str(), ImVec2(-1, 0))) {
      table->erase(it);
      ed.sim.setRules(ed.townSpec);
      if (!ed.townSpecPath.empty()) ed.townSpec.save(ed.townSpecPath);
      ImGui::End();
      return;
    }
  }

  ville::SocialRules& r = *target;
  ville::SocialRules before = r;
  sectionHeader(ed, "Conversation");
  label("Chattiness");
  ImGui::SliderFloat("##chat", &r.chattiness, 0.0f, 5.0f, "%.2fx");
  ImGui::SetItemTooltip("How likely they are to start talking with someone they meet");
  label("Time between chats");
  ImGui::SliderFloat("##cool", &r.chatCooldownHours, 0.25f, 12.0f, "%.1f game-hours");
  ImGui::SetItemTooltip("How long before they talk to the same person again");
  label("Conversation length");
  static const char* kLen[] = {"brief", "normal", "chatty", "long"};
  ImGui::SliderInt("##len", &r.conversationLength, 0, 3, kLen[std::clamp(r.conversationLength, 0, 3)]);
  label("Talks to strangers");
  ImGui::Checkbox("##strangers", &r.strangersTalk);
  ImGui::SameLine();
  ImGui::TextDisabled(r.strangersTalk ? "yes" : "only to people they already know");
  sectionHeader(ed, "News and invitations");
  label("News eagerness");
  ImGui::SliderFloat("##news", &r.newsEagerness, 1.0f, 10.0f, "%.1fx");
  ImGui::SetItemTooltip("How much likelier they are to start talking when they have news the other hasn't heard");
  label("Invite acceptance");
  ImGui::SliderFloat("##invite", &r.inviteAcceptance, 0.0f, 2.0f, "%.2fx");
  ImGui::SetItemTooltip("Multiplies the chance they accept an invitation (Isabella's party)");
  sectionHeader(ed, "Perception and memory");
  label("Vision radius");
  ImGui::SliderInt("##vision", &r.visionRadius, 1, 12, "%d tiles");
  label("Attention");
  ImGui::SliderInt("##att", &r.attention, 1, 10, "%d observations / step");
  label("Reflect after");
  ImGui::SliderFloat("##refl", &r.reflectThreshold, 20.0f, 500.0f, "%.0f importance");
  ImGui::SetItemTooltip("Summed importance of new memories before reflecting (the paper uses 150)");
  ImGui::Spacing();
  if (ed.rulesScope == 0 && ImGui::Button("Reset to the paper's defaults", ImVec2(-1, 0))) r = ville::SocialRules{};
  changed = changed || std::memcmp(&before, &r, sizeof r) != 0;
  if (changed) {
    ed.sim.setRules(ed.townSpec);
    if (!ed.townSpecPath.empty()) ed.townSpec.save(ed.townSpecPath);
  }
  // Who has their own rules, at a glance.
  if (!ed.townSpec.groupRules.empty() || !ed.townSpec.residentRules.empty()) {
    sectionHeader(ed, "Overrides");
    for (auto& [g, _] : ed.townSpec.groupRules) ImGui::BulletText("group: every %s", g.c_str());
    for (auto& [n, _] : ed.townSpec.residentRules) ImGui::BulletText("resident: %s", n.c_str());
  }
  ImGui::End();
}

}  // namespace app
