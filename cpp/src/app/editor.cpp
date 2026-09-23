// Editor shell: assets, Play-mode semantics, menu bar, toolbar, status bar,
// the default dock layout, and small widgets the panels share.
#include "app/editor.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <fstream>
#include <sstream>

#include "imgui_internal.h"

namespace app {

namespace fs = std::filesystem;

namespace {

const char* kNewScriptTemplate = R"(// A new SocialLang game. The language reference is DESIGN.md in the repo.
sim NewGame {
  agents: 4

  role Villager {
    team: "town"
    memory: recent(5)
    sees: none
    count: remainder
  }

  phase Day {
    for p in alive() {
      let mood = ask_choice(p, "How is your day going?", ["great", "fine", "rough"])
      broadcast(p, p.seat + " says their day is " + mood + ".")
    }
  }

  win_condition {
    if round >= 5 { return "done" }
  }

  loop {
    run Day
    let winner = check_win()
    if winner != null { return winner }
  }
}
)";

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  std::string s = ss.str();
  // Normalize CRLF so the editor buffer and the saved copy compare equal.
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}

fs::path exeDir() {
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return fs::path(buf).parent_path();
}

// Run from a checkout, the Project panel edits the repo's own games/; an
// installed copy uses the games/ shipped next to the exe.
fs::path resolveAssetsDir() {
  for (fs::path p = exeDir(); !p.empty(); p = p.parent_path()) {
    if (fs::exists(p / "games" / "mafia.sl") && fs::exists(p / "sociallang")) return p / "games";
    if (p == p.root_path()) break;
  }
  return exeDir() / "games";
}

// ---- Toolbar icon buttons, drawn rather than glyphs.

enum class Icon { Play, Pause, Step, FastForward, Sun, Moon, Gear };

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col) {
  switch (icon) {
    case Icon::Play:
      dl->AddTriangleFilled(ImVec2(c.x - s * 0.35f, c.y - s * 0.45f), ImVec2(c.x - s * 0.35f, c.y + s * 0.45f),
                            ImVec2(c.x + s * 0.45f, c.y), col);
      break;
    case Icon::Pause:
      dl->AddRectFilled(ImVec2(c.x - s * 0.38f, c.y - s * 0.42f), ImVec2(c.x - s * 0.1f, c.y + s * 0.42f), col);
      dl->AddRectFilled(ImVec2(c.x + s * 0.1f, c.y - s * 0.42f), ImVec2(c.x + s * 0.38f, c.y + s * 0.42f), col);
      break;
    case Icon::Step:
      dl->AddTriangleFilled(ImVec2(c.x - s * 0.42f, c.y - s * 0.42f), ImVec2(c.x - s * 0.42f, c.y + s * 0.42f),
                            ImVec2(c.x + s * 0.22f, c.y), col);
      dl->AddRectFilled(ImVec2(c.x + s * 0.24f, c.y - s * 0.42f), ImVec2(c.x + s * 0.42f, c.y + s * 0.42f), col);
      break;
    case Icon::FastForward:
      dl->AddTriangleFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.4f), ImVec2(c.x - s * 0.5f, c.y + s * 0.4f),
                            ImVec2(c.x, c.y), col);
      dl->AddTriangleFilled(ImVec2(c.x, c.y - s * 0.4f), ImVec2(c.x, c.y + s * 0.4f), ImVec2(c.x + s * 0.5f, c.y), col);
      break;
    case Icon::Sun: {
      dl->AddCircleFilled(c, s * 0.22f, col);
      for (int i = 0; i < 8; ++i) {
        float a = i * 3.14159265f / 4;
        dl->AddLine(ImVec2(c.x + std::cos(a) * s * 0.34f, c.y + std::sin(a) * s * 0.34f),
                    ImVec2(c.x + std::cos(a) * s * 0.48f, c.y + std::sin(a) * s * 0.48f), col, 1.5f);
      }
      break;
    }
    case Icon::Moon:
      dl->PathArcTo(c, s * 0.42f, 0.9f, 5.4f, 24);
      dl->PathArcTo(ImVec2(c.x + s * 0.2f, c.y - s * 0.12f), s * 0.3f, 5.1f, 1.2f, 24);
      dl->PathFillConcave(col);
      break;
    case Icon::Gear: {
      dl->AddCircle(c, s * 0.26f, col, 16, 1.8f);
      for (int i = 0; i < 6; ++i) {
        float a = i * 3.14159265f / 3;
        dl->AddLine(ImVec2(c.x + std::cos(a) * s * 0.3f, c.y + std::sin(a) * s * 0.3f),
                    ImVec2(c.x + std::cos(a) * s * 0.46f, c.y + std::sin(a) * s * 0.46f), col, 2.5f);
      }
      break;
    }
  }
}

