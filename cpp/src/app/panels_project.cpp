// Project panel (the games folder as Unity's Assets grid) and the script
// editor tabs that dock next to the Scene.
#include <windows.h>
#include <shellapi.h>

#include <algorithm>

#include "app/editor.h"

namespace app {

void drawProject(EditorState& ed) {
  if (!beginPanel("Project", &ed.showProject)) {
    ImGui::End();
    return;
  }
  // Toolbar: breadcrumb left, actions and tile size right
  ImGui::TextDisabled("Assets  >");
  ImGui::SameLine();
  ImGui::TextUnformatted(ed.assetsDir.filename().string().c_str());
  ImGui::SameLine();
  float right = 90 + 70 + 70 + 110 + 30;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - right));
  if (ImGui::SmallButton("+ New Script")) newScript(ed);
  ImGui::SameLine();
  if (ImGui::SmallButton("Refresh")) refreshAssets(ed);
  ImGui::SameLine();
  if (ImGui::SmallButton("Reveal"))
    ShellExecuteW(nullptr, L"open", ed.assetsDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  ImGui::SetItemTooltip("Open the folder in Explorer");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##tiles", &ed.projectTileSize, 48, 128, "");
  ImGui::SetItemTooltip("Icon size");
  ImGui::Separator();

  ImGui::BeginChild("##grid");
  if (ed.assets.empty()) {
    ImGui::TextDisabled("No .sl files in %s", ed.assetsDir.string().c_str());
  }
  float tile = ed.projectTileSize;
  float cellW = tile + 28;
  int cols = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellW));
  for (size_t i = 0; i < ed.assets.size(); ++i) {
    Asset& a = ed.assets[i];
    int col = static_cast<int>(i) % cols;
    if (col) ImGui::SameLine(col * cellW);
    ImGui::PushID(static_cast<int>(i));
    ImGui::BeginGroup();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool selected = ed.sel.kind == SelKind::Asset && ed.sel.index == (int)i;
    float labelH = ImGui::GetTextLineHeight() * 2 + 4;
    if (ImGui::InvisibleButton("##tile", ImVec2(cellW - 8, tile + labelH))) ed.sel = {SelKind::Asset, (int)i};
    bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) loadAsset(ed, (int)i);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected || hovered)
      dl->AddRectFilled(p, ImVec2(p.x + cellW - 8, p.y + tile + labelH),
                        ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 3);
    scriptIcon(ed, ImVec2(p.x + (cellW - 8 - tile) * 0.5f, p.y + 2), tile - 4, (int)i == ed.activeAsset);
    // Name, wrapped to two lines, centered; a dot marks unsaved edits.
    std::string label = (a.dirty() ? "* " : "") + a.name;
    float wrap = cellW - 12;
    ImVec2 ts = ImGui::CalcTextSize(label.c_str(), nullptr, false, wrap);
    ImVec2 tp(p.x + (cellW - 8 - ts.x) * 0.5f, p.y + tile + 2);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), tp,
                selected ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text), label.c_str(), nullptr, wrap);
    if (hovered) ImGui::SetTooltip("%s\nDouble-click to open in the Scene", a.path.string().c_str());
    if (ImGui::BeginPopupContextItem("##ctx")) {
      if (ImGui::MenuItem("Open in Scene")) loadAsset(ed, (int)i);
      if (ImGui::MenuItem("Edit Script")) {
        a.scriptOpen = true;
        a.focusScript = true;
      }
      if (ImGui::MenuItem("Save", nullptr, false, a.dirty())) saveAsset(ed, (int)i);
      ImGui::Separator();
      if (ImGui::MenuItem("Show in Explorer")) {
        std::wstring arg = L"/select,\"" + a.path.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
      }
      ImGui::EndPopup();
    }
    ImGui::EndGroup();
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::End();
}

void drawScripts(EditorState& ed) {
  ed.focusedScript = -1;
  for (size_t i = 0; i < ed.assets.size(); ++i) {
    Asset& a = ed.assets[i];
    if (!a.scriptOpen) continue;
    std::string title = a.name + "###script:" + a.path.string();
    if (ed.sceneDockId) ImGui::SetNextWindowDockID(ed.sceneDockId, ImGuiCond_FirstUseEver);
    if (a.focusScript) {
      ImGui::SetNextWindowFocus();
      a.focusScript = false;
    }
    ImGuiWindowFlags flags = a.dirty() ? ImGuiWindowFlags_UnsavedDocument : 0;
    bool open = true;
    if (beginPanel(title.c_str(), &open, flags)) {
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ed.focusedScript = static_cast<int>(i);
      bool isActive = static_cast<int>(i) == ed.activeAsset;
      ImGui::BeginDisabled(!a.dirty());
      if (ImGui::SmallButton("Save")) saveAsset(ed, (int)i);
      ImGui::SameLine();
      if (ImGui::SmallButton("Revert")) a.text = a.saved;
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::SmallButton(isActive ? "Reload in Scene" : "Open in Scene")) {
        if (a.dirty()) saveAsset(ed, (int)i);
        loadAsset(ed, (int)i);
      }
      ImGui::SetItemTooltip("Save, then parse and load this script into the Scene at round 0");
      ImGui::SameLine();
      ImGui::TextDisabled("%s", a.dirty() ? "unsaved  |  Ctrl+S to save" : "saved");
      if (isActive && ed.info.status == SimStatus::CompileError) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ed.pal.error));
        ImGui::TextWrapped("%s", ed.info.error.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::PushFont(ed.fonts.mono, 0.0f);
      ImGui::InputTextMultiline("##src", &a.text, ImGui::GetContentRegionAvail(),
                                ImGuiInputTextFlags_AllowTabInput);
      ImGui::PopFont();
    }
    ImGui::End();
    if (!open) a.scriptOpen = false;
  }
}

}  // namespace app
