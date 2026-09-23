// Inspector: Unity-style component stacks for whatever is selected -- an
// agent (Transform, Agent, Persona, Plan, Recent Activity), a location
// (Transform, Location, Occupants), the sim itself, or a script asset.
#include <algorithm>

#include "app/editor.h"
#include "ville/world.h"

namespace app {

namespace {

// Component headers are neutral gray bars, as in Unity -- blue is reserved
// for selection.
bool component(EditorState& ed, const char* title) {
  ImGui::PushStyleColor(ImGuiCol_Header, ed.dark ? IM_COL32(62, 62, 62, 255) : IM_COL32(214, 214, 214, 255));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ed.dark ? IM_COL32(72, 72, 72, 255) : IM_COL32(224, 224, 224, 255));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ed.dark ? IM_COL32(80, 80, 80, 255) : IM_COL32(200, 200, 200, 255));
  ImGui::PushFont(ed.fonts.bold, 0.0f);
  bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
  ImGui::PopFont();
  ImGui::PopStyleColor(3);
  if (open) ImGui::Indent(4);
  return open;
}
void endComponent() {
  ImGui::Unindent(4);
  ImGui::Spacing();
}

// Unity's header: an icon, the object's name, and Tag / Layer below.
void objectHeader(EditorState& ed, void (*icon)(EditorState&, int, float), int arg, const char* name, const char* sub) {
  ImGui::Spacing();
  float s = ImGui::GetFrameHeight() * 1.4f;
  icon(ed, arg, s);
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::PushFont(ed.fonts.bold, ImGui::GetStyle().FontSizeBase * 1.15f);
  ImGui::TextUnformatted(name);
  ImGui::PopFont();
  if (sub && *sub) ImGui::TextDisabled("%s", sub);
  ImGui::EndGroup();
  ImGui::Separator();
}

void agentIconFn(EditorState& ed, int a, float s) { agentIcon(ed, a, s); }
void locationIconFn(EditorState& ed, int, float s) { locationIcon(ed, s); }
void scriptIconFn(EditorState& ed, int active, float s) {
  scriptIcon(ed, ImGui::GetCursorScreenPos(), s, active != 0);
  ImGui::Dummy(ImVec2(s, s));
}
void simIconFn(EditorState& ed, int, float s) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(s, s));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // A tiny scene graph: three linked nodes.
  ImVec2 a(p.x + s * 0.25f, p.y + s * 0.3f), b(p.x + s * 0.75f, p.y + s * 0.35f), c(p.x + s * 0.45f, p.y + s * 0.75f);
  dl->AddLine(a, b, ed.pal.dim, 1.5f);
  dl->AddLine(a, c, ed.pal.dim, 1.5f);
  dl->AddLine(b, c, ed.pal.dim, 1.5f);
  for (ImVec2 q : {a, b, c}) dl->AddCircleFilled(q, s * 0.1f, ed.pal.accent);
}

void readOnlyVec2(const char* label, float x, float y) {
  float labelW = std::max(96.0f, ImGui::GetContentRegionAvail().x * 0.38f);
  float x0 = ImGui::GetCursorPosX();
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", label);
  ImGui::SameLine(x0 + labelW);
  float w = (ImGui::GetContentRegionAvail().x - 30) * 0.5f;
  ImGui::BeginDisabled();
  ImGui::TextUnformatted("X");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(w);
  ImGui::InputFloat("##x", &x, 0, 0, "%.2f");
  ImGui::SameLine();
  ImGui::TextUnformatted("Y");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(w);
  ImGui::InputFloat("##y", &y, 0, 0, "%.2f");
  ImGui::EndDisabled();
}

