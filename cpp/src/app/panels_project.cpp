// Project panel (the games folder as Unity's Project window: one folder per
// town, holding its environment and behavior files) and the script editor
// tabs that dock next to the Scene.
#include <windows.h>
#include <shellapi.h>

#include <algorithm>

#include "app/editor.h"
#include "app/sl_language.h"

namespace app {

// A script tab's code editor: highlighting, autocomplete, and live checks
// (syntax errors and likely typos, with one-click fixes).
struct ScriptView {
  TextEditor editor;
  std::string kind, dir;  // file kind, and its folder (where imports resolve)
  std::string synced;       // the text the editor holds (mirrors Asset::text)
  size_t undoIndex = 0;
  bool dark = true;
  TextEditor::AutoCompleteConfig autocomplete;
  // Diagnostics, re-run a moment after typing stops.
  std::string checked;
  double editedAt = 0;
  std::string error;
  int errorLine = 0;
  std::vector<SlTypo> typos;
};

namespace {

std::shared_ptr<ScriptView> makeView(EditorState& ed, const Asset& a) {
  auto v = std::make_shared<ScriptView>();
  v->kind = a.kind;
  v->dir = a.path.parent_path().string();
  v->dark = ed.dark;
  v->editor.SetLanguage(slLanguage());
  v->editor.SetPalette(slPalette(ed.dark));
  v->editor.SetTabSize(2);
  v->editor.SetShowWhitespacesEnabled(false);
  v->editor.SetText(a.text);
  v->synced = a.text;
  v->undoIndex = v->editor.GetUndoIndex();
  ScriptView* raw = v.get();
  v->autocomplete.triggerDelay = std::chrono::milliseconds(120);
  v->autocomplete.callback = [raw](TextEditor::AutoCompleteState& st) {
    st.suggestions = slSuggestions(raw->kind, raw->synced, st.searchTerm);
  };
  v->editor.SetAutoCompleteConfig(&v->autocomplete);
  // Right-click a flagged word to correct it.
  v->editor.SetTextContextMenuCallback([raw](TextEditor::PopupData& d) {
    bool any = false;
    for (auto& t : raw->typos) {
      if (t.line != d.pos.line) continue;
      any = true;
      std::string item = "Change \"" + t.word + "\" to \"" + t.fix + "\"";
      if (ImGui::MenuItem(item.c_str()))
        raw->editor.ReplaceSectionText(TextEditor::DocPos(t.line, t.col), TextEditor::DocPos(t.line, t.col + t.len), t.fix);
    }
    if (any) ImGui::Separator();
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, raw->editor.CanUndo())) raw->editor.Undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, raw->editor.CanRedo())) raw->editor.Redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", "Ctrl+X")) raw->editor.Cut();
    if (ImGui::MenuItem("Copy", "Ctrl+C")) raw->editor.Copy();
    if (ImGui::MenuItem("Paste", "Ctrl+V")) raw->editor.Paste();
  });
  return v;
}

void runChecks(EditorState& ed, ScriptView& v) {
  v.checked = v.synced;
  checkSource(v.synced, v.kind, v.dir, v.error, v.errorLine);
  v.typos = findTypos(v.synced, v.kind);
  v.editor.ClearMarkers();
  if (!v.error.empty())
    v.editor.AddMarker(v.errorLine > 0 ? v.errorLine - 1 : 0, ed.pal.error, IM_COL32(200, 60, 60, 60), v.error, v.error);
  for (auto& t : v.typos) {
    std::string tip = "\"" + t.word + "\": did you mean \"" + t.fix + "\"? Right-click to fix.";
    v.editor.AddMarker(t.line, ed.pal.warning, IM_COL32(220, 170, 40, 40), tip, tip);
  }
}