// A Unity toolbar button: flat, highlighted blue while `on`.
bool iconButton(EditorState& ed, const char* id, Icon icon, bool on, bool enabled, const char* tooltip,
                ImVec2 size = ImVec2(30, 24)) {
  ImGui::BeginDisabled(!enabled);
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool pressed = ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Button);
  ImU32 bg = on ? ed.pal.accent
                : ImGui::GetColorU32(hovered && enabled ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, 3.0f);
  (void)base;
  ImU32 fg = on ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text, enabled ? 1.0f : 0.4f);
  drawIcon(dl, icon, ImVec2(p.x + size.x * 0.5f, p.y + size.y * 0.5f), size.y * 0.55f, fg);
  ImGui::EndDisabled();
  if (hovered && tooltip) ImGui::SetItemTooltip("%s", tooltip);
  return pressed && enabled;
}

// The app mark: an "S" on a dark tile, drawn at any size.
void drawLogo(ImDrawList* dl, ImVec2 p, float s) {
  dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), IM_COL32(24, 24, 27, 255), s * 0.22f);
  const float pi = 3.14159265f;
  ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
  float r = s * 0.17f;
  // One continuous stroke: the upper bowl runs counter-clockwise from its
  // upper-right terminal over the top to the midpoint, the lower bowl
  // clockwise from the midpoint around to its lower-left terminal.
  dl->PathArcTo(ImVec2(c.x, c.y - r), r, -pi * 0.2f, -pi * 1.5f, 24);
  dl->PathArcTo(ImVec2(c.x, c.y + r), r, -pi * 0.5f, pi * 0.8f, 24);
  dl->PathStroke(IM_COL32(245, 245, 245, 255), s * 0.13f);
}

}  // namespace

// ---- Assets

void notify(EditorState& ed, const std::string& msg) {
  ed.toast = msg;
  ed.toastUntil = ImGui::GetTime() + 4.0;
}

void refreshAssets(EditorState& ed) {
  std::vector<Asset> next;
  std::error_code ec;
  if (fs::exists(ed.assetsDir, ec)) {
    for (auto& entry : fs::directory_iterator(ed.assetsDir, ec)) {
      if (entry.path().extension() != ".sl") continue;
      Asset a;
      a.name = entry.path().filename().string();
      a.path = entry.path();
      a.saved = readFile(a.path);
      a.text = a.saved;
      // Keep unsaved edits and open tabs across a refresh.
      for (auto& old : ed.assets) {
        if (old.path == a.path) {
          a.text = old.text;
          a.scriptOpen = old.scriptOpen;
        }
      }
      next.push_back(std::move(a));
    }
  }
  // The two Smallville-engine games lead, then alphabetical.
  auto rank = [](const std::string& n) { return n == "smallville_mafia.sl" ? 0 : n == "smallville.sl" ? 1 : 2; };
  std::sort(next.begin(), next.end(), [&](const Asset& x, const Asset& y) {
    return rank(x.name) != rank(y.name) ? rank(x.name) < rank(y.name) : x.name < y.name;
  });
  std::string activePath = ed.activeAsset >= 0 && ed.activeAsset < (int)ed.assets.size()
                               ? ed.assets[ed.activeAsset].path.string()
                               : "";
  ed.assets = std::move(next);
  ed.activeAsset = -1;
  for (size_t i = 0; i < ed.assets.size(); ++i)
    if (ed.assets[i].path.string() == activePath) ed.activeAsset = static_cast<int>(i);
}

void loadAsset(EditorState& ed, int index) {
  if (index < 0 || index >= (int)ed.assets.size()) return;
  if (ed.villePopulation > 0 && ed.roundsPerSecond > 60) ed.roundsPerSecond = 4;
  ed.villePopulation = 0;
  ed.nameIndex.clear();
  ed.activeAsset = index;
  ed.playMode = false;
  ed.sim.load(ed.assets[index].text, ed.seed, ed.settings);
  ed.info = ed.sim.info();  // Play/Step check this status before the next frame refreshes it
  ed.sel = {};
  ed.fitView = true;
  ed.followLatest = true;
  ed.selectedLog = -1;
}

void loadVille(EditorState& ed, int population) {
  ed.villePopulation = population;
  ed.activeAsset = -1;
  ed.playMode = false;
  ed.sim.loadVille(population, ed.seed, ed.settings, ed.townSpec);
  ed.info = ed.sim.info();
  ed.nameIndex.clear();
  for (size_t i = 0; i < ed.info.agents.size(); ++i) ed.nameIndex[ed.info.agents[i].seat] = static_cast<int>(i);
  ed.sel = {};
  ed.fitView = true;
  ed.followLatest = true;
  ed.selectedLog = -1;
  if (ed.roundsPerSecond < 10) ed.roundsPerSecond = 30;
}