// Log lines involving an agent (authored by it, or visible to it), newest
// first -- read from the tail so it stays cheap on long runs.
void recentActivity(EditorState& ed, int agent, size_t logEnd, int max) {
  const std::string& seat = ed.info.agents[agent].seat;
  int shown = 0;
  size_t scanned = 0;
  for (size_t i = std::min(logEnd, ed.log.size()); i-- > 0 && shown < max && scanned < 20000; ++scanned) {
    const sl::LogEntry& e = ed.log[i];
    bool involved = e.author == seat || std::find(e.visibleTo.begin(), e.visibleTo.end(), agent) != e.visibleTo.end();
    if (!involved) continue;
    ++shown;
    ImGui::PushID(static_cast<int>(i));
    kindIcon(ed, e.kind, ImGui::GetTextLineHeight());
    ImGui::SameLine();
    ImGui::TextDisabled("R%d", e.round);
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(e.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopID();
  }
  if (shown == 0) ImGui::TextDisabled("Nothing yet -- press Play or Step.");
}


// ---- Towns: a resident's state (memory, plan, reflections), and a room.

void wrapped(const std::string& s) {
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(s.c_str());
  ImGui::PopTextWrapPos();
}

void inspectResident(EditorState& ed, int i) {
  const SimInfo& info = ed.info;
  const AgentStatic& a = info.agents[i];
  const AgentDetail* d = info.details && i < (int)info.details->size() ? &(*info.details)[i] : nullptr;
  std::string sub = a.role + "  |  lives at " + a.team;
  objectHeader(ed, [](EditorState& e, int x, float s) { agentIcon(e, x, s); }, i, a.seat.c_str(), sub.c_str());
  if (!d) return;

  if (component(ed, "Now")) {
    ImGui::PushFont(ed.fonts.bold, 0.0f);
    wrapped(d->emoji + "  " + d->action);
    ImGui::PopFont();
    if (!d->address.empty()) ImGui::TextDisabled("%s", d->address.c_str());
    if (d->chatWith >= 0 && d->chatWith < (int)info.agents.size()) {
      ImGui::TextDisabled("talking with");
      ImGui::SameLine();
      if (ImGui::TextLink(info.agents[d->chatWith].seat.c_str())) ed.sel = {SelKind::Agent, d->chatWith};
      if (!d->utterance.empty()) wrapped("\"" + d->utterance + "\"");
    }
    endComponent();
  }
  if (component(ed, "Identity")) {
    propertyRow("Innate", "%s", d->innate.c_str());
    propertyRow("Learned", "%s", d->learned.c_str());
    propertyRow("Currently", "%s", d->currently.c_str());
    propertyRow("Lifestyle", "%s", d->lifestyle.c_str());
    propertyRow("Home", "%s", d->homeName.c_str());
    propertyRow("Work", "%s", d->workName.c_str());
    propertyRow("Thinks with", "%s", a.model.c_str());
    endComponent();
  }
  if (!d->dailyPlan.empty() && component(ed, "Daily plan")) {
    for (size_t k = 0; k < d->dailyPlan.size(); ++k) ImGui::BulletText("%s", d->dailyPlan[k].c_str());
    endComponent();
  }
  if (!d->plan.empty() && component(ed, "Hourly schedule")) {
    std::string last;
    for (size_t h = 0; h < d->plan.size(); ++h) {
      bool now = (int)h == d->planCursor;
      if (d->plan[h].second == last && !now) continue;
      last = d->plan[h].second;
      if (now) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ed.pal.select), "> %s  %s", d->plan[h].first.c_str(), last.c_str());
      else ImGui::TextDisabled("  %s  %s", d->plan[h].first.c_str(), last.c_str());
      if (now && !d->subplan.empty()) {
        ImGui::Indent(22);
        for (size_t k = 0; k < d->subplan.size(); ++k) {
          if ((int)k == d->subplanCursor) ImGui::TextUnformatted(("- " + d->subplan[k]).c_str());
          else ImGui::TextDisabled("- %s", d->subplan[k].c_str());
        }
        ImGui::Unindent(22);
      }
    }
    endComponent();
  }
  char memTitle[64];
  std::snprintf(memTitle, sizeof memTitle, "Memory stream (%d)###mem", d->memoryCount);
  if (component(ed, memTitle)) {
    ImGui::TextDisabled("newest first  |  [importance 1-10]");
    for (auto& [type, text] : d->memories) {
      sl::LogKind k = type == 2 ? sl::LogKind::Reflection : type == 1 ? sl::LogKind::Dialogue : sl::LogKind::Note;
      kindIcon(ed, k, ImGui::GetTextLineHeight());
      ImGui::SameLine();
      wrapped(text);
    }
    endComponent();
  }
  if (!d->relations.empty() && component(ed, "Relationships")) {
    for (auto& [who, fam] : d->relations) {
      int idx = seatIndex(ed, who);
      ImGui::PushID(who.c_str());
      if (ImGui::TextLink(who.c_str()) && idx >= 0) ed.sel = {SelKind::Agent, idx};
      ImGui::SameLine();
      ImGui::TextDisabled("%.0f conversation%s", fam, fam == 1 ? "" : "s");
      ImGui::PopID();
    }
    endComponent();
  }
  if (!d->knows.empty() && component(ed, "Knows")) {
    for (auto& k : d->knows) ImGui::BulletText("%s", k.c_str());
    endComponent();
  }
  editResident(ed, i);
}

