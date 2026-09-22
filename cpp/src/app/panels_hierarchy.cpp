// Hierarchy: the sim as a scene graph. The sim is the root; World holds the
// locations grouped by type; Agents holds every seat. Thousands of rows stay
// cheap because only the visible ones are laid out (ImGuiListClipper).
#include <algorithm>
#include <cctype>
#include <map>

#include "app/editor.h"

namespace app {

namespace {

bool matches(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                        [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
  return it != haystack.end();
}

// One selectable row with a drawn icon, Unity-style full-width highlight.
bool row(EditorState& ed, const char* id, bool selected, void (*icon)(EditorState&, int, float), int iconArg,
         const char* label, const char* suffix, bool dimmed) {
  ImGui::PushID(id);
  ImVec2 start = ImGui::GetCursorPos();
  bool clicked = ImGui::Selectable("##row", selected,
                                   ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
  bool doubleClicked = clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  ImGui::SetCursorPos(start);
  float h = ImGui::GetTextLineHeight();
  icon(ed, iconArg, h);
  ImGui::SameLine(0, 4);
  if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextUnformatted(label);
  if (dimmed) ImGui::PopStyleColor();
  if (suffix && *suffix) {
    ImGui::SameLine(0, 6);
    ImGui::TextDisabled("%s", suffix);
  }
  ImGui::PopID();
  if (doubleClicked) ed.frameSelection = true;
  return clicked;
}

void agentIconFn(EditorState& ed, int a, float s) { agentIcon(ed, a, s); }
void locationIconFn(EditorState& ed, int, float s) { locationIcon(ed, s); }

}  // namespace

void drawHierarchy(EditorState& ed) {
  static std::string search;
  if (!beginPanel("Hierarchy", &ed.showHierarchy)) {
    ImGui::End();
    return;
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##search", "Search", &search);
  ImGui::Separator();

  ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None);
  const SimInfo& info = ed.info;
  if (info.status == SimStatus::Empty || (info.status == SimStatus::CompileError && info.agents.empty())) {
    ImGui::TextDisabled(info.status == SimStatus::CompileError ? "Fix the compile error to see the scene."
                                                               : "Open a game from the Project panel.");
    ImGui::EndChild();
    ImGui::End();
    return;
  }

  const Frame* frame = viewedFrame(ed);
  ImGuiTreeNodeFlags base = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                            ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DrawLinesToNodes;

  // Root: the sim itself
  ImGuiTreeNodeFlags rootFlags = base | ImGuiTreeNodeFlags_DefaultOpen;
  if (ed.sel.kind == SelKind::Sim) rootFlags |= ImGuiTreeNodeFlags_Selected;
  ImGui::PushFont(ed.fonts.bold, 0.0f);
  bool rootOpen = ImGui::TreeNodeEx("##sim", rootFlags, "%s", info.programName.c_str());
  ImGui::PopFont();
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ed.sel = {SelKind::Sim, 0};
  if (!rootOpen) {
    ImGui::EndChild();
    ImGui::End();
    return;
  }

  // World: locations grouped by type
  std::vector<int> occupancy(info.locations.size(), 0);
  if (frame)
    for (auto& a : frame->agents)
      if (a.location >= 0 && a.location < (int)occupancy.size()) ++occupancy[a.location];
  if (!info.locations.empty()) {
    if (ImGui::TreeNodeEx("##world", base, "World  (%zu locations)", info.locations.size())) {
      std::map<std::string, std::vector<int>> byType;
      for (size_t i = 0; i < info.locations.size(); ++i)
        if (matches(info.locations[i].id, search)) byType[info.locations[i].type].push_back(static_cast<int>(i));
      for (auto& [type, ids] : byType) {
        ImGuiTreeNodeFlags f = base | (search.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::TreeNodeEx(type.c_str(), f, "%s  (%zu)", type.c_str(), ids.size())) {
          ImGuiListClipper clip;
          clip.Begin(static_cast<int>(ids.size()));
          while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
              int li = ids[r];
              const sl::Location& loc = info.locations[li];
              char suffix[48] = "";
              if (occupancy[li]) std::snprintf(suffix, sizeof suffix, "%d here", occupancy[li]);
              bool sel = ed.sel.kind == SelKind::Location && ed.sel.index == li;
              if (row(ed, loc.id.c_str(), sel, locationIconFn, li, loc.id.c_str(), suffix, false))
                ed.sel = {SelKind::Location, li};
            }
          ImGui::TreePop();
        }
      }
      ImGui::TreePop();
    }
  }

  // Agents
  std::vector<int> shown;
  shown.reserve(info.agents.size());
  for (size_t i = 0; i < info.agents.size(); ++i) {
    const AgentStatic& a = info.agents[i];
    bool revealed = teamRevealed(ed, static_cast<int>(i));
    if (matches(a.seat, search) || (revealed && (matches(a.role, search) || matches(a.team, search))))
      shown.push_back(static_cast<int>(i));
  }
  int alive = 0;
  if (frame)
    for (auto& a : frame->agents) alive += a.alive;
  if (ImGui::TreeNodeEx("##agents", base | ImGuiTreeNodeFlags_DefaultOpen, "Agents  (%d alive / %zu)", alive,
                        info.agents.size())) {
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(shown.size()));
    while (clip.Step())
      for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
        int i = shown[r];
        const AgentStatic& a = info.agents[i];
        bool dead = frame && i < (int)frame->agents.size() && !frame->agents[i].alive;
        std::string suffix = teamRevealed(ed, i) ? a.role : "role hidden";
        if (dead) suffix += "  - eliminated";
        bool sel = ed.sel.kind == SelKind::Agent && ed.sel.index == i;
        if (row(ed, a.seat.c_str(), sel, agentIconFn, i, a.seat.c_str(), suffix.c_str(), dead))
          ed.sel = {SelKind::Agent, i};
      }
    ImGui::TreePop();
  }
  ImGui::TreePop();
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace app