std::string villeClockForStep(long long step) {
  static const char* kDays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  long long minutes = 6 * 60 + step * ville::kSecondsPerStep / 60;
  int d = static_cast<int>(minutes / 1440), m = static_cast<int>(minutes % 1440);
  char buf[48];
  std::snprintf(buf, sizeof buf, "%s Feb %d, %d:%02d %s", kDays[d % 7], 13 + d, (m / 60) % 12 == 0 ? 12 : (m / 60) % 12,
                m % 60, m < 720 ? "am" : "pm");
  return buf;
}

bool saveAsset(EditorState& ed, int index) {
  if (index < 0 || index >= (int)ed.assets.size()) return false;
  Asset& a = ed.assets[index];
  std::ofstream out(a.path, std::ios::binary);
  if (!out) {
    notify(ed, "Could not save " + a.name);
    return false;
  }
  out << a.text;
  a.saved = a.text;
  notify(ed, "Saved " + a.name);
  return true;
}

void newScript(EditorState& ed) {
  std::error_code ec;
  fs::create_directories(ed.assetsDir, ec);
  fs::path p;
  for (int i = 0;; ++i) {
    p = ed.assetsDir / (i == 0 ? "NewGame.sl" : "NewGame" + std::to_string(i) + ".sl");
    if (!fs::exists(p)) break;
  }
  std::ofstream(p, std::ios::binary) << kNewScriptTemplate;
  refreshAssets(ed);
  for (size_t i = 0; i < ed.assets.size(); ++i) {
    if (ed.assets[i].path == p) {
      ed.assets[i].scriptOpen = true;
      ed.assets[i].focusScript = true;
      ed.sel = {SelKind::Asset, static_cast<int>(i)};
    }
  }
  notify(ed, "Created " + p.filename().string());
}

void initEditor(EditorState& ed) {
  ed.settings = sl::Settings::load();
  ed.settingsDraft = ed.settings;
  ed.assetsDir = resolveAssetsDir();
  ed.townSpecPath = (ed.assetsDir / "the_ville.town.json").string();
  ed.townSpec = ville::TownSpec::load(ed.townSpecPath);
  ed.dark = !ed.launch.light;
  ed.seed = ed.launch.seed;
  applyTheme(ed);
  refreshAssets(ed);
  int first = 0;
  for (size_t i = 0; i < ed.assets.size(); ++i)
    if (ed.assets[i].name == ed.launch.game) first = static_cast<int>(i);
  if (ed.launch.ville > 0) loadVille(ed, ed.launch.ville);
  else if (!ed.launch.game.empty() && !ed.assets.empty()) loadAsset(ed, first);
  else loadVille(ed, 25);  // The Ville is the default scenario
  if (ed.launch.toEnd) {
    ed.playMode = true;
    ed.sim.runToEnd();
  }
  for (int i = 0; i < ed.launch.steps; ++i) stepOnce(ed);
  ed.showSettings = ed.launch.settings;
}

// ---- Play mode (Unity semantics)

void enterPlay(EditorState& ed) {
  if (ed.info.status == SimStatus::CompileError) {
    notify(ed, "This script has errors -- fix them to run it.");
    return;
  }
  if (ed.info.status == SimStatus::Empty) return;
  ed.playMode = true;
  ed.followLatest = true;
  ed.sim.play(ed.roundsPerSecond);
}

void exitPlay(EditorState& ed) {
  ed.playMode = false;
  ed.sim.pause();
  ed.sim.reset();
  ed.followLatest = true;
}

void togglePause(EditorState& ed) {
  if (!ed.playMode) {
    if (ed.info.status == SimStatus::CompileError || ed.info.status == SimStatus::Empty) return;
    ed.playMode = true;  // armed: in play mode, paused
    return;
  }
  if (ed.info.playing) ed.sim.pause();
  else ed.sim.play(ed.roundsPerSecond);
}

void stepOnce(EditorState& ed) {
  if (ed.info.status == SimStatus::CompileError || ed.info.status == SimStatus::Empty) return;
  ed.playMode = true;
  ed.followLatest = true;
  ed.sim.step();
}

// ---- Helpers shared by panels

const Frame* viewedFrame(const EditorState& ed) {
  if (ed.frames.empty()) return nullptr;
  int i = std::clamp(ed.viewFrame, 0, (int)ed.frames.size() - 1);
  return ed.frames[i].get();
}

bool teamRevealed(const EditorState& ed, int agent) {
  if (!ed.info.hiddenRoles || ed.info.status == SimStatus::Done) return true;
  const Frame* f = ed.frames.empty() ? nullptr : ed.frames.back().get();
  return f && agent < (int)f->agents.size() && !f->agents[agent].alive;
}

int seatIndex(const EditorState& ed, const std::string& seat) {
  if (!ed.nameIndex.empty()) {
    auto it = ed.nameIndex.find(seat);
    return it == ed.nameIndex.end() ? -1 : it->second;
  }
  if (seat.size() < 2 || seat[0] != 'P') return -1;
  int n = 0;
  for (size_t i = 1; i < seat.size(); ++i) {
    if (seat[i] < '0' || seat[i] > '9') return -1;
    n = n * 10 + (seat[i] - '0');
  }
  return n >= 1 && n <= (int)ed.info.agents.size() ? n - 1 : -1;
}