void inspectRoom(EditorState& ed, int arena) {
  if (!ed.info.villeWorld) return;
  const ville::World& w = *ed.info.villeWorld;
  const ville::Arena& ar = w.arenas[arena];
  objectHeader(ed, locationIconFn, arena, ar.name.c_str(), w.sectors[ar.sector].name.c_str());
  if (ImGui::Button(("Edit " + w.sectors[ar.sector].name).c_str(), ImVec2(-1, 0))) ed.sel = {SelKind::Building, ar.sector};
  ImGui::SetItemTooltip("Change how this building looks: type, size, colors, rooms, furniture");
  if (component(ed, "Objects")) {
    for (int o : ar.objects) {
      bool busy = ed.info.objectBusy && o < (int)ed.info.objectBusy->size() && (*ed.info.objectBusy)[o];
      propertyRow(w.objects[o].name.c_str(), "%s", busy ? w.objects[o].state.c_str() : "idle");
    }
    endComponent();
  }
  if (component(ed, "Here now")) {
    const Frame* f = viewedFrame(ed);
    int n = 0;
    if (f)
      for (size_t i = 0; i < f->agents.size(); ++i) {
        if (f->agents[i].location != arena) continue;
        ++n;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TextLink(ed.info.agents[i].seat.c_str())) ed.sel = {SelKind::Agent, static_cast<int>(i)};
        if (ed.info.details && i < ed.info.details->size()) {
          ImGui::SameLine();
          ImGui::TextDisabled("%s %s", (*ed.info.details)[i].emoji.c_str(), (*ed.info.details)[i].action.c_str());
        }
        ImGui::PopID();
      }
    if (n == 0) ImGui::TextDisabled("Nobody here right now.");
    endComponent();
  }
}

