// Editor state shared by every panel, plus the panel entry points. Layout
// follows the Unity editor: Hierarchy (left), Scene + script tabs (center),
// Inspector (right), Project + Console (bottom), Play / Pause / Step in a
// toolbar at the top center.
#pragma once

#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/sim_controller.h"
#include "ville/spec.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace app {

struct ScriptView;  // the script tab's code editor (panels_project.cpp)

struct Asset {
  std::string name;  // file name, e.g. "smallville.sl"
  std::filesystem::path path;
  std::string text;   // editor buffer
  std::string saved;  // last saved / loaded contents
  std::string kind;   // "environment" or "behavior"
  bool library = false;  // in games/lib: imported by other files
  std::string town;      // the town folder it lives in (games/NAME/); "lib"; "" if loose in games/
  std::string detail; // environment files: "25 residents, oakhill.behavior.sl"
  bool scriptOpen = false;
  bool focusScript = false;
  std::shared_ptr<ScriptView> view;
  bool dirty() const { return text != saved; }
};

enum class SelKind { None, Sim, Agent, Location, Asset, Building };

struct Selection {
  SelKind kind = SelKind::None;
  int index = -1;
  bool operator==(const Selection& o) const { return kind == o.kind && index == o.index; }
};

struct Fonts {
  ImFont* ui = nullptr;
  ImFont* bold = nullptr;
  ImFont* mono = nullptr;
};

// Colors that aren't ImGui style colors: the Scene view and log-kind accents.
struct Palette {
  ImU32 sceneBg, gridMinor, gridMajor, worldBorder;
  ImU32 agent, agentOutline, dead, location, locationLabel, label;
  ImU32 talk, select, gizmoX, gizmoY;
  ImU32 dim, accent, error, warning, playTint;
};

struct ModelTest {
  std::future<std::string> result;
  std::string text;
  bool running = false;
};

struct EditorState {
  SimController sim;
  sl::Settings settings;
  sl::Settings settingsDraft;  // edited in Model Settings until Save
  Fonts fonts;
  Palette pal;
  bool dark = true;

  // Assets (the Project panel): every .sl file in the games folder.
  std::filesystem::path assetsDir;
  std::vector<Asset> assets;
  int activeAsset = -1;  // loaded into the World view
  int townAsset = -1;  // the environment file loaded as a town, or -1
  std::unordered_map<std::string, int> nameIndex;  // resident name -> index (towns)
  ville::TownSpec townSpec;  // the loaded town's environment + behavior files, parsed
  bool showRules = true;
  int rulesScope = 0;          // 0 whole town, 1 a group, 2 one resident
  std::string rulesGroup, rulesResident;
  int buildingScope = 0;       // 0 this building, 1 every building of its type, 2 every building
  uint32_t seed = 1;

  // Latest snapshot from the worker, refreshed once per frame.
  SimInfo info;
  std::vector<sl::LogEntry> log;
  std::vector<std::shared_ptr<const Frame>> frames;
  int viewFrame = 0;         // index into frames shown in the Scene
  bool followLatest = true;  // track the newest frame as rounds arrive

  // Play mode, Unity-style: Play enters (and runs), Play again exits and
  // resets to round 0; Pause holds inside play mode; Step advances one round.
  bool playMode = false;
  float roundsPerSecond = 4.0f;

  Selection sel;
  bool frameSelection = false;  // Scene: center the camera on sel next frame

  // Scene camera, in world units
  float zoom = 1.0f;
  ImVec2 camCenter{0, 0};
  bool fitView = true;
  bool showLabels = true, showTalk = true, showGizmos = true;

  // Console
  bool showKind[9] = {true, true, true, true, true, true, true, true, true};
  std::string consoleSearch;
  size_t consoleClearedAt = 0;
  int selectedLog = -1;
  bool consoleAutoScroll = true;

  // Windows
  bool showHierarchy = true, showScene = true, showInspector = true, showProject = true, showConsole = true;
  bool showSettings = false, showAbout = false;
  bool resetLayout = false;
  unsigned sceneDockId = 0;
  int focusedScript = -1;  // asset index of the focused script tab (Ctrl+S target)
  float projectTileSize = 72.0f;
  std::string projectFolder;  // Scenarios panel: "" the games folder, else a town folder or "lib"
  std::vector<ModelTest> modelTests;

  // Command-line launch options (for scripted checks and shortcuts):
  //   --game <file.sl> --seed N --steps N --to-end --select P3 --light
  struct Launch {
    std::string game, select;
    int steps = 0;
    bool toEnd = false, light = false, settings = false;
    std::string town;  // --town FILE.env.sl
    std::string edit;  // --edit FILE.sl: open it in a script tab
    std::string building;  // --building NAME: open it in the Inspector
    uint32_t seed = 1;
  } launch;

  // Transient status-bar message
  std::string toast;
  double toastUntil = 0;
};

// editor.cpp
void initEditor(EditorState& ed);
void drawEditor(EditorState& ed);
void applyTheme(EditorState& ed);
void loadAsset(EditorState& ed, int index);
// Load an environment file (and the behavior file it names) as a town.
bool loadTown(EditorState& ed, int index);
// Write the edited townSpec back to its two files and rebuild the town.
void saveTown(EditorState& ed);
std::string villeClockForStep(const EditorState& ed, long long step);
bool saveAsset(EditorState& ed, int index);
void refreshAssets(EditorState& ed);
void newScript(EditorState& ed);
// The environment file of a town folder, as an asset index (-1 if none).
int townEnvAsset(const EditorState& ed, const std::string& town);
void notify(EditorState& ed, const std::string& msg);
const Frame* viewedFrame(const EditorState& ed);
bool teamRevealed(const EditorState& ed, int agent);
// "P12" -> 11; -1 for anything that isn't one of this run's seats.
int seatIndex(const EditorState& ed, const std::string& seat);
ImU32 teamColor(const EditorState& ed, const std::string& team);
ImU32 logKindColor(const EditorState& ed, sl::LogKind kind);
void enterPlay(EditorState& ed);
void exitPlay(EditorState& ed);
void togglePause(EditorState& ed);
void stepOnce(EditorState& ed);

// Panels
void drawHierarchy(EditorState& ed);
void drawScene(EditorState& ed);
void drawInspector(EditorState& ed);
void drawConsole(EditorState& ed);
void drawProject(EditorState& ed);
void drawScripts(EditorState& ed);
void drawModelSettings(EditorState& ed);
void drawRules(EditorState& ed);

// Town customization (panels_town_edit.cpp)
void inspectBuilding(EditorState& ed, int sector);
void editResident(EditorState& ed, int agent);
void applyTownEdits(EditorState& ed, const char* what);

// Small shared widgets (editor.cpp)
void sectionHeader(EditorState& ed, const char* label);
void propertyRow(const char* label, const char* fmt, ...);
void kindIcon(EditorState& ed, sl::LogKind kind, float size);
void kindIconAt(EditorState& ed, sl::LogKind kind, ImVec2 pos, float size);
// Begin() for a dockable panel: no dock-node close/menu buttons (the tab's
// own close button and the Window menu cover that), like Unity's tabs.
bool beginPanel(const char* name, bool* open, ImGuiWindowFlags flags = 0);
void parseLaunchArgs(EditorState& ed, int argc, wchar_t** argv);
void agentIcon(EditorState& ed, int agent, float size);
void locationIcon(EditorState& ed, float size);
void scriptIcon(EditorState& ed, ImVec2 pos, float size, bool active);
void folderIcon(EditorState& ed, ImVec2 pos, float size, bool active);

}  // namespace app