ImU32 teamColor(const EditorState& ed, const std::string& team) {
  static const unsigned kDark[] = {0x5AA0E6, 0xE6655A, 0x6FC46F, 0xE6C25A, 0xB77EE6, 0x4FD1C5};
  static const unsigned kLight[] = {0x1F5FA8, 0xB3261E, 0x2E7D32, 0x8A6100, 0x6A3FB0, 0x00796B};
  size_t h = std::hash<std::string>()(team) % 6;
  unsigned rgb = ed.dark ? kDark[h] : kLight[h];
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

ImU32 logKindColor(const EditorState& ed, sl::LogKind kind) {
  using sl::LogKind;
  switch (kind) {
    case LogKind::Dialogue: return ed.pal.talk;
    case LogKind::Reflection: return ed.pal.warning;
    case LogKind::Error: return ed.pal.error;
    case LogKind::Whisper: return ed.dark ? IM_COL32(183, 126, 230, 255) : IM_COL32(106, 63, 176, 255);
    case LogKind::Broadcast: return ImGui::GetColorU32(ImGuiCol_Text);
    default: return ed.pal.dim;
  }
}

void sectionHeader(EditorState& ed, const char* label) {
  ImGui::Spacing();
  ImGui::PushFont(ed.fonts.bold, 0.0f);
  ImGui::TextUnformatted(label);
  ImGui::PopFont();
  ImGui::Separator();
}

void propertyRow(const char* label, const char* fmt, ...) {
  float labelW = std::max(96.0f, ImGui::GetContentRegionAvail().x * 0.38f);
  float x0 = ImGui::GetCursorPosX();
  ImGui::TextDisabled("%s", label);
  ImGui::SameLine(x0 + labelW);
  va_list args;
  va_start(args, fmt);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextV(fmt, args);
  ImGui::PopTextWrapPos();
  va_end(args);
}

void kindIcon(EditorState& ed, sl::LogKind kind, float size) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size, size));
  kindIconAt(ed, kind, p, size);
}

bool beginPanel(const char* name, bool* open, ImGuiWindowFlags flags) {
  ImGuiWindowClass wc;
  wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton;
  ImGui::SetNextWindowClass(&wc);
  return ImGui::Begin(name, open, flags);
}

void parseLaunchArgs(EditorState& ed, int argc, wchar_t** argv) {
  auto narrow = [](const wchar_t* w) {
    std::string s;
    for (; *w; ++w) s += static_cast<char>(*w < 128 ? *w : '?');
    return s;
  };
  for (int i = 1; i < argc; ++i) {
    std::string a = narrow(argv[i]);
    auto next = [&] { return i + 1 < argc ? narrow(argv[++i]) : std::string(); };
    if (a == "--game") ed.launch.game = next();
    else if (a == "--seed") ed.launch.seed = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
    else if (a == "--steps") ed.launch.steps = std::atoi(next().c_str());
    else if (a == "--select") ed.launch.select = next();
    else if (a == "--to-end") ed.launch.toEnd = true;
    else if (a == "--light") ed.launch.light = true;
    else if (a == "--settings") ed.launch.settings = true;
    else if (a == "--ville") ed.launch.ville = std::atoi(next().c_str());
    else if (a == "--building") ed.launch.building = next();
  }
}

void kindIconAt(EditorState& ed, sl::LogKind kind, ImVec2 p, float size) {
  using sl::LogKind;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 col = logKindColor(ed, kind);
  ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
  float r = size * 0.42f;
  switch (kind) {
    case LogKind::Dialogue:  // speech bubble
      dl->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.8f), ImVec2(c.x + r, c.y + r * 0.45f), col, r * 0.4f);
      dl->AddTriangleFilled(ImVec2(c.x - r * 0.5f, c.y + r * 0.4f), ImVec2(c.x - r * 0.05f, c.y + r * 0.4f),
                            ImVec2(c.x - r * 0.6f, c.y + r * 0.95f), col);
      break;
    case LogKind::Reflection:  // four-point star
      dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.3f, c.y), ImVec2(c.x, c.y + r),
                        ImVec2(c.x - r * 0.3f, c.y), col);
      dl->AddQuadFilled(ImVec2(c.x - r, c.y), ImVec2(c.x, c.y - r * 0.3f), ImVec2(c.x + r, c.y),
                        ImVec2(c.x, c.y + r * 0.3f), col);
      break;
    case LogKind::Plan:  // list lines
      for (int i = 0; i < 3; ++i)
        dl->AddLine(ImVec2(c.x - r * 0.8f, c.y - r * 0.55f + i * r * 0.55f),
                    ImVec2(c.x + r * 0.8f, c.y - r * 0.55f + i * r * 0.55f), col, 1.6f);
      break;
    case LogKind::Error:  // red disc with "!"
      dl->AddCircleFilled(c, r, col);
      dl->AddLine(ImVec2(c.x, c.y - r * 0.5f), ImVec2(c.x, c.y + r * 0.1f), IM_COL32(255, 255, 255, 255), 2.0f);
      dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.45f), 1.3f, IM_COL32(255, 255, 255, 255));
      break;
    case LogKind::Whisper:  // two small dots
      dl->AddCircleFilled(ImVec2(c.x - r * 0.4f, c.y), r * 0.3f, col);
      dl->AddCircleFilled(ImVec2(c.x + r * 0.4f, c.y), r * 0.3f, col);
      break;
    case LogKind::Broadcast:  // disc with a ring
      dl->AddCircleFilled(c, r * 0.35f, col);
      dl->AddCircle(c, r * 0.85f, col, 16, 1.3f);
      break;
    default:  // ask / note / print: a small chevron
      dl->AddLine(ImVec2(c.x - r * 0.4f, c.y - r * 0.6f), ImVec2(c.x + r * 0.3f, c.y), col, 1.6f);
      dl->AddLine(ImVec2(c.x + r * 0.3f, c.y), ImVec2(c.x - r * 0.4f, c.y + r * 0.6f), col, 1.6f);
      break;
  }
}