// Draws the editor for one asset, keeping Asset::text in sync both ways.
void drawCode(EditorState& ed, Asset& a) {
  if (!a.view || a.view->kind != a.kind) a.view = makeView(ed, a);
  ScriptView& v = *a.view;
  if (a.text != v.synced) {  // changed outside the editor (revert, file rewritten by a panel)
    v.editor.SetText(a.text);
    v.synced = a.text;
    v.undoIndex = v.editor.GetUndoIndex();
    v.checked.clear();
  }
  if (v.dark != ed.dark) {
    v.dark = ed.dark;
    v.editor.SetPalette(slPalette(ed.dark));
  }
  double now = ImGui::GetTime();
  if (v.checked != v.synced && now - v.editedAt > 0.35) runChecks(ed, v);

  // Status line: errors and typos, with the fix.
  if (!v.error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ed.pal.error));
    ImGui::TextWrapped("%s", v.error.c_str());
    ImGui::PopStyleColor();
    if (v.errorLine > 0) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Go to line")) {
        v.editor.SetCursor(TextEditor::DocPos(v.errorLine - 1, 0));
        v.editor.ScrollToLine(v.errorLine - 1);
      }
    }
  }
  if (!v.typos.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ed.pal.warning));
    const SlTypo& t = v.typos.front();
    if (v.typos.size() == 1)
      ImGui::Text("Line %zu: \"%s\" -- did you mean \"%s\"?", t.line + 1, t.word.c_str(), t.fix.c_str());
    else
      ImGui::Text("%zu likely typos, e.g. line %zu: \"%s\" -> \"%s\"", v.typos.size(), t.line + 1, t.word.c_str(), t.fix.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton(v.typos.size() == 1 ? "Fix" : "Fix all")) {
      // Right to left so earlier columns stay valid.
      auto typos = v.typos;
      std::sort(typos.begin(), typos.end(), [](auto& x, auto& y) { return x.line != y.line ? x.line > y.line : x.col > y.col; });
      for (auto& fix : typos)
        v.editor.ReplaceSectionText(TextEditor::DocPos(fix.line, fix.col), TextEditor::DocPos(fix.line, fix.col + fix.len), fix.fix);
    }
  }

  ImGui::PushFont(ed.fonts.mono, 0.0f);
  v.editor.Render("##code", ImGui::GetContentRegionAvail());
  ImGui::PopFont();
  if (v.editor.GetUndoIndex() != v.undoIndex) {
    v.undoIndex = v.editor.GetUndoIndex();
    v.synced = v.editor.GetText();
    a.text = v.synced;
    v.editedAt = now;
  }
}

}  // namespace

namespace {

void showInExplorer(const std::filesystem::path& p) {
  std::wstring arg = L"/select,\"" + p.wstring() + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

// One tile in the grid: an icon over a centered, two-line label. Returns true
// when it was double-clicked.
struct Tile {
  bool clicked = false, doubleClicked = false, hovered = false;
};
Tile drawTile(EditorState& ed, const std::string& label, bool selected, bool folder, bool active, float tile, float cellW) {
  Tile t;
  ImVec2 p = ImGui::GetCursorScreenPos();
  float labelH = ImGui::GetTextLineHeight() * 2 + 4;
  t.clicked = ImGui::InvisibleButton("##tile", ImVec2(cellW - 8, tile + labelH));
  t.hovered = ImGui::IsItemHovered();
  t.doubleClicked = t.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (selected || t.hovered)
    dl->AddRectFilled(p, ImVec2(p.x + cellW - 8, p.y + tile + labelH),
                      ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 3);
  ImVec2 icon(p.x + (cellW - 8 - tile) * 0.5f, p.y + 2);
  if (folder) folderIcon(ed, icon, tile - 4, active);
  else scriptIcon(ed, icon, tile - 4, active);
  float wrap = cellW - 12;
  ImVec2 ts = ImGui::CalcTextSize(label.c_str(), nullptr, false, wrap);
  ImVec2 tp(p.x + (cellW - 8 - ts.x) * 0.5f, p.y + tile + 2);
  dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), tp,
              selected ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text), label.c_str(), nullptr, wrap);
  return t;
}

void shelfTitle(EditorState& ed, const char* title, const char* hint) {
  ImGui::PushFont(ed.fonts.bold, 0.0f);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", hint);
}

void runTown(EditorState& ed, const std::string& town) {
  int env = townEnvAsset(ed, town);
  if (env >= 0) loadTown(ed, env);
  else notify(ed, town + " has no environment file (" + town + ".env.sl)");
}

