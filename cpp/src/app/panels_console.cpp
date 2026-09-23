// Console: the run's log, Unity-style -- a toolbar with Clear, Auto-scroll,
// search, and per-kind toggle counters on the right; one row per line with a
// kind icon; the selected line's full text in a pane below. Clicking a line
// also selects the agent who wrote it.
#include <algorithm>
#include <cctype>
#include <cstring>

#include "app/editor.h"

namespace app {

namespace {

struct KindGroup {
  const char* label;
  const char* tip;
  sl::LogKind icon;
  std::vector<sl::LogKind> kinds;
};

const std::vector<KindGroup>& groups() {
  using sl::LogKind;
  static const std::vector<KindGroup> g = {
      {"Talk", "Dialogue between agents (converse)", LogKind::Dialogue, {LogKind::Dialogue}},
      {"News", "Town news spreading (who heard what from whom), broadcasts, whispers, eliminations", LogKind::Whisper, {LogKind::Broadcast, LogKind::Whisper}},
      {"Reflect", "Reflections -- higher-level insights an agent drew", LogKind::Reflection, {LogKind::Reflection}},
      {"Plans", "Plans made with make_plan", LogKind::Plan, {LogKind::Plan}},
      {"Actions", "What residents start doing (the town logs this below 200 residents), and private asks", LogKind::Note, {LogKind::Ask, LogKind::Note}},
      {"Print", "print() output from the script", LogKind::Print, {LogKind::Print}},
      {"Errors", "Script errors and failed model calls", LogKind::Error, {LogKind::Error}},
  };
  return g;
}

bool containsCI(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
           return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
         }) != hay.end();
}

}  // namespace