void agentIcon(EditorState& ed, int agent, float size) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size, size));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
  float r = size * 0.32f;
  const Frame* f = viewedFrame(ed);
  bool alive = !f || agent >= (int)f->agents.size() || f->agents[agent].alive;
  if (!alive) {
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ed.pal.dead, 1.8f);
    dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), ed.pal.dead, 1.8f);
    return;
  }
  ImU32 col = teamRevealed(ed, agent) && ed.info.hiddenRoles ? teamColor(ed, ed.info.agents[agent].team) : ed.pal.agent;
  dl->AddCircleFilled(c, r, col);
  dl->AddCircle(c, r, ed.pal.agentOutline, 12, 1.0f);
}

void locationIcon(EditorState& ed, float size) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size, size));
  float r = size * 0.3f;
  ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
  ImGui::GetWindowDrawList()->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ed.pal.location, 1.0f, 0,
                                      1.5f);
}

void scriptIcon(EditorState& ed, ImVec2 p, float s, bool active) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float w = s * 0.72f, fold = s * 0.2f;
  ImVec2 a(p.x + (s - w) * 0.5f, p.y + s * 0.06f), b(a.x + w, p.y + s * 0.94f);
  ImU32 paper = ed.dark ? IM_COL32(222, 222, 222, 255) : IM_COL32(250, 250, 250, 255);
  dl->PathLineTo(a);
  dl->PathLineTo(ImVec2(b.x - fold, a.y));
  dl->PathLineTo(ImVec2(b.x, a.y + fold));
  dl->PathLineTo(b);
  dl->PathLineTo(ImVec2(a.x, b.y));
  dl->PathFillConvex(paper);
  dl->AddTriangleFilled(ImVec2(b.x - fold, a.y), ImVec2(b.x, a.y + fold), ImVec2(b.x - fold, a.y + fold),
                        IM_COL32(160, 160, 160, 255));
  ImU32 band = active ? ed.pal.accent : IM_COL32(90, 140, 90, 255);
  dl->AddRectFilled(ImVec2(a.x, b.y - s * 0.3f), ImVec2(b.x, b.y - s * 0.08f), band);
  float fs = s * 0.2f;
  const char* t = "SL";
  ImVec2 ts = ed.fonts.bold->CalcTextSizeA(fs, FLT_MAX, 0, t);
  dl->AddText(ed.fonts.bold, fs, ImVec2(a.x + (w - ts.x) * 0.5f, b.y - s * 0.3f + (s * 0.22f - ts.y) * 0.5f),
              IM_COL32(255, 255, 255, 255), t);
}

// ---- Shell