// A file tile: select, double-click to run its town, right-click for the rest.
void fileTile(EditorState& ed, size_t i, float tile, float cellW) {
  Asset& a = ed.assets[i];
  ImGui::PushID(static_cast<int>(i));
  ImGui::BeginGroup();
  bool selected = ed.sel.kind == SelKind::Asset && ed.sel.index == (int)i;
  bool active = (int)i == ed.activeAsset || (int)i == ed.townAsset;
  Tile t = drawTile(ed, (a.dirty() ? "* " : "") + a.name, selected, false, active, tile, cellW);
  if (t.clicked) ed.sel = {SelKind::Asset, (int)i};
  if (t.doubleClicked && !a.library) loadAsset(ed, (int)i);
  if (t.doubleClicked && a.library) a.scriptOpen = a.focusScript = true;
  const char* role = a.kind == "environment" ? "Environment: buildings, residents, relationships, events"
                                             : "Behavior: social rules, routines, activities";
  if (t.hovered)
    ImGui::SetTooltip("%s\n%s%s%s\nDouble-click to %s", a.path.string().c_str(), role, a.detail.empty() ? "" : "\n",
                      a.detail.c_str(), a.library ? "edit it" : "run the town");
  if (ImGui::BeginPopupContextItem("##ctx")) {
    if (!a.library && ImGui::MenuItem("Run Town")) loadAsset(ed, (int)i);
    if (ImGui::MenuItem("Edit Script")) a.scriptOpen = a.focusScript = true;
    if (ImGui::MenuItem("Save", nullptr, false, a.dirty())) saveAsset(ed, (int)i);
    ImGui::Separator();
    if (ImGui::MenuItem("Show in Explorer")) showInExplorer(a.path);
    ImGui::EndPopup();
  }
  ImGui::EndGroup();
  ImGui::PopID();
}

}  // namespace