void drawConsole(EditorState& ed) {
  if (!beginPanel("Activity", &ed.showConsole)) {
    ImGui::End();
    return;
  }
  if (ed.consoleClearedAt > ed.log.size()) ed.consoleClearedAt = 0;

  // Counts per group since the last Clear, updated incrementally.
  static int counts[7] = {};
  static size_t countedTo = 0, countedFrom = 0;
  if (countedFrom != ed.consoleClearedAt || countedTo > ed.log.size()) {
    std::fill(std::begin(counts), std::end(counts), 0);
    countedFrom = countedTo = ed.consoleClearedAt;
  }
  for (; countedTo < ed.log.size(); ++countedTo)
    for (size_t g = 0; g < groups().size(); ++g)
      for (auto k : groups()[g].kinds)
        if (ed.log[countedTo].kind == k) ++counts[g];

  // ---- Toolbar
  if (ImGui::SmallButton("Clear")) {
    ed.consoleClearedAt = ed.log.size();
    ed.selectedLog = -1;
  }
  ImGui::SetItemTooltip("Hide everything logged so far");
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Button, ed.consoleAutoScroll ? ImGui::GetStyleColorVec4(ImGuiCol_Header)
                                                              : ImGui::GetStyleColorVec4(ImGuiCol_Button));
  if (ImGui::SmallButton("Auto-scroll")) ed.consoleAutoScroll = !ed.consoleAutoScroll;
  ImGui::PopStyleColor();
  ImGui::SameLine();
  float togglesW = 0;
  for (size_t g = 0; g < groups().size(); ++g)
    togglesW += ImGui::CalcTextSize(groups()[g].label).x + ImGui::GetTextLineHeight() + 48;
  ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - togglesW));
  ImGui::InputTextWithHint("##search", "Search", &ed.consoleSearch);
  for (size_t g = 0; g < groups().size(); ++g) {
    const KindGroup& kg = groups()[g];
    bool on = ed.showKind[static_cast<int>(kg.kinds[0])];
    ImGui::SameLine(0, 4);
    ImGui::PushID(static_cast<int>(g));
    char text[32];
    std::snprintf(text, sizeof text, "%s %d", kg.label, counts[g]);
    float lh = ImGui::GetTextLineHeight();
    float w = ImGui::CalcTextSize(text).x + lh + 14;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (ImGui::Button("##t", ImVec2(w, ImGui::GetFrameHeight())))
      for (auto k : kg.kinds) ed.showKind[static_cast<int>(k)] = !on;
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", kg.tip);
    // Icon and label painted over the button; the layout cursor stays put.
    ImVec2 p = ImGui::GetItemRectMin();
    float y = p.y + ImGui::GetStyle().FramePadding.y;
    kindIconAt(ed, kg.icon, ImVec2(p.x + 5, y), lh);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 8 + lh, y), ImGui::GetColorU32(ImGuiCol_Text), text);
    ImGui::PopID();
  }
  ImGui::Separator();

  // ---- Filter (rebuilt only when the log, filters, or search change)
  static std::vector<int> rows;
  static size_t builtLogSize = 0, builtFrom = 0;
  static std::string builtSearch;
  static bool builtKinds[9];
  bool kindsChanged = !std::equal(std::begin(builtKinds), std::end(builtKinds), std::begin(ed.showKind));
  if (kindsChanged || builtSearch != ed.consoleSearch || builtFrom != ed.consoleClearedAt || ed.log.size() < builtLogSize) {
    rows.clear();
    builtLogSize = ed.consoleClearedAt;
    builtFrom = ed.consoleClearedAt;
    builtSearch = ed.consoleSearch;
    std::copy(std::begin(ed.showKind), std::end(ed.showKind), std::begin(builtKinds));
  }
  for (size_t i = std::max(builtLogSize, ed.consoleClearedAt); i < ed.log.size(); ++i) {
    const sl::LogEntry& e = ed.log[i];
    if (!ed.showKind[static_cast<int>(e.kind)]) continue;
    if (!containsCI(e.text, ed.consoleSearch) && !containsCI(e.author, ed.consoleSearch)) continue;
    rows.push_back(static_cast<int>(i));
  }
  bool grew = ed.log.size() > builtLogSize;
  builtLogSize = ed.log.size();

  // ---- List + detail pane
  float detailH = ed.selectedLog >= 0 ? 86.0f : 0.0f;
  bool compileError = ed.info.status == SimStatus::CompileError;
  ImGui::BeginChild("##log", ImVec2(0, -detailH), ImGuiChildFlags_None);
  float lh = ImGui::GetTextLineHeight();
  if (compileError) {  // pinned, like Unity's compiler errors
    kindIcon(ed, sl::LogKind::Error, lh);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ed.pal.error));
    ImGui::TextWrapped("%s", ed.info.error.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
  }
  if (rows.empty() && !compileError) ImGui::TextDisabled(ed.log.empty() ? "Nothing logged yet -- press Play or Step." : "No lines match.");
  ImGuiListClipper clip;
  clip.Begin(static_cast<int>(rows.size()));
  while (clip.Step())
    for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
      int i = rows[r];
      const sl::LogEntry& e = ed.log[i];
      ImGui::PushID(i);
      ImVec2 start = ImGui::GetCursorPos();
      if (ImGui::Selectable("##row", ed.selectedLog == i, ImGuiSelectableFlags_SpanAllColumns)) {
        ed.selectedLog = i;
        int a = seatIndex(ed, e.author);
        if (a >= 0) ed.sel = {SelKind::Agent, a};
      }
      ImGui::SetCursorPos(start);
      kindIcon(ed, e.kind, lh);
      ImGui::SameLine();
      if (ed.info.isVille) ImGui::TextDisabled("%s", villeClockForStep(ed, e.round).c_str());
      else ImGui::TextDisabled("R%-3d", e.round);
      ImGui::SameLine();
      if (!e.author.empty()) {
        ImGui::PushFont(ed.fonts.bold, 0.0f);
        ImGui::TextUnformatted(e.author.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
      }
      // One line per row (the clipper needs uniform heights): the first
      // line of the entry, marked when more follows. Full text is below.
      const char* begin = e.text.c_str();
      const char* nl = std::strchr(begin, '\n');
      if (e.kind == sl::LogKind::Error) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ed.pal.error));
      ImGui::TextUnformatted(begin, nl ? nl : begin + e.text.size());
      if (e.kind == sl::LogKind::Error) ImGui::PopStyleColor();
      if (nl) {
        ImGui::SameLine();
        ImGui::TextDisabled("...");
      }
      ImGui::PopID();
    }
  if (ed.consoleAutoScroll && grew) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();

  if (ed.selectedLog >= 0 && ed.selectedLog < (int)ed.log.size()) {
    const sl::LogEntry& e = ed.log[ed.selectedLog];
    ImGui::Separator();
    ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_None);
    if (ed.info.isVille)
      ImGui::TextDisabled("%s  |  %s%s%s", villeClockForStep(ed, e.round).c_str(), sl::logKindName(e.kind),
                          e.author.empty() ? "" : "  |  ", e.author.c_str());
    else
      ImGui::TextDisabled("Round %d  |  %s%s%s", e.round, sl::logKindName(e.kind), e.author.empty() ? "" : "  |  by ",
                          e.author.c_str());
    if (!e.visibleTo.empty()) {
      std::string who;
      for (size_t k = 0; k < e.visibleTo.size() && k < 12; ++k)
        who += (k ? ", " : "") + ed.info.agents[e.visibleTo[k]].seat;
      if (e.visibleTo.size() > 12) who += ", ...";
      ImGui::SameLine();
      ImGui::TextDisabled("  |  visible to %s", who.c_str());
    } else if (e.seq > 0) {
      ImGui::SameLine();
      ImGui::TextDisabled("  |  public");
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(e.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
  }
  ImGui::End();
}

}  // namespace app