namespace {

void drawMenuBar(EditorState& ed) {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Script", "Ctrl+N")) newScript(ed);
    if (ImGui::MenuItem("Save", "Ctrl+S", false, ed.focusedScript >= 0)) saveAsset(ed, ed.focusedScript);
    if (ImGui::MenuItem("Save All")) {
      for (size_t i = 0; i < ed.assets.size(); ++i)
        if (ed.assets[i].dirty()) saveAsset(ed, static_cast<int>(i));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reveal Games Folder"))
      ShellExecuteW(nullptr, L"open", ed.assetsDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (ImGui::MenuItem("Refresh", "Ctrl+R")) refreshAssets(ed);
    ImGui::Separator();
    if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Play", "Ctrl+P", ed.playMode)) ed.playMode ? exitPlay(ed) : enterPlay(ed);
    if (ImGui::MenuItem("Pause", "Ctrl+Shift+P", ed.playMode && !ed.info.playing)) togglePause(ed);
    if (ImGui::MenuItem("Step", "Ctrl+Alt+P")) stepOnce(ed);
    ImGui::Separator();
    if (ImGui::MenuItem("Model Settings...")) {
      ed.settingsDraft = ed.settings;
      ed.showSettings = true;
    }
    if (ImGui::MenuItem(ed.dark ? "Light Theme" : "Dark Theme")) {
      ed.dark = !ed.dark;
      applyTheme(ed);
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Window")) {
    ImGui::MenuItem("Town", nullptr, &ed.showHierarchy);
    ImGui::MenuItem("World", nullptr, &ed.showScene);
    ImGui::MenuItem("Inspector", nullptr, &ed.showInspector);
    ImGui::MenuItem("Rules", nullptr, &ed.showRules);
    ImGui::MenuItem("Scenarios", nullptr, &ed.showProject);
    ImGui::MenuItem("Activity", nullptr, &ed.showConsole);
    ImGui::Separator();
    if (ImGui::MenuItem("Reset Layout")) {
      ed.resetLayout = true;
      ed.showHierarchy = ed.showScene = ed.showInspector = ed.showProject = ed.showConsole = true;
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    if (ImGui::MenuItem("About SocialSandbox")) ed.showAbout = true;
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void drawToolbar(EditorState& ed) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar * 0;
  float h = 36.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  if (ImGui::BeginViewportSideBar("##Toolbar", vp, ImGuiDir_Up, h, flags)) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ed.playMode) {  // Unity tints the editor while playing
      ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
      dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ed.pal.playTint);
    }

    // Left: logo, game picker
    ImVec2 lp = ImGui::GetCursorScreenPos();
    drawLogo(dl, lp, 24);
    ImGui::Dummy(ImVec2(24, 24));
    ImGui::SameLine(0, 10);
    ImGui::SetNextItemWidth(210);
    std::string current = ed.villePopulation > 0 ? "The Ville - " + std::to_string(ed.villePopulation) + " residents"
                          : ed.activeAsset >= 0 ? ed.assets[ed.activeAsset].name
                                                : "No scenario loaded";
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1);
    if (ImGui::BeginCombo("##game", current.c_str())) {
      ImGui::TextDisabled("The Ville (Generative Agents)");
      for (int pop : {25, 250, 1000, 5000}) {
        std::string label = "The Ville - " + std::to_string(pop) + " residents" + (pop == 25 ? " (the paper's cast)" : "");
        if (ImGui::Selectable(label.c_str(), ed.villePopulation == pop)) loadVille(ed, pop);
      }
      ImGui::Separator();
      ImGui::TextDisabled("SocialLang games");
      for (size_t i = 0; i < ed.assets.size(); ++i)
        if (ImGui::Selectable(ed.assets[i].name.c_str(), (int)i == ed.activeAsset)) loadAsset(ed, (int)i);
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Scenario loaded into the World view");

    // Center: Play / Pause / Step, then run-to-end
    float groupW = 30 * 3 + 2 * 2 + 12 + 30;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12, (ImGui::GetWindowWidth() - groupW) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
    bool canRun = ed.info.status != SimStatus::CompileError && ed.info.status != SimStatus::Empty;
    if (iconButton(ed, "##play", Icon::Play, ed.playMode, canRun, ed.playMode ? "Stop (Ctrl+P) - resets to round 0" : "Play (Ctrl+P)"))
      ed.playMode ? exitPlay(ed) : enterPlay(ed);
    ImGui::SameLine();
    if (iconButton(ed, "##pause", Icon::Pause, ed.playMode && !ed.info.playing, canRun, "Pause (Ctrl+Shift+P)"))
      togglePause(ed);
    ImGui::SameLine();
    bool canStep = canRun && ed.info.status != SimStatus::Done && ed.info.status != SimStatus::RuntimeError;
    if (iconButton(ed, "##step", Icon::Step, false, canStep, "Step one round (Ctrl+Alt+P)")) stepOnce(ed);
    ImGui::SameLine(0, 12);
    if (iconButton(ed, "##toend", Icon::FastForward, false, canStep,
                   ed.villePopulation > 0 ? "Fast-forward: simulate as fast as the machine allows" : "Run to the end as fast as possible")) {
      ed.playMode = true;
      ed.followLatest = true;
      ed.sim.runToEnd();
    }
    ImGui::PopStyleVar();
    if (ed.info.isVille) {  // the game clock, right of the transport
      ImGui::SameLine(0, 14);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
      ImGui::PushFont(ed.fonts.bold, 0.0f);
      ImGui::TextUnformatted(ed.info.clock.c_str());
      ImGui::PopFont();
    }

    // Right: speed, models, theme
    float rightW = 190 + 8 + 130 + 8 + 30;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16, ImGui::GetWindowWidth() - rightW - 8));
    ImGui::SetNextItemWidth(190);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1);
    bool changedSpeed;
    if (ed.info.isVille) {
      // A step is 10 game-seconds, so steps/s * 10 = game-seconds per second.
      float gameMinPerSec = ed.roundsPerSecond / 6.0f;
      changedSpeed = ImGui::SliderFloat("##speed", &gameMinPerSec, 0.2f, 200.0f, "%.1f game-min / s", ImGuiSliderFlags_Logarithmic);
      if (changedSpeed) ed.roundsPerSecond = gameMinPerSec * 6.0f;
    } else {
      changedSpeed = ImGui::SliderFloat("##speed", &ed.roundsPerSecond, 0.5f, 60.0f, "%.1f rounds/s", ImGuiSliderFlags_Logarithmic);
    }
    if (changedSpeed) ed.sim.setSpeed(ed.roundsPerSecond);
    ImGui::SetItemTooltip("Play speed");
    ImGui::SameLine(0, 8);
    int enabled = 0;
    std::string single;
    for (auto& m : ed.settings.roster)
      if (m.enabled) {
        ++enabled;
        single = m.name;
      }
    std::string modelsLabel = enabled == 0 ? "Mock" : enabled == 1 ? single : std::to_string(enabled) + " models";
    if (modelsLabel.size() > 16) modelsLabel = modelsLabel.substr(0, 15) + "...";
    if (ImGui::Button((modelsLabel + "##models").c_str(), ImVec2(130, 24))) {
      ed.settingsDraft = ed.settings;
      ed.showSettings = true;
    }
    ImGui::SetItemTooltip("Model Settings: API keys, local LLMs, which models play");
    ImGui::SameLine(0, 8);
    if (iconButton(ed, "##theme", ed.dark ? Icon::Sun : Icon::Moon, false, true,
                   ed.dark ? "Switch to the light theme" : "Switch to the dark theme")) {
      ed.dark = !ed.dark;
      applyTheme(ed);
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

const char* statusText(SimStatus s) {
  switch (s) {
    case SimStatus::Empty: return "No game";
    case SimStatus::CompileError: return "Compile error";
    case SimStatus::Ready: return "Ready";
    case SimStatus::Paused: return "Paused";
    case SimStatus::Running: return "Running";
    case SimStatus::Done: return "Finished";
    case SimStatus::RuntimeError: return "Runtime error";
  }
  return "";
}

void drawStatusBar(EditorState& ed) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  if (ImGui::BeginViewportSideBar("##StatusBar", vp, ImGuiDir_Down, 24, ImGuiWindowFlags_NoScrollbar)) {
    // Left: what just happened
    if (ed.info.busy) {  // a round in flight, e.g. waiting on a model
      ImVec2 p = ImGui::GetCursorScreenPos();
      float t = static_cast<float>(ImGui::GetTime());
      ImGui::GetWindowDrawList()->PathArcTo(ImVec2(p.x + 7, p.y + 8), 5, t * 6, t * 6 + 4.2f, 12);
      ImGui::GetWindowDrawList()->PathStroke(ed.pal.accent, 2.0f);
      ImGui::Dummy(ImVec2(14, 14));
      ImGui::SameLine();
    }
    std::string left;
    ImU32 leftCol = ImGui::GetColorU32(ImGuiCol_Text);
    if (ImGui::GetTime() < ed.toastUntil) {
      left = ed.toast;
    } else if (ed.info.status == SimStatus::CompileError || ed.info.status == SimStatus::RuntimeError) {
      left = ed.info.error;
      leftCol = ed.pal.error;
    } else if (!ed.log.empty()) {
      const auto& e = ed.log.back();
      left = (e.author.empty() ? "" : e.author + ": ") + e.text;
    }
    std::replace(left.begin(), left.end(), '\n', ' ');
    ImGui::PushStyleColor(ImGuiCol_Text, leftCol);
    float rightW = 520;
    ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - rightW, ImGui::GetWindowPos().y + 24),
                        true);
    ImGui::TextUnformatted(left.c_str());
    ImGui::PopClipRect();
    ImGui::PopStyleColor();