void drawProject(EditorState& ed) {
  if (!beginPanel("Scenarios", &ed.showProject)) {
    ImGui::End();
    return;
  }
  namespace fs = std::filesystem;
  // A folder that went away (deleted or renamed outside) drops back to the top.
  bool folderExists = ed.projectFolder.empty();
  for (auto& a : ed.assets) folderExists |= a.town == ed.projectFolder;
  if (!folderExists) ed.projectFolder.clear();
  bool inLib = ed.projectFolder == "lib";
  fs::path here = ed.projectFolder.empty() ? ed.assetsDir : ed.assetsDir / ed.projectFolder;

  // Breadcrumb, Unity-style: games > smallville.
  ImGui::PushFont(ed.fonts.bold, 0.0f);
  if (ed.projectFolder.empty()) {
    ImGui::TextUnformatted("games");
  } else {
    if (ImGui::SmallButton("games")) ed.projectFolder.clear();
    ImGui::SetItemTooltip("Back to all towns (Backspace)");
  }
  ImGui::PopFont();
  if (!ed.projectFolder.empty()) {
    ImGui::SameLine(0, 4);
    ImGui::TextDisabled(">");
    ImGui::SameLine(0, 4);
    ImGui::PushFont(ed.fonts.bold, 0.0f);
    ImGui::TextUnformatted(ed.projectFolder.c_str());
    ImGui::PopFont();
    if (!inLib) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Run Town")) runTown(ed, ed.projectFolder);
      ImGui::SetItemTooltip("Load %s in the World view", ed.projectFolder.c_str());
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", here.string().c_str());
  float right = 90 + 70 + 70 + 110 + 30;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - right));
  if (ImGui::SmallButton("+ New Town")) newScript(ed);
  ImGui::SetItemTooltip("A new town folder with an environment and a behavior file, both starting from smallville");
  ImGui::SameLine();
  if (ImGui::SmallButton("Refresh")) refreshAssets(ed);
  ImGui::SameLine();
  if (ImGui::SmallButton("Reveal")) ShellExecuteW(nullptr, L"open", here.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  ImGui::SetItemTooltip("Open this folder in Explorer");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##tiles", &ed.projectTileSize, 48, 128, "");
  ImGui::SetItemTooltip("Icon size");
  ImGui::Separator();

  if (!ed.projectFolder.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Backspace))
    ed.projectFolder.clear();

  ImGui::BeginChild("##grid");
  float tile = ed.projectTileSize;
  float cellW = tile + 28;
  int cols = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellW));
  auto place = [&](int& shown) {
    int col = shown++ % cols;
    if (col) ImGui::SameLine(col * cellW);
  };

  if (ed.projectFolder.empty()) {
    // The top level: one folder per town, then the library.
    std::vector<std::string> towns;
    for (auto& a : ed.assets)
      if (!a.library && !a.town.empty() && std::find(towns.begin(), towns.end(), a.town) == towns.end())
        towns.push_back(a.town);
    if (towns.empty()) ImGui::TextDisabled("No towns yet. + New Town makes one.");
    int shown = 0;
    for (auto& town : towns) {
      if (shown == 0) shelfTitle(ed, "Towns", "each town is a folder: its environment and behavior files. Double-click to open.");
      place(shown);
      int env = townEnvAsset(ed, town);
      ImGui::PushID(town.c_str());
      ImGui::BeginGroup();
      bool selected = env >= 0 && ed.sel.kind == SelKind::Asset && ed.sel.index == env;
      bool running = ed.townAsset >= 0 && ed.assets[ed.townAsset].town == town;
      bool dirty = false;
      for (auto& a : ed.assets) dirty |= a.town == town && a.dirty();
      Tile t = drawTile(ed, (dirty ? "* " : "") + town, selected, true, running, tile, cellW);
      if (t.clicked && env >= 0) ed.sel = {SelKind::Asset, env};
      if (t.doubleClicked) ed.projectFolder = town;
      if (t.hovered)
        ImGui::SetTooltip("%s\n%s\nDouble-click to open  |  right-click to run", (ed.assetsDir / town).string().c_str(),
                          env >= 0 ? ed.assets[env].detail.c_str() : "no environment file");
      if (ImGui::BeginPopupContextItem("##ctx")) {
        if (ImGui::MenuItem("Open")) ed.projectFolder = town;
        if (ImGui::MenuItem("Run Town", nullptr, false, env >= 0)) runTown(ed, town);
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Explorer")) showInExplorer(ed.assetsDir / town);
        ImGui::EndPopup();
      }
      ImGui::EndGroup();
      ImGui::PopID();
    }
    if (shown) ImGui::Spacing();

    bool hasLib = false;
    for (auto& a : ed.assets) hasLib |= a.library;
    if (hasLib) {
      shelfTitle(ed, "Library", "lib/: the defaults a town starts from with `import smallville`");
      ImGui::PushID("lib");
      Tile t = drawTile(ed, "lib", false, true, false, tile, cellW);
      if (t.doubleClicked) ed.projectFolder = "lib";
      if (t.hovered) ImGui::SetTooltip("%s\nDouble-click to open", (ed.assetsDir / "lib").string().c_str());
      ImGui::PopID();
      ImGui::Spacing();
    }

    // Anything still loose in games/ (from before towns were folders).
    shown = 0;
    for (size_t i = 0; i < ed.assets.size(); ++i) {
      if (ed.assets[i].library || !ed.assets[i].town.empty()) continue;
      if (shown == 0) shelfTitle(ed, "Loose files", "not in a town folder. Move each pair into games/NAME/ to make it a town.");
      place(shown);
      fileTile(ed, i, tile, cellW);
    }
  } else {
    // Inside a folder: its files.
    if (inLib) shelfTitle(ed, "Library", "the defaults towns import. Double-click to edit.");
    else shelfTitle(ed, "Town", "the environment (the world) and behavior (how residents act). Double-click either to run.");
    int shown = 0;
    for (size_t i = 0; i < ed.assets.size(); ++i) {
      if (ed.assets[i].town != ed.projectFolder) continue;
      place(shown);
      fileTile(ed, i, tile, cellW);
    }
    if (!inLib) {
      bool hasEnv = townEnvAsset(ed, ed.projectFolder) >= 0, hasBeh = false;
      for (auto& a : ed.assets) hasBeh |= a.town == ed.projectFolder && a.kind == "behavior";
      if (!hasEnv || !hasBeh) {
        ImGui::Spacing();
        ImGui::TextDisabled("This town is missing its %s file (%s%s).", !hasEnv ? "environment" : "behavior",
                            ed.projectFolder.c_str(), !hasEnv ? ".env.sl" : ".behavior.sl");
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

void drawScripts(EditorState& ed) {
  ed.focusedScript = -1;
  for (size_t i = 0; i < ed.assets.size(); ++i) {
    Asset& a = ed.assets[i];
    if (!a.scriptOpen) continue;
    std::string title = (a.town.empty() ? a.name : a.town + "/" + a.name) + "###script:" + a.path.string();
    if (ed.sceneDockId) ImGui::SetNextWindowDockID(ed.sceneDockId, ImGuiCond_FirstUseEver);
    if (a.focusScript) {
      ImGui::SetNextWindowFocus();
      // The dock space is built on the first frames; keep asking until it is.
      if (ImGui::GetFrameCount() > 3) a.focusScript = false;
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
      if (ImGui::SmallButton(isActive ? "Reload" : "Load")) {
        if (a.dirty()) saveAsset(ed, (int)i);
        loadAsset(ed, (int)i);
      }
      ImGui::SetItemTooltip("Save, then parse and load this script at round 0");
      ImGui::SameLine();
      ImGui::TextDisabled("%s", a.dirty() ? "unsaved  |  Ctrl+S to save" : "saved");
      ImGui::SameLine();
      ImGui::TextDisabled("|  %s file  |  Ctrl+Space: suggestions", a.kind.c_str());
      drawCode(ed, a);
    }
    ImGui::End();
    if (!open) a.scriptOpen = false;
  }
}

}  // namespace app