void inspectAgent(EditorState& ed, int i) {
  if (ed.info.isVille) {
    inspectResident(ed, i);
    return;
  }
  const SimInfo& info = ed.info;
  const AgentStatic& a = info.agents[i];
  const Frame* f = viewedFrame(ed);
  const AgentLive* live = f && i < (int)f->agents.size() ? &f->agents[i] : nullptr;
  const AgentDetail* d = info.details && i < (int)info.details->size() ? &(*info.details)[i] : nullptr;
  bool revealed = teamRevealed(ed, i);

  std::string sub = revealed ? "Role " + a.role + "  |  Team " + a.team : "Role hidden until revealed";
  objectHeader(ed, [](EditorState& e, int x, float s) { agentIcon(e, x, s); }, i, a.seat.c_str(), sub.c_str());

  if (component(ed, "Location")) {
    if (live && live->hasPos) readOnlyVec2("Position", live->x, live->y);
    else propertyRow("Position", "%s", info.hasWorld ? "not placed yet" : "no world in this game");
    if (live && live->location >= 0 && live->location < (int)info.locations.size()) {
      float labelW = std::max(96.0f, ImGui::GetContentRegionAvail().x * 0.38f);
      float x0 = ImGui::GetCursorPosX();
      ImGui::TextDisabled("Location");
      ImGui::SameLine(x0 + labelW);
      if (ImGui::TextLink(info.locations[live->location].id.c_str())) ed.sel = {SelKind::Location, live->location};
    }
    endComponent();
  }
  if (component(ed, "Agent")) {
    propertyRow("Role", "%s", revealed ? a.role.c_str() : "hidden");
    propertyRow("Team", "%s", revealed ? a.team.c_str() : "hidden");
    propertyRow("Model", "%s", a.model.c_str());
    bool alive = !live || live->alive;
    if (alive) propertyRow("Status", "alive");
    else propertyRow("Status", "eliminated%s%s", d && d->hasDeathCause ? " - " : "",
                     d && d->hasDeathCause ? d->deathCause.c_str() : "");
    endComponent();
  }
  if (d && !d->persona.empty() && component(ed, "Persona")) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(d->persona.c_str());
    ImGui::PopTextWrapPos();
    endComponent();
  }
  if (d && !d->plan.empty() && component(ed, "Plan")) {
    for (size_t s = 0; s < d->plan.size(); ++s) {
      bool current = static_cast<int>(s) == d->planCursor;
      bool past = static_cast<int>(s) < d->planCursor;
      const auto& [time, activity] = d->plan[s];
      ImGui::PushStyleColor(ImGuiCol_Text, current ? ImGui::ColorConvertU32ToFloat4(ed.pal.select)
                                           : past  ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                                                   : ImGui::GetStyleColorVec4(ImGuiCol_Text));
      ImGui::PushTextWrapPos(0);
      ImGui::Text("%s %s%s%s", current ? ">" : " ", time.c_str(), time.empty() ? "" : "  ", activity.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
      if (current && !d->subplan.empty()) {
        ImGui::Indent(18);
        for (size_t k = 0; k < d->subplan.size(); ++k) {
          bool now = static_cast<int>(k) == d->subplanCursor;
          if (now) ImGui::TextUnformatted(("- " + d->subplan[k]).c_str());
          else ImGui::TextDisabled("- %s", d->subplan[k].c_str());
        }
        ImGui::Unindent(18);
      }
    }
    endComponent();
  }
  if (component(ed, "Recent Activity")) {
    recentActivity(ed, i, f ? f->logCount : ed.log.size(), 14);
    endComponent();
  }
}

void inspectLocation(EditorState& ed, int li) {
  if (ed.info.isVille) {
    inspectRoom(ed, li);
    return;
  }
  const sl::Location& loc = ed.info.locations[li];
  objectHeader(ed, locationIconFn, li, loc.id.c_str(), ("Type " + loc.type).c_str());
  if (component(ed, "Position")) {
    readOnlyVec2("Position", (float)loc.x, (float)loc.y);
    endComponent();
  }
  if (component(ed, "Location")) {
    propertyRow("Type", "%s", loc.type.c_str());
    propertyRow("Tag", "%s", loc.hasTag ? loc.tag.c_str() : "none");
    if (loc.capacity >= 0) propertyRow("Capacity", "%d (not enforced)", loc.capacity);
    else propertyRow("Capacity", "unlimited");
    endComponent();
  }
  if (component(ed, "Occupants")) {
    const Frame* f = viewedFrame(ed);
    int n = 0;
    if (f)
      for (size_t i = 0; i < f->agents.size(); ++i) {
        if (f->agents[i].location != li) continue;
        if (++n > 200) continue;
        ImGui::PushID(static_cast<int>(i));
        agentIcon(ed, static_cast<int>(i), ImGui::GetTextLineHeight());
        ImGui::SameLine();
        if (ImGui::TextLink(ed.info.agents[i].seat.c_str())) ed.sel = {SelKind::Agent, static_cast<int>(i)};
        ImGui::PopID();
      }
    if (n == 0) ImGui::TextDisabled("Nobody here this round.");
    if (n > 200) ImGui::TextDisabled("... and %d more", n - 200);
    endComponent();
  }
}