    // Right: run stats
    char right[256];
    int alive = 0;
    if (!ed.frames.empty())
      for (auto& a : ed.frames.back()->agents) alive += a.alive;
    bool anyReal = false;
    for (auto& m : ed.settings.roster) anyReal = anyReal || (m.enabled && m.kind != sl::ProviderKind::Mock);
    if (ed.info.isVille)
      std::snprintf(right, sizeof right, "%s  |  %s  |  %zu residents  |  %lld model calls  %lld err  |  %s  |  %.0f fps",
                    statusText(ed.info.status), ed.info.clock.c_str(), ed.info.agents.size(), ed.info.calls, ed.info.errors,
                    anyReal ? "LLM cognition" : "Offline persona model (no LLM)", ImGui::GetIO().Framerate);
    else
      std::snprintf(right, sizeof right, "%s  |  Round %d  |  %d/%zu alive  |  %lld calls  %lld tok  %lld err  |  %s  |  %.0f fps",
                    statusText(ed.info.status), ed.info.round, alive, ed.info.agents.size(), ed.info.calls,
                    ed.info.promptTokens + ed.info.completionTokens, ed.info.errors,
                    anyReal ? "Live models" : "Mock (no LLM)", ImGui::GetIO().Framerate);
    float w = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - w - 10);
    ImGui::TextDisabled("%s", right);
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void buildDefaultLayout(EditorState& ed, ImGuiID dockId) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockId);
  ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);
  ImGuiID main = dockId, right, bottom, left;
  right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.24f, nullptr, &main);
  bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.30f, nullptr, &main);
  left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.22f, nullptr, &main);
  ImGui::DockBuilderDockWindow("Town", left);
  ImGui::DockBuilderDockWindow("World", main);
  ImGui::DockBuilderDockWindow("Rules", right);
  ImGui::DockBuilderDockWindow("Inspector", right);  // docked last: the tab in front
  ImGui::DockBuilderDockWindow("Scenarios", bottom);
  ImGui::DockBuilderDockWindow("Activity", bottom);
  ImGui::DockBuilderFinish(dockId);
  ed.sceneDockId = main;
}

