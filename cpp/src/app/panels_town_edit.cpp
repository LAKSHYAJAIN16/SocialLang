// Customizing the town from the editor: how each building looks and is laid
// out, who the residents are, their relationships, and the social rules they
// follow. Edits are written back into the town's two .sl files -- building,
// resident and relationship edits into the environment file (and rebuild the
// town from the start); social rules into the behavior file (applied live).
#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>

#include "app/editor.h"
#include "ville/world.h"

namespace app {

namespace {

const char* kKinds[] = {"home", "cafe", "pub", "store", "market", "park", "college", "dorm", "town hall", "office"};
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
  unsigned rgb = (b(c[0]) << 16) | (b(c[1]) << 8) | b(c[2]);
  return rgb ? rgb : 0x010101;  // 0 means "inherit"
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

// Edits `target` in place; true when the edit is finished -- the moment to
// write the file and rebuild the town.
bool editText(const char* id, std::string& target, bool multiline = false) {
  if (multiline) ImGui::InputTextMultiline(id, &target, ImVec2(-1, ImGui::GetTextLineHeight() * 3.4f));
  else ImGui::InputText(id, &target);
  return ImGui::IsItemDeactivatedAfterEdit();
}

void scopeButtons(int& scope, const char* const* labels, int n) {
  for (int k = 0; k < n; ++k) {
    if (k) ImGui::SameLine(0, 2);
    bool on = scope == k;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (ImGui::Button(labels[k], ImVec2((ImGui::GetContentRegionAvail().x - (n - 1 - k) * 2) / (n - k), 0))) scope = k;
    ImGui::PopStyleColor();
  }
}

bool boldHeader(EditorState& ed, const char* title, bool open = true) {
  ImGui::PushFont(ed.fonts.bold, 0.0f);
  bool r = ImGui::CollapsingHeader(title, open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
  ImGui::PopFont();
  return r;
}

ville::ResidentSpec* findResident(EditorState& ed, const std::string& name) {
  for (auto& r : ed.townSpec.env.residents)
    if (r.name == name) return &r;
  return nullptr;
}

std::string fileName(const std::string& path) { return std::filesystem::path(path).filename().string(); }

// Social rules write straight into the behavior file and apply live.
void saveRules(EditorState& ed) {
  try {
    ed.townSpec.save();
  } catch (const std::exception& e) {
    notify(ed, e.what());
  }
  refreshAssets(ed);
}

}  // namespace

// Write the town's files and rebuild it from the start, keeping the view.
void applyTownEdits(EditorState& ed, const char* what) {
  bool wasPlaying = ed.playMode;
  float zoom = ed.zoom;
  ImVec2 cam = ed.camCenter;
  saveTown(ed);
  ed.zoom = zoom;
  ed.camCenter = cam;
  notify(ed, std::string(what) + " in " + fileName(ed.townSpec.envPath) + (wasPlaying ? " -- the town restarted" : ""));
}

void inspectBuilding(EditorState& ed, int sector) {
  if (!ed.info.villeWorld || sector < 0 || sector >= (int)ed.info.villeWorld->sectors.size()) return;
  const ville::World& w = *ed.info.villeWorld;
  const ville::Sector& s = w.sectors[sector];
  std::string kindName = ville::sectorKindName(s.kind);
  ville::EnvironmentSpec& env = ed.townSpec.env;
  ville::BuildingSpec* declared = ed.townSpec.findBuilding(s.key);
  bool changed = false;

  ImGui::Spacing();
  ImGui::PushFont(ed.fonts.bold, ImGui::GetStyle().FontSizeBase * 1.15f);
  ImGui::TextUnformatted(s.name.c_str());
  ImGui::PopFont();
  ImGui::TextDisabled("%s  |  %zu rooms  |  %s", kindName.c_str(), s.arenas.size(),
                      declared ? ("building \"" + s.key + "\"").c_str() : "generated");
  ImGui::Separator();

  // Scope: how widely an edit applies. The more specific setting wins.
  ImGui::TextDisabled("Apply changes to");
  std::string typeLabel = "Every " + kindName;
  const char* scopes[] = {"This building", typeLabel.c_str(), "Every building"};
  if (!declared && ed.buildingScope == 0) ed.buildingScope = 1;
  ImGui::BeginDisabled(!declared);
  scopeButtons(ed.buildingScope, scopes, 1);
  ImGui::EndDisabled();
  if (!declared) ImGui::SetItemTooltip("Generated buildings follow their type. Declare a building in the environment file to style it alone.");
  ImGui::SameLine(0, 2);
  {
    int sub = ed.buildingScope - 1;
    const char* rest[] = {scopes[1], scopes[2]};
    ImGui::PushID("rest");
    for (int k = 0; k < 2; ++k) {
      if (k) ImGui::SameLine(0, 2);
      bool on = sub == k;
      ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
      if (ImGui::Button(rest[k], ImVec2((ImGui::GetContentRegionAvail().x - (1 - k) * 2) / (2 - k), 0))) ed.buildingScope = k + 1;
      ImGui::PopStyleColor();
    }
    ImGui::PopID();
  }
  ImGui::TextDisabled("writes %s", ed.buildingScope == 0   ? ("building \"" + s.key + "\" { }").c_str()
                                   : ed.buildingScope == 1 ? ("type " + kindName + " { }").c_str()
                                                           : "style { }");

  ville::BuildingSpec& b = ed.buildingScope == 0 ? *declared : ed.buildingScope == 1 ? env.types[kindName] : env.style;
  ville::BuildingSpec probe;
  probe.name = s.key;
  probe.kind = kindName;
  ville::BuildingSpec eff = ed.townSpec.styleFor(declared ? *declared : probe);  // what this building shows now
  ImGui::Spacing();

  if (boldHeader(ed, "Look")) {
    if (ed.buildingScope == 0) {
      label("Name");
      std::string oldName = b.name;
      if (editText("##name", b.name) && !b.name.empty() && b.name != oldName) {
        // Keep residents and events that point at it.
        for (auto& r : env.residents) {
          if (r.home == oldName) r.home = b.name;
          if (r.work == oldName) r.work = b.name;
        }
        for (auto& e : env.events)
          if (e.at == oldName) e.at = b.name;
        changed = true;
      }
      int kind = static_cast<int>(s.kind);
      label("Type");
      if (ImGui::Combo("##kind", &kind, kKinds, IM_ARRAYSIZE(kKinds))) {
        b.kind = kKinds[kind];
        b.customRooms = false;  // a new type brings that type's rooms
        b.rooms.clear();
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

  // Rooms: a building can replace its type's layout; a type defines it.
  bool roomScope = ed.buildingScope == 0 || ed.buildingScope == 1;
  if (s.kind != ville::SectorKind::Park && roomScope && boldHeader(ed, "Rooms & furniture")) {
    bool editing = ed.buildingScope == 1 || b.customRooms;
    if (!editing) {
      for (int a : s.arenas) {
        ImGui::TextUnformatted(w.arenas[a].name.c_str());
        std::vector<std::string> objs;
        for (int o : w.arenas[a].objects) objs.push_back(w.objects[o].name);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", joinObjects(objs).c_str());
      }
      if (ImGui::Button("Give this building its own rooms", ImVec2(-1, 0))) {
        b.customRooms = true;
        b.rooms.clear();
        for (int a : s.arenas) {
          ville::RoomSpec r;
          r.name = w.arenas[a].name;
          for (int o : w.arenas[a].objects) r.objects.push_back(w.objects[o].name);
          b.rooms.push_back(r);
        }
        changed = true;
      }
    } else {
      ImGui::TextDisabled("First room is the hall the front door opens into. Objects: comma-separated.");
      int removeAt = -1;
      for (size_t k = 0; k < b.rooms.size(); ++k) {
        ImGui::PushID(static_cast<int>(k));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.34f);
        if (editText("##room", b.rooms[k].name)) changed = true;
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
      if (ed.buildingScope == 0) {
        ImGui::SameLine();
        if (ImGui::Button(("Use the " + kindName + " layout").c_str())) {
          b.customRooms = false;
          b.rooms.clear();
          changed = true;
        }
      }
      if (ed.buildingScope == 1 && b.hasBedroom) {
        int beds = std::max(1, b.bedrooms);
        label("Bedrooms");
        if (ImGui::SliderInt("##beds", &beds, 1, 6)) b.bedrooms = beds;
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        label("Bedroom objects");
        std::string objs = joinObjects(b.bedroom.objects);
        if (ImGui::InputText("##bedobjs", &objs)) b.bedroom.objects = splitObjects(objs);
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
      }
      ImGui::TextDisabled("Activities look for objects by name (\"bed\", \"stove\", \"desk\"...);");
      ImGui::TextDisabled("the behavior file says which.");
    }
    ImGui::Spacing();
  }

  if (ed.buildingScope == 0 && boldHeader(ed, "Residents")) {
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

  std::string reset = ed.buildingScope == 0   ? "Clear this building's own colors and size"
                      : ed.buildingScope == 1 ? "Clear every " + kindName + "'s colors and size"
                                              : "Clear the town-wide style";
  if (ImGui::Button(reset.c_str(), ImVec2(-1, 0))) {
    b.floorColor = b.wallColor = 0;
    b.size = -1;
    applyTownEdits(ed, "Style cleared");
    return;
  }
  if (changed) applyTownEdits(ed, ed.buildingScope == 0 ? "Building updated" : ed.buildingScope == 1 ? "Building type updated" : "Town style updated");
}

// A resident's editable persona and relationships, below their live state.
void editResident(EditorState& ed, int i) {
  if (!ed.info.villeWorld || !ed.info.details || i >= (int)ed.info.details->size()) return;
  const ville::World& w = *ed.info.villeWorld;
  const std::string name = ed.info.agents[i].seat;
  ville::EnvironmentSpec& env = ed.townSpec.env;
  ville::ResidentSpec* r = findResident(ed, name);
  bool changed = false;

  if (ImGui::Button(("Interaction rules for " + name.substr(0, name.find(' '))).c_str(), ImVec2(-1, 0))) {
    ed.showRules = true;
    ed.rulesScope = 2;
    ed.rulesResident = name;
    ImGui::SetWindowFocus("Rules");
  }
  if (boldHeader(ed, "Edit resident", false)) {
    if (!r) {
      ImGui::PushTextWrapPos(0);
      ImGui::TextDisabled("%s was made by the generate block in %s. Declare them as a resident there to edit them one by one.",
                          name.c_str(), fileName(ed.townSpec.envPath).c_str());
      ImGui::PopTextWrapPos();
    } else {
      ImGui::TextDisabled("writes resident \"%s\" { } -- changes restart the town", name.c_str());
      label("Traits");
      if (editText("##innate", r->innate)) changed = true;
      label("Background");
      if (editText("##learned", r->learned, true)) changed = true;
      label("Currently");
      if (editText("##currently", r->currently, true)) changed = true;
      label("Age");
      ImGui::SliderInt("##age", &r->age, 5, 100);
      if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

      // Routine: any routine the behavior file defines.
      label("Daily routine");
      if (ImGui::BeginCombo("##routine", r->routine.c_str())) {
        for (auto& [key, _] : ed.townSpec.behavior.routines)
          if (ImGui::Selectable(key.c_str(), key == r->routine)) r->routine = key, changed = true;
        ImGui::EndCombo();
      }
      ImGui::SetItemTooltip("Routines are defined in %s", fileName(ed.townSpec.behaviorPath).c_str());

      // Home and workplace pickers over the town's buildings.
      label("Lives at");
      if (ImGui::BeginCombo("##home", r->home.c_str())) {
        for (auto& s : w.sectors)
          if ((s.kind == ville::SectorKind::Home || s.kind == ville::SectorKind::Dorm) &&
              ImGui::Selectable(s.key.c_str(), s.key == r->home))
            r->home = s.key, r->bedroom.clear(), changed = true;
        ImGui::EndCombo();
      }
      label("Works at");
      if (ImGui::BeginCombo("##work", r->work.empty() ? "home" : r->work.c_str())) {
        if (ImGui::Selectable("home", r->work.empty() || r->work == "home")) r->work = "home", changed = true;
        for (auto& s : w.sectors)
          if (s.kind != ville::SectorKind::Home && s.kind != ville::SectorKind::Dorm &&
              ImGui::Selectable(s.key.c_str(), s.key == r->work))
            r->work = s.key, changed = true;
        ImGui::EndCombo();
      }
      label("Wakes up");
      ImGui::SliderInt("##wake", &r->wakeHour, 4, 11, "%d am");
      if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
      label("Goes to bed");
      ImGui::SliderInt("##sleep", &r->sleepHour, 20, 24, "%d:00");
      if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
      float& soc = r->sociability;
      label("Sociability");
      ImGui::SliderFloat("##soc", &soc, 0.0f, 1.0f, soc < 0.35f ? "reserved (%.2f)" : soc < 0.7f ? "average (%.2f)" : "outgoing (%.2f)");
      if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
      ImGui::Spacing();
      if (ImGui::Button(("Remove " + name + " from the town").c_str(), ImVec2(-1, 0))) {
        env.residents.erase(env.residents.begin() + (r - env.residents.data()));
        std::erase_if(env.relationships, [&](const ville::RelationshipSpec& x) { return x.a == name || x.b == name; });
        ed.sel = {};
        applyTownEdits(ed, "Resident removed");
        return;
      }
    }
    ImGui::Spacing();
  }
  if (changed) {
    applyTownEdits(ed, "Resident updated");
    return;
  }

  // Relationships: who they already know, and how.
  if (!boldHeader(ed, "Edit relationships", false)) return;
  ImGui::TextDisabled("Closeness raises how often they talk; the note shapes how they greet each other.");
  bool relChanged = false;
  int removeAt = -1;
  for (size_t k = 0; k < env.relationships.size(); ++k) {
    auto& rel = env.relationships[k];
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
    env.relationships.erase(env.relationships.begin() + removeAt);
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
    env.relationships.push_back({name, ed.info.agents[otherIdx[pick]].seat, 5, kNotes[pickNote]});
    relChanged = true;
  }
  if (relChanged) applyTownEdits(ed, "Relationships updated");
}

// Rules panel: how residents interact, applied live and saved to the
// behavior file. Scope: the whole town, a group (everyone on one routine), or
// one resident -- the most specific rules that exist for a resident win.
void drawRules(EditorState& ed) {
  if (!ed.showRules) return;
  if (!beginPanel("Rules", &ed.showRules)) {
    ImGui::End();
    return;
  }
  if (!ed.info.isVille || ed.townAsset < 0) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Load a town from Scenarios to edit how its residents interact.");
    ImGui::PopTextWrapPos();
    ImGui::End();
    return;
  }
  ville::BehaviorSpec& beh = ed.townSpec.behavior;
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("How residents interact. Changes apply immediately, even mid-run, and save to %s.",
                      fileName(ed.townSpec.behaviorPath).c_str());
  ImGui::PopTextWrapPos();

  const char* scopes[] = {"Whole town", "A group", "One resident"};
  scopeButtons(ed.rulesScope, scopes, 3);
  // Groups are the routines residents of this town follow.
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
  auto groupOf = [&](const std::string& resident) {
    int idx = seatIndex(ed, resident);
    std::string g = idx >= 0 ? ed.info.agents[idx].role : "";
    std::replace(g.begin(), g.end(), ' ', '_');
    return g;
  };

  ville::SocialRules* target = &beh.rules;
  std::map<std::string, ville::SocialRules>* table = nullptr;
  std::string key, who, block = "rules { }";
  if (ed.rulesScope == 1) {
    label("Group");
    if (ImGui::BeginCombo("##group", ed.rulesGroup.c_str())) {
      for (auto& g : groups)
        if (ImGui::Selectable(g.c_str(), g == ed.rulesGroup)) ed.rulesGroup = g;
      ImGui::EndCombo();
    }
    table = &beh.groupRules;
    key = ed.rulesGroup;
    who = "every " + key;
    block = "rules " + key + " { }";
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
    table = &beh.residentRules;
    key = ed.rulesResident;
    who = key;
    block = "rules \"" + key + "\" { }";
  }
  if (table) {
    auto it = table->find(key);
    if (it == table->end()) {
      ImGui::Spacing();
      ImGui::PushTextWrapPos(0);
      ImGui::TextDisabled("%s follows %s rules.", who.c_str(),
                          ed.rulesScope == 2 && beh.groupRules.count(groupOf(key)) ? "their group's" : "the town's");
      ImGui::PopTextWrapPos();
      if (ImGui::Button(("Give " + who + " their own rules").c_str(), ImVec2(-1, 0))) {
        // Start from whatever they follow now.
        (*table)[key] = ed.rulesScope == 2 ? ed.townSpec.rulesFor(key, groupOf(key)) : beh.rules;
        ed.sim.setRules(ed.townSpec);
        saveRules(ed);
      }
      ImGui::End();
      return;
    }
    target = &it->second;
    if (ImGui::Button(("Remove " + who + "'s own rules").c_str(), ImVec2(-1, 0))) {
      table->erase(it);
      ed.sim.setRules(ed.townSpec);
      saveRules(ed);
      ImGui::End();
      return;
    }
  }
  ImGui::TextDisabled("writes %s", block.c_str());

  ville::SocialRules& r = *target;
  ville::SocialRules before = r;
  bool done = false;  // an edit finished: write the file
  auto finished = [&] { done |= ImGui::IsItemDeactivatedAfterEdit(); };
  sectionHeader(ed, "Conversation");
  label("Chattiness");
  ImGui::SliderFloat("##chat", &r.chattiness, 0.0f, 5.0f, "%.2fx");
  finished();
  ImGui::SetItemTooltip("How likely they are to start talking with someone they meet");
  label("Time between chats");
  ImGui::SliderFloat("##cool", &r.chatCooldownHours, 0.25f, 12.0f, "%.1f game-hours");
  finished();
  ImGui::SetItemTooltip("How long before they talk to the same person again");
  label("Conversation length");
  static const char* kLen[] = {"brief", "normal", "chatty", "long"};
  ImGui::SliderInt("##len", &r.conversationLength, 0, 3, kLen[std::clamp(r.conversationLength, 0, 3)]);
  finished();
  label("Talks to strangers");
  ImGui::Checkbox("##strangers", &r.strangersTalk);
  finished();
  ImGui::SameLine();
  ImGui::TextDisabled(r.strangersTalk ? "yes" : "only to people they already know");
  sectionHeader(ed, "News and invitations");
  label("News eagerness");
  ImGui::SliderFloat("##news", &r.newsEagerness, 1.0f, 10.0f, "%.1fx");
  finished();
  ImGui::SetItemTooltip("How much likelier they are to start talking when they have news the other hasn't heard");
  label("Invite acceptance");
  ImGui::SliderFloat("##invite", &r.inviteAcceptance, 0.0f, 2.0f, "%.2fx");
  finished();
  ImGui::SetItemTooltip("Multiplies the chance they accept an invitation to an event");
  sectionHeader(ed, "Perception and memory");
  label("Vision radius");
  ImGui::SliderInt("##vision", &r.visionRadius, 1, 12, "%d tiles");
  finished();
  label("Attention");
  ImGui::SliderInt("##att", &r.attention, 1, 10, "%d observations / step");
  finished();
  label("Reflect after");
  ImGui::SliderFloat("##refl", &r.reflectThreshold, 20.0f, 500.0f, "%.0f importance");
  finished();
  ImGui::SetItemTooltip("Summed importance of new memories before a resident stops to reflect");
  ImGui::Spacing();
  if (ed.rulesScope == 0 && ImGui::Button("Reset to default rules", ImVec2(-1, 0))) {
    r = ville::SocialRules{};
    done = true;
  }
  if (std::memcmp(&before, &r, sizeof r) != 0) ed.sim.setRules(ed.townSpec);
  if (done) saveRules(ed);
  // Who has their own rules, at a glance.
  if (!beh.groupRules.empty() || !beh.residentRules.empty()) {
    sectionHeader(ed, "Overrides");
    for (auto& [g, _] : beh.groupRules) ImGui::BulletText("group: every %s", g.c_str());
    for (auto& [n, _] : beh.residentRules) ImGui::BulletText("resident: %s", n.c_str());
  }
  ImGui::End();
}

}  // namespace app