void inspectSim(EditorState& ed) {
  const SimInfo& info = ed.info;
  std::string file = ed.activeAsset >= 0 ? ed.assets[ed.activeAsset].name : "";
  objectHeader(ed, simIconFn, 0, info.programName.c_str(), file.c_str());
  if (component(ed, "Simulation")) {
    int alive = 0;
    if (!ed.frames.empty())
      for (auto& a : ed.frames.back()->agents) alive += a.alive;
    propertyRow("Agents", "%d alive of %zu", alive, info.agents.size());
    propertyRow("Locations", "%zu", info.locations.size());
    if (info.hasWorld) propertyRow("World", "%d x %d", info.worldW, info.worldH);
    propertyRow("Round", "%d", info.round);
    if (!info.winner.empty()) propertyRow("Winner", "%s", info.winner.c_str());
    propertyRow("Hidden roles", "%s", info.hiddenRoles ? "yes - revealed on elimination" : "no");
    endComponent();
  }
  if (component(ed, "Run")) {
    float labelW = std::max(96.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    float x0 = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Seed");
    ImGui::SameLine(x0 + labelW);
    int seed = static_cast<int>(ed.seed);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputInt("##seed", &seed, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue) ||
        (ImGui::IsItemDeactivatedAfterEdit())) {
      ed.seed = static_cast<uint32_t>(std::max(0, seed));
      loadAsset(ed, ed.activeAsset);
    }
    ImGui::SetItemTooltip("Same seed, same run: roles, world layout, and every mock answer replay exactly");
    propertyRow("Provider calls", "%lld", info.calls);
    propertyRow("Tokens", "%lld in / %lld out", info.promptTokens, info.completionTokens);
    propertyRow("Errors", "%lld", info.errors);
    std::string roster;
    for (size_t k = 0; k < info.rosterLabels.size(); ++k) roster += (k ? ", " : "") + info.rosterLabels[k];
    propertyRow("Models", "%s", roster.empty() ? "-" : roster.c_str());
    endComponent();
  }
}

void inspectAsset(EditorState& ed, int idx) {
  Asset& a = ed.assets[idx];
  bool active = idx == ed.activeAsset;
  objectHeader(ed, scriptIconFn, active ? 1 : 0, a.name.c_str(), active ? "Loaded in the Scene" : "SocialLang script");
  float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  if (ImGui::Button("Open in Scene", ImVec2(w, 0))) loadAsset(ed, idx);
  ImGui::SameLine();
  if (ImGui::Button("Edit Script", ImVec2(w, 0))) {
    a.scriptOpen = true;
    a.focusScript = true;
  }
  ImGui::Spacing();
  if (component(ed, "Imported Script")) {
    ImGui::TextDisabled("%s", a.path.string().c_str());
    if (a.dirty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ed.pal.warning), "Unsaved changes");
    ImGui::PushFont(ed.fonts.mono, 0.0f);
    ImGui::BeginChild("##preview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(a.text.c_str());
    ImGui::EndChild();
    ImGui::PopFont();
    endComponent();
  }
}

}  // namespace

void drawInspector(EditorState& ed) {
  if (!beginPanel("Inspector", &ed.showInspector)) {
    ImGui::End();
    return;
  }
  const Selection& s = ed.sel;
  if (s.kind == SelKind::Agent && s.index >= 0 && s.index < (int)ed.info.agents.size()) inspectAgent(ed, s.index);
  else if (s.kind == SelKind::Location && s.index >= 0 && s.index < (int)ed.info.locations.size())
    inspectLocation(ed, s.index);
  else if (s.kind == SelKind::Sim && ed.info.status != SimStatus::Empty) inspectSim(ed);
  else if (s.kind == SelKind::Asset && s.index >= 0 && s.index < (int)ed.assets.size()) inspectAsset(ed, s.index);
  else if (s.kind == SelKind::Building) inspectBuilding(ed, s.index);
  else {
    ImGui::Spacing();
    ImGui::TextDisabled("Nothing selected.");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled(
        "Click a resident or a room in the World view or the Town panel to inspect them: what they're doing, "
        "their plan for the day, their memory stream, and who they know.");
    ImGui::PopTextWrapPos();
  }
  ImGui::End();
}

}  // namespace app