void drawAbout(EditorState& ed) {
  if (ed.showAbout) {
    ImGui::OpenPopup("About SocialSandbox");
    ed.showAbout = false;
  }
  ImGui::SetNextWindowSize(ImVec2(460, 0));
  if (ImGui::BeginPopupModal("About SocialSandbox", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    drawLogo(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), 40);
    ImGui::Dummy(ImVec2(40, 40));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(ed.fonts.bold, 0.0f);
    ImGui::TextUnformatted("SocialSandbox");
    ImGui::PopFont();
    ImGui::TextDisabled("Native C++ editor for SocialLang simulations");
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Simulates Generative Agents towns (Park et al. 2023) at scale, and runs SocialLang .sl games. "
        "Offline, residents think with a persona-driven model and .sl agents with a random mock; enable real "
        "models (your own API keys) or a local LLM (Ollama, LM Studio) in Model Settings.");
    ImGui::Spacing();
    ImGui::TextDisabled("Dear ImGui %s  |  %s", IMGUI_VERSION, ed.assetsDir.string().c_str());
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(-1, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void handleShortcuts(EditorState& ed) {
  using namespace ImGui;
  if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) ed.playMode ? exitPlay(ed) : enterPlay(ed);
  if (Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) togglePause(ed);
  if (Shortcut(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) stepOnce(ed);
  if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal) && ed.focusedScript >= 0)
    saveAsset(ed, ed.focusedScript);
  if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal)) newScript(ed);
  if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, ImGuiInputFlags_RouteGlobal)) refreshAssets(ed);
}

}  // namespace

void drawEditor(EditorState& ed) {
  // Pull what the worker published since last frame.
  if (ed.sim.drain(ed.log, ed.frames)) {
    ed.selectedLog = -1;
    ed.consoleClearedAt = 0;
    ed.viewFrame = 0;
  }
  ed.info = ed.sim.info();
  if (ed.frames.size() > 6000) {
    size_t drop = ed.frames.size() - 4000;
    ed.frames.erase(ed.frames.begin(), ed.frames.begin() + static_cast<long long>(drop));
    ed.viewFrame = std::max(0, ed.viewFrame - static_cast<int>(drop));
  }
  if (!ed.launch.building.empty() && ed.info.villeWorld) {
    int s = ed.info.villeWorld->findSector(ed.launch.building);
    if (s >= 0) ed.sel = {SelKind::Building, s};
    ed.launch.building.clear();
  }
  if (!ed.launch.select.empty() && !ed.info.agents.empty()) {  // --select, once the run exists
    int a = seatIndex(ed, ed.launch.select);
    if (a >= 0) {
      ed.sel = {SelKind::Agent, a};
      ed.frameSelection = true;
    }
    ed.launch.select.clear();
  }
  if (ed.followLatest && !ed.frames.empty()) ed.viewFrame = static_cast<int>(ed.frames.size()) - 1;
  // A finished or failed run drops back to "paused in play mode".
  handleShortcuts(ed);

  drawMenuBar(ed);
  drawToolbar(ed);
  drawStatusBar(ed);

  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGuiID dockId = ImGui::GetID("SocialSandboxDock.v4");
  if (ed.resetLayout || !ImGui::DockBuilderGetNode(dockId)) {
    buildDefaultLayout(ed, dockId);
    ed.resetLayout = false;
  }
  ImGui::DockSpaceOverViewport(dockId, vp);

  if (ed.showHierarchy) drawHierarchy(ed);
  if (ed.showScene) drawScene(ed);
  if (ed.showInspector) drawInspector(ed);
  if (ed.showProject) drawProject(ed);
  if (ed.showConsole) drawConsole(ed);
  drawScripts(ed);
  drawRules(ed);
  drawModelSettings(ed);
  drawAbout(ed);
}

}  // namespace app
