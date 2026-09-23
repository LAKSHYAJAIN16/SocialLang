// Scene view: the world as a Unity-style 2D scene. Grid with major/minor
// lines, the world's bounds, locations as square gizmos, agents as discs
// (team-colored once revealed, an X once eliminated), conversation lines that
// fade over a few rounds, and the selection drawn with Unity's orange outline
// and move-gizmo arrows. Right/middle-drag or drag empty space to pan, wheel
// to zoom, F to frame the selection.
#include <algorithm>
#include <cmath>

#include "app/editor.h"
#include "imgui_internal.h"
#include "ville/world.h"

namespace app {

namespace {

constexpr float kRingWorld = 100.0f;  // coordinate space for games without world{}

struct View {
  ImVec2 origin, size;  // canvas rect
  float scale;          // pixels per world unit
  ImVec2 center;        // world point at canvas center
  ImVec2 toScreen(float wx, float wy) const {
    return ImVec2(origin.x + size.x * 0.5f + (wx - center.x) * scale, origin.y + size.y * 0.5f + (wy - center.y) * scale);
  }
  ImVec2 toWorld(ImVec2 s) const {
    return ImVec2(center.x + (s.x - origin.x - size.x * 0.5f) / scale, center.y + (s.y - origin.y - size.y * 0.5f) / scale);
  }
};

ImVec2 worldSize(const SimInfo& info) {
  if (info.hasWorld && info.worldW > 0 && info.worldH > 0) return ImVec2((float)info.worldW, (float)info.worldH);
  return ImVec2(kRingWorld, kRingWorld);
}

// Where to draw each agent: its real position, a parking spot along the
// bottom edge until it's spawned, or a ring when the game has no world.
ImVec2 agentPos(const SimInfo& info, const Frame& f, int i) {
  const AgentLive& a = f.agents[i];
  int n = static_cast<int>(f.agents.size());
  if (!info.hasWorld || info.worldW <= 0) {
    float ang = (float)i / std::max(n, 1) * 6.2831853f - 1.5707963f;
    return ImVec2(kRingWorld * 0.5f + std::cos(ang) * kRingWorld * 0.36f,
                  kRingWorld * 0.5f + std::sin(ang) * kRingWorld * 0.36f);
  }
  if (a.hasPos) return ImVec2(a.x, a.y);
  return ImVec2((i + 0.5f) / std::max(n, 1) * info.worldW, info.worldH * 1.09f);
}

void drawGrid(ImDrawList* dl, const View& v, const EditorState& ed) {
  // Pick a world-unit step that keeps minor lines >= 14px apart.
  static const float kSteps[] = {0.5f, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};
  float step = kSteps[0];
  for (float s : kSteps) {
    step = s;
    if (s * v.scale >= 14) break;
  }
  ImVec2 w0 = v.toWorld(v.origin), w1 = v.toWorld(ImVec2(v.origin.x + v.size.x, v.origin.y + v.size.y));
  auto lineCol = [&](float w) {
    return std::fmod(std::fabs(w), step * 10) < step * 0.5f ? ed.pal.gridMajor : ed.pal.gridMinor;
  };
  for (float x = std::floor(w0.x / step) * step; x <= w1.x; x += step) {
    float sx = v.toScreen(x, 0).x;
    dl->AddLine(ImVec2(sx, v.origin.y), ImVec2(sx, v.origin.y + v.size.y), lineCol(x));
  }
  for (float y = std::floor(w0.y / step) * step; y <= w1.y; y += step) {
    float sy = v.toScreen(0, y).y;
    dl->AddLine(ImVec2(v.origin.x, sy), ImVec2(v.origin.x + v.size.x, sy), lineCol(y));
  }
}

void centeredText(EditorState& ed, const View& v, const char* text, ImU32 col) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float wrap = std::min(v.size.x - 40, 560.0f);
  ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, wrap);
  ImVec2 p(v.origin.x + (v.size.x - ts.x) * 0.5f, v.origin.y + (v.size.y - ts.y) * 0.5f);
  dl->AddRectFilled(ImVec2(p.x - 14, p.y - 10), ImVec2(p.x + ts.x + 14, p.y + ts.y + 10),
                    ImGui::GetColorU32(ImGuiCol_PopupBg, 0.92f), 4);
  dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), p, col, text, nullptr, wrap);
  (void)ed;
}

bool toggleChip(const char* label, bool* v) {
  ImGui::PushStyleColor(ImGuiCol_Button, *v ? ImGui::GetStyleColorVec4(ImGuiCol_Header)
                                            : ImGui::GetStyleColorVec4(ImGuiCol_Button));
  bool pressed = ImGui::SmallButton(label);
  ImGui::PopStyleColor();
  if (pressed) *v = !*v;
  return pressed;
}


// ---- The Ville: tile map, buildings, residents, bubbles.

ImU32 rgb(unsigned c, int a = 255) { return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, a); }

ImU32 floorColor(ville::SectorKind k) {
  using K = ville::SectorKind;
  switch (k) {
    case K::Home: return rgb(0xC49A6C);
    case K::Cafe: return rgb(0xD9B38C);
    case K::Pub: return rgb(0x8B6A47);
    case K::Store: return rgb(0xB8B8A8);
    case K::Market: return rgb(0xD8D8BC);
    case K::College: return rgb(0xC9B79C);
    case K::Dorm: return rgb(0xA9B8C8);
    case K::TownHall: return rgb(0xCFC6B0);
    case K::Office: return rgb(0xB4BCC6);
    case K::Park: return rgb(0x6B8F4E);
  }
  return rgb(0xC0C0C0);
}

ImU32 residentColor(const std::string& name) {
  static const unsigned kColors[] = {0xE4572E, 0x2E86AB, 0xF3A712, 0x6A994E, 0x9B5DE5, 0xF15BB5,
                                     0x00BBF9, 0xE07A5F, 0x3D405B, 0x81B29A, 0xC1121F, 0x1B998B};
  return rgb(kColors[std::hash<std::string>()(name) % 12]);
}

std::string shortText(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 3) + "..."; }

void pill(ImDrawList* dl, ImVec2 center, const char* text, ImU32 bg, ImU32 fg, float padX = 5, float padY = 2) {
  ImVec2 ts = ImGui::CalcTextSize(text);
  ImVec2 a(center.x - ts.x * 0.5f - padX, center.y - ts.y * 0.5f - padY), b(center.x + ts.x * 0.5f + padX, center.y + ts.y * 0.5f + padY);
  dl->AddRectFilled(a, b, bg, 4);
  dl->AddText(ImVec2(a.x + padX, a.y + padY), fg, text);
}

// Draws the town and its residents; returns the hovered resident (or -1) and
// sets hoverRoom to the room under the cursor.
int drawVille(EditorState& ed, ImDrawList* dl, const View& v, const Frame& frame, bool hovered, ImVec2 mouse,
              int& hoverRoom) {
  const SimInfo& info = ed.info;
  if (!info.villeWorld) return -1;
  const ville::World& w = *info.villeWorld;
  float ts = v.scale;  // pixels per tile
  ImVec2 w0 = v.toWorld(v.origin), w1 = v.toWorld(ImVec2(v.origin.x + v.size.x, v.origin.y + v.size.y));
  int x0 = std::max(0, (int)std::floor(w0.x)), y0 = std::max(0, (int)std::floor(w0.y));
  int x1 = std::min(w.width() - 1, (int)std::ceil(w1.x)), y1 = std::min(w.height() - 1, (int)std::ceil(w1.y));
  auto T = [&](float x, float y) { return v.toScreen(x, y); };

  if (ts >= 5.0f) {
    // Every visible tile.
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        ville::Tile t = w.tile(x, y);
        ImU32 c;
        switch (t) {
          case ville::Tile::Grass: c = rgb(0x6B8F4E); break;
          case ville::Tile::Road: c = rgb(0x8C8C84); break;
          case ville::Tile::Plaza: c = rgb(0xC8B894); break;
          case ville::Tile::Wall: {
            int s = w.sectorAt(x, y);
            c = s >= 0 && w.sectors[s].wallColor ? rgb(w.sectors[s].wallColor) : rgb(0x3A3A44);
            break;
          }
          case ville::Tile::Door: c = rgb(0x8A5A2B); break;
          case ville::Tile::Water: c = rgb(0x4A86C5); break;
          case ville::Tile::Tree: c = rgb(0x6B8F4E); break;
          default: {
            int s = w.sectorAt(x, y);
            c = s < 0 ? rgb(0xC0C0C0) : w.sectors[s].floorColor ? rgb(w.sectors[s].floorColor) : floorColor(w.sectors[s].kind);
          }
        }
        ImVec2 a = T((float)x, (float)y), b = T((float)x + 1, (float)y + 1);
        dl->AddRectFilled(a, ImVec2(b.x + 0.5f, b.y + 0.5f), c);
        if (t == ville::Tile::Tree) dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), ts * 0.45f, rgb(0x2F5F2A), 10);
      }
    // Floor tile seams inside buildings, once zoomed in.
    if (ts >= 14)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
          if (w.tile(x, y) == ville::Tile::Floor) {
            ImVec2 a = T((float)x, (float)y), b = T((float)x + 1, (float)y + 1);
            dl->AddRect(a, b, IM_COL32(0, 0, 0, 18));
          }
  } else {
    // Zoomed out: grass, the street grid, and each building as a block
    // (the street grid matches World::generate's 28x20 cells).
    dl->AddRectFilled(T(0, 0), T((float)w.width(), (float)w.height()), rgb(0x6B8F4E));
    for (int x = 0; x < w.width(); x += 28) dl->AddRectFilled(T((float)x, 0), T((float)x + 2, (float)w.height()), rgb(0x8C8C84));
    for (int y = 0; y < w.height(); y += 20) dl->AddRectFilled(T(0, (float)y), T((float)w.width(), (float)y + 2), rgb(0x8C8C84));
    for (auto& s : w.sectors) {
      ImVec2 a = T((float)s.rect.x, (float)s.rect.y), b = T((float)(s.rect.x + s.rect.w), (float)(s.rect.y + s.rect.h));
      dl->AddRectFilled(a, b, s.floorColor ? rgb(s.floorColor) : floorColor(s.kind));
      if (s.kind != ville::SectorKind::Park)
        dl->AddRect(a, b, s.wallColor ? rgb(s.wallColor) : rgb(0x3A3A44), 0, 0, std::max(1.0f, ts * 0.8f));
    }
  }

  // Furniture and in-use objects.
  if (ts >= 7) {
    for (size_t o = 0; o < w.objects.size(); ++o) {
      const ville::GameObject& obj = w.objects[o];
      if (obj.x < x0 || obj.x > x1 || obj.y < y0 || obj.y > y1) continue;
      bool busy = info.objectBusy && o < info.objectBusy->size() && (*info.objectBusy)[o];
      ImVec2 a = T(obj.x + 0.15f, obj.y + 0.15f), b = T(obj.x + 0.85f, obj.y + 0.85f);
      dl->AddRectFilled(a, b, rgb(0x6E5A48), ts * 0.12f);
      if (busy) dl->AddRect(a, b, rgb(0xF2C744), ts * 0.12f, 0, std::max(1.5f, ts * 0.08f));
      if (ts >= 26) {
        ImVec2 tsz = ImGui::CalcTextSize(obj.name.c_str());
        dl->AddText(ImVec2((a.x + b.x - tsz.x) * 0.5f, b.y + 1), IM_COL32(30, 30, 30, 200), obj.name.c_str());
      }
    }
  }

  // Room names (zoomed in) and building names.
  if (ts >= 13 && ed.showLabels)
    for (auto& ar : w.arenas) {
      if (ar.rect.x > x1 || ar.rect.y > y1 || ar.rect.x + ar.rect.w < x0 || ar.rect.y + ar.rect.h < y0) continue;
      if (ImGui::CalcTextSize(ar.name.c_str()).x < ar.rect.w * ts - 6)
        dl->AddText(T(ar.rect.x + 0.2f, ar.rect.y + ar.rect.h - 0.9f), IM_COL32(40, 30, 20, 170), ar.name.c_str());
    }
  if (ts >= 2.2f && ed.showLabels)
    for (auto& s : w.sectors) {
      if (s.rect.x > x1 || s.rect.y > y1 || s.rect.x + s.rect.w < x0 || s.rect.y + s.rect.h < y0) continue;
      ImVec2 c = T(s.rect.x + s.rect.w * 0.5f, (float)s.rect.y);
      // Clip the name to the building's width so neighbors don't overlap.
      float maxW = s.rect.w * ts - 8;
      std::string name = s.name;
      while (name.size() > 4 && ImGui::CalcTextSize(name.c_str()).x > maxW) name = name.substr(0, name.size() - 4) + "...";
      if (ImGui::CalcTextSize(name.c_str()).x <= maxW)
        pill(dl, ImVec2(c.x, c.y - 2), name.c_str(), IM_COL32(20, 20, 24, 200), IM_COL32(245, 245, 245, 255));
    }

  // Rooms under the cursor.
  if (hovered) {
    ImVec2 m = v.toWorld(mouse);
    int mx = (int)std::floor(m.x), my = (int)std::floor(m.y);
    if (mx >= 0 && my >= 0 && mx < w.width() && my < w.height()) hoverRoom = w.arenaAt(mx, my);
  }
  if (ed.sel.kind == SelKind::Building && ed.sel.index >= 0 && ed.sel.index < (int)w.sectors.size()) {
    const ville::Rect& r = w.sectors[ed.sel.index].rect;
    dl->AddRect(T((float)r.x, (float)r.y), T((float)(r.x + r.w), (float)(r.y + r.h)), ed.pal.select, 0, 0, 3.0f);
  }
  if (ed.sel.kind == SelKind::Location && ed.sel.index >= 0 && ed.sel.index < (int)w.arenas.size()) {
    const ville::Rect& r = w.arenas[ed.sel.index].rect;
    dl->AddRect(T((float)r.x, (float)r.y), T((float)(r.x + r.w), (float)(r.y + r.h)), ed.pal.select, 0, 0, 2.5f);
  }

  // Conversations in the last ten game-minutes, as lines between speakers.
  size_t logEnd = std::min(frame.logCount, ed.log.size());
  if (ed.showTalk)
    for (size_t k = logEnd; k-- > 0;) {
      const sl::LogEntry& e = ed.log[k];
      if (e.round < frame.round - 60) break;
      if (e.kind != sl::LogKind::Dialogue || e.visibleTo.size() != 2) continue;
      int a = e.visibleTo[0], b = e.visibleTo[1];
      if (a >= (int)frame.agents.size() || b >= (int)frame.agents.size()) continue;
      dl->AddLine(T(frame.agents[a].x, frame.agents[a].y), T(frame.agents[b].x, frame.agents[b].y), ed.pal.talk, 2.0f);
    }

  // Residents.
  int n = static_cast<int>(frame.agents.size());
  const auto* details = info.details ? info.details.get() : nullptr;
  float r = std::clamp(ts * 0.45f, 2.0f, 16.0f);
  int hover = -1;
  float bestD = std::max(8.0f, r + 3);
  std::vector<int> onScreen;
  for (int i = 0; i < n; ++i) {
    ImVec2 p = T(frame.agents[i].x, frame.agents[i].y);
    if (p.x < v.origin.x - 20 || p.y < v.origin.y - 20 || p.x > v.origin.x + v.size.x + 20 || p.y > v.origin.y + v.size.y + 20)
      continue;
    onScreen.push_back(i);
    if (hovered) {
      float d = std::hypot(mouse.x - p.x, mouse.y - p.y);
      if (d < bestD) {
        bestD = d;
        hover = i;
      }
    }
  }
  for (int i : onScreen) {
    ImVec2 p = T(frame.agents[i].x, frame.agents[i].y);
    ImU32 col = residentColor(info.agents[i].seat);
    if (ts < 3.0f) {
      dl->AddRectFilled(ImVec2(p.x - 1.5f, p.y - 1.5f), ImVec2(p.x + 1.5f, p.y + 1.5f), col);
      continue;
    }
    // A little figure: shadow, body, head.
    dl->AddCircleFilled(ImVec2(p.x, p.y + r * 0.85f), r * 0.7f, IM_COL32(0, 0, 0, 60), 12);
    dl->AddCircleFilled(ImVec2(p.x, p.y + r * 0.25f), r * 0.62f, col, 14);
    dl->AddCircleFilled(ImVec2(p.x, p.y - r * 0.5f), r * 0.45f, rgb(0xF1C9A0), 14);
    dl->AddCircle(ImVec2(p.x, p.y - r * 0.5f), r * 0.45f, IM_COL32(60, 40, 30, 160), 14, 1.0f);
    if (ed.sel.kind == SelKind::Agent && ed.sel.index == i) dl->AddCircle(ImVec2(p.x, p.y), r * 1.5f, ed.pal.select, 24, 2.5f);
  }
  // Names and bubbles once there's room to read them.
  bool fewEnough = onScreen.size() <= 80;
  for (int i : onScreen) {
    bool selected = ed.sel.kind == SelKind::Agent && ed.sel.index == i;
    if (!selected && (ts < 11 || !fewEnough)) continue;
    ImVec2 p = T(frame.agents[i].x, frame.agents[i].y);
    const std::string& name = info.agents[i].seat;
    std::string first = name.substr(0, name.find(' '));
    if (ed.showLabels) {
      ImVec2 tsz = ImGui::CalcTextSize(first.c_str());
      dl->AddText(ImVec2(p.x - tsz.x * 0.5f, p.y + r * 0.95f), IM_COL32(15, 15, 15, 230), first.c_str());
    }
    if (!ed.showGizmos || !details || i >= (int)details->size()) continue;
    const AgentDetail& d = (*details)[i];
    if (d.chatWith >= 0 && !d.utterance.empty()) {
      // Speech bubble with what they're saying.
      std::string text = shortText(d.utterance, 90);
      float wrap = 190;
      ImVec2 tsz = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
      ImVec2 a(p.x - tsz.x * 0.5f - 6, p.y - r * 1.2f - tsz.y - 12), b(p.x + tsz.x * 0.5f + 6, p.y - r * 1.2f - 4);
      dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 240), 6);
      dl->AddTriangleFilled(ImVec2(p.x - 4, b.y), ImVec2(p.x + 4, b.y), ImVec2(p.x, b.y + 5), IM_COL32(255, 255, 255, 240));
      dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + 6, a.y + 4), IM_COL32(20, 20, 20, 255), text.c_str(), nullptr, wrap);
    } else {
      // Emoji + what they're doing, like the paper's replay.
      std::string text = d.emoji + " " + shortText(d.action, selected ? 60 : 28);
      pill(dl, ImVec2(p.x, p.y - r * 1.25f - 8), text.c_str(), IM_COL32(20, 20, 24, 215), IM_COL32(250, 250, 250, 255));
    }
  }
  return hover;
}

}  // namespace

void drawScene(EditorState& ed) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool open = beginPanel("World", &ed.showScene, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (ImGui::GetWindowDockID()) ed.sceneDockId = ImGui::GetWindowDockID();
  if (!open) {
    ImGui::End();
    return;
  }
  const SimInfo& info = ed.info;
  const Frame* frame = viewedFrame(ed);
  ImVec2 ws = worldSize(info);

  // ---- View toolbar
  ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6, ImGui::GetCursorPosY() + 4));
  toggleChip("Labels", &ed.showLabels);
  ImGui::SameLine();
  toggleChip("Talk lines", &ed.showTalk);
  ImGui::SameLine();
  toggleChip(info.isVille ? "Bubbles" : "Highlights", &ed.showGizmos);
  ImGui::SameLine();
  if (ImGui::SmallButton("Frame All")) ed.fitView = true;
  ImGui::SetItemTooltip("Fit the whole world in view");
  ImGui::SameLine();
  ImGui::TextDisabled("%.0f%%", ed.zoom * 100);
  if (!info.hasWorld && info.status != SimStatus::Empty && !info.agents.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled(" |  no world { } in this game: agents shown in a ring");
  }

  // ---- Canvas
  float timelineH = 30;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  origin.y += 2;
  ImGui::SetCursorScreenPos(origin);
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 size(std::max(avail.x, 50.0f), std::max(avail.y - timelineH, 50.0f));
  ImGui::InvisibleButton("##canvas", size,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  float pad = 36;
  float fitScale = std::min((size.x - pad * 2) / ws.x, (size.y - pad * 2) / (ws.y * 1.14f));
  fitScale = std::max(fitScale, 0.0001f);
  if (ed.fitView) {
    ed.zoom = 1;
    ed.camCenter = ImVec2(ws.x * 0.5f, ws.y * 0.55f);
    ed.fitView = false;
  }
  View v{origin, size, fitScale * ed.zoom, ed.camCenter};

  // Frame the selection (F, or double-click in the Hierarchy)
  bool wantFrame = ed.frameSelection || (hovered && ImGui::IsKeyPressed(ImGuiKey_F, false)) ||
                   (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
                    !ImGui::GetIO().WantTextInput);
  if (wantFrame && frame) {
    if (ed.sel.kind == SelKind::Agent && ed.sel.index < (int)frame->agents.size()) {
      ed.camCenter = agentPos(info, *frame, ed.sel.index);
      ed.zoom = std::max(ed.zoom, 2.5f);
    } else if (ed.sel.kind == SelKind::Location && ed.sel.index < (int)info.locations.size()) {
      ed.camCenter = ImVec2((float)info.locations[ed.sel.index].x, (float)info.locations[ed.sel.index].y);
      ed.zoom = std::max(ed.zoom, 2.5f);
    } else {
      ed.fitView = true;
    }
  }
  ed.frameSelection = false;

  // Pan / zoom
  ImGuiIO& io = ImGui::GetIO();
  static bool panning = false;
  static int hoveredAgentAtPress = -1;
  if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0) ||
                 (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3) && hoveredAgentAtPress < 0))) {
    panning = true;
    ed.camCenter.x -= io.MouseDelta.x / v.scale;
    ed.camCenter.y -= io.MouseDelta.y / v.scale;
  }
  if (hovered && io.MouseWheel != 0) {
    ImVec2 before = v.toWorld(io.MousePos);
    ed.zoom = std::clamp(ed.zoom * std::pow(1.15f, io.MouseWheel), 0.2f, 60.0f);
    v.scale = fitScale * ed.zoom;
    ImVec2 after = v.toWorld(io.MousePos);
    ed.camCenter.x += before.x - after.x;
    ed.camCenter.y += before.y - after.y;
  }
  v.center = ed.camCenter;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), ed.pal.sceneBg);
  if (!info.isVille) drawGrid(dl, v, ed);

  if (info.status == SimStatus::Empty) {
    centeredText(ed, v, "Pick a scenario in the Scenarios panel.", ImGui::GetColorU32(ImGuiCol_Text));
  } else if (info.status == SimStatus::CompileError) {
    std::string msg = "This script has errors -- fix them to run it.\n\n" + info.error;
    centeredText(ed, v, msg.c_str(), ed.pal.error);
  }

  int hoverAgent = -1, hoverLoc = -1;
  if (frame && !info.agents.empty() && info.isVille) {
    hoverAgent = drawVille(ed, dl, v, *frame, hovered && !panning, io.MousePos, hoverLoc);
    // The game clock, on the map.
    std::string clock = info.clock + (info.busy && !info.playing ? "" : "");
    pill(dl, ImVec2(origin.x + 14 + ImGui::CalcTextSize(clock.c_str()).x * 0.5f, origin.y + 16), clock.c_str(),
         IM_COL32(20, 20, 24, 220), IM_COL32(250, 250, 250, 255), 8, 4);
  } else if (frame && !info.agents.empty()) {
    // World bounds
    if (info.hasWorld && info.worldW > 0) {
      ImVec2 a = v.toScreen(0, 0), b = v.toScreen(ws.x, ws.y);
      dl->AddRect(a, b, ed.pal.worldBorder, 0, 0, 1.5f);
    }

    // Locations: square gizmos. Dense worlds label one instance per type
    // until you zoom in.
    bool dense = info.locations.size() > 40;
    std::vector<std::string> labeledTypes;
    float lsz = std::clamp(5.0f * std::sqrt(ed.zoom), 4.0f, 11.0f);
    for (size_t li = 0; li < info.locations.size(); ++li) {
      const sl::Location& loc = info.locations[li];
      ImVec2 p = v.toScreen((float)loc.x, (float)loc.y);
      if (p.x < origin.x - 60 || p.y < origin.y - 60 || p.x > origin.x + size.x + 60 || p.y > origin.y + size.y + 60)
        continue;
      bool sel = ed.sel.kind == SelKind::Location && ed.sel.index == (int)li;
      dl->AddRect(ImVec2(p.x - lsz, p.y - lsz), ImVec2(p.x + lsz, p.y + lsz), sel ? ed.pal.select : ed.pal.location, 1.0f,
                  0, sel ? 2.5f : 1.2f);
      if (hovered && std::fabs(io.MousePos.x - p.x) <= lsz + 2 && std::fabs(io.MousePos.y - p.y) <= lsz + 2) hoverLoc = (int)li;
      bool label = ed.showLabels &&
                   (!dense || ed.zoom >= 3.0f ||
                    std::find(labeledTypes.begin(), labeledTypes.end(), loc.type) == labeledTypes.end());
      if (label) {
        labeledTypes.push_back(loc.type);
        const std::string& text = dense && ed.zoom < 3.0f ? loc.type : loc.id;
        ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        dl->AddText(ImVec2(p.x - ts.x * 0.5f, p.y + lsz + 2), ed.pal.locationLabel, text.c_str());
      }
    }

    // Conversation lines from the last few rounds, fading with age.
    size_t logEnd = std::min(frame->logCount, ed.log.size());
    if (ed.showTalk) {
      for (size_t i = logEnd; i-- > 0;) {
        const sl::LogEntry& e = ed.log[i];
        if (e.round < frame->round - 3) break;
        if (e.kind != sl::LogKind::Dialogue || e.visibleTo.size() != 2) continue;
        int a = e.visibleTo[0], b = e.visibleTo[1];
        if (a >= (int)frame->agents.size() || b >= (int)frame->agents.size()) continue;
        float age = static_cast<float>(frame->round - e.round);
        ImU32 col = (ed.pal.talk & 0x00FFFFFF) | (static_cast<ImU32>(220 * (1.0f - age / 4.0f)) << 24);
        ImVec2 pa = agentPos(info, *frame, a), pb = agentPos(info, *frame, b);
        dl->AddLine(v.toScreen(pa.x, pa.y), v.toScreen(pb.x, pb.y), col, 1.6f);
      }
    }

    // Who acted in the viewed round -- a ring, like a Unity gizmo highlight.
    std::vector<char> spoke(frame->agents.size(), 0);
    for (size_t i = logEnd; i-- > 0;) {
      const sl::LogEntry& e = ed.log[i];
      if (e.round < frame->round) break;
      if (e.kind != sl::LogKind::Dialogue && e.kind != sl::LogKind::Broadcast && e.kind != sl::LogKind::Reflection) continue;
      int a = seatIndex(ed, e.author);
      if (a >= 0 && a < (int)spoke.size()) spoke[a] = 1;
    }

    // Agents
    int n = static_cast<int>(frame->agents.size());
    bool crowd = n > 1500;
    float r = crowd ? 2.2f : std::clamp(4.0f * std::sqrt(ed.zoom), 3.0f, 10.0f);
    int onScreen = 0;
    float bestD = 9.0f + r;
    for (int i = 0; i < n; ++i) {
      ImVec2 w = agentPos(info, *frame, i);
      ImVec2 p = v.toScreen(w.x, w.y);
      if (p.x < origin.x - 20 || p.y < origin.y - 20 || p.x > origin.x + size.x + 20 || p.y > origin.y + size.y + 20)
        continue;
      ++onScreen;
      if (hovered) {
        float d = std::hypot(io.MousePos.x - p.x, io.MousePos.y - p.y);
        if (d < bestD) {
          bestD = d;
          hoverAgent = i;
        }
      }
      if (!frame->agents[i].alive) {
        float k = r * 0.9f;
        dl->AddLine(ImVec2(p.x - k, p.y - k), ImVec2(p.x + k, p.y + k), ed.pal.dead, 2.0f);
        dl->AddLine(ImVec2(p.x + k, p.y - k), ImVec2(p.x - k, p.y + k), ed.pal.dead, 2.0f);
        continue;
      }
      ImU32 col = info.hiddenRoles && teamRevealed(ed, i) ? teamColor(ed, info.agents[i].team)
                  : info.hiddenRoles                        ? ed.pal.agent
                                                            : teamColor(ed, info.agents[i].team);
      if (crowd) {
        dl->AddRectFilled(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), col);
      } else {
        dl->AddCircleFilled(p, r, col, 14);
        dl->AddCircle(p, r, ed.pal.agentOutline, 14, 1.0f);
      }
      if (spoke[i]) dl->AddCircle(p, r + 3.5f, ed.pal.talk, 18, 1.5f);
    }

    // Seat labels once the crowd is small enough to read.
    if (ed.showLabels && onScreen <= 80) {
      for (int i = 0; i < n; ++i) {
        ImVec2 w = agentPos(info, *frame, i);
        ImVec2 p = v.toScreen(w.x, w.y);
        dl->AddText(ImVec2(p.x + r + 3, p.y - r - 12), ed.pal.label, info.agents[i].seat.c_str());
      }
    }

    // Selection: Unity's orange outline plus move-gizmo arrows.
    if (ed.sel.kind == SelKind::Agent && ed.sel.index >= 0 && ed.sel.index < n) {
      ImVec2 w = agentPos(info, *frame, ed.sel.index);
      ImVec2 p = v.toScreen(w.x, w.y);
      dl->AddCircle(p, r + 3, ed.pal.select, 20, 2.5f);
    }

    // Winner banner
    if (info.status == SimStatus::Done && ed.viewFrame == (int)ed.frames.size() - 1) {
      std::string text = "Winner: " + info.winner;
      ImGui::PushFont(ed.fonts.bold, 0.0f);
      ImVec2 ts = ImGui::CalcTextSize(text.c_str());
      ImVec2 p(origin.x + (size.x - ts.x) * 0.5f, origin.y + 14);
      dl->AddRectFilled(ImVec2(p.x - 14, p.y - 6), ImVec2(p.x + ts.x + 14, p.y + ts.y + 6), ed.pal.accent, 4);
      dl->AddText(p, IM_COL32(255, 255, 255, 255), text.c_str());
      ImGui::PopFont();
    }
  }
  if (info.status == SimStatus::RuntimeError) {
    std::string msg = "Runtime error: " + info.error;
    ImVec2 p(origin.x + 10, origin.y + size.y - 28);
    dl->AddRectFilled(ImVec2(p.x - 4, p.y - 4), ImVec2(origin.x + size.x - 10, p.y + 20), ImGui::GetColorU32(ImGuiCol_PopupBg, 0.95f), 3);
    dl->AddText(p, ed.pal.error, msg.c_str());
  }
  dl->PopClipRect();

  // Clicks: select what's under the cursor (a click, not the end of a pan).
  if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    hoveredAgentAtPress = hoverAgent;
    panning = false;
  }
  if (ImGui::IsItemDeactivated() && !panning && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    if (hoverAgent >= 0) ed.sel = {SelKind::Agent, hoverAgent};
    else if (hoverLoc >= 0) ed.sel = {SelKind::Location, hoverLoc};
    else ed.sel = {};
  }
  if (!active) {
    panning = false;
    hoveredAgentAtPress = -1;
  }

  // Hover tooltip
  if (hovered && !panning && frame) {
    if (hoverAgent >= 0) {
      ImGui::BeginTooltip();
      const AgentStatic& a = info.agents[hoverAgent];
      ImGui::PushFont(ed.fonts.bold, 0.0f);
      ImGui::TextUnformatted(a.seat.c_str());
      ImGui::PopFont();
      if (teamRevealed(ed, hoverAgent)) ImGui::Text("%s  (%s)", a.role.c_str(), a.team.c_str());
      else ImGui::TextDisabled("role hidden until revealed");
      int li = frame->agents[hoverAgent].location;
      if (li >= 0 && li < (int)info.locations.size()) ImGui::TextDisabled("at %s", info.locations[li].id.c_str());
      if (!frame->agents[hoverAgent].alive) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ed.pal.dead), "eliminated");
      ImGui::EndTooltip();
    } else if (hoverLoc >= 0) {
      ImGui::SetTooltip("%s  (%s)", info.locations[hoverLoc].id.c_str(), info.locations[hoverLoc].type.c_str());
    }
  }

  // ---- Timeline: scrub back through rounds
  ImGui::SetCursorScreenPos(ImVec2(origin.x + 8, origin.y + size.y + 5));
  int maxFrame = std::max(0, (int)ed.frames.size() - 1);
  bool live = ed.followLatest;
  ImGui::PushStyleColor(ImGuiCol_Button, live ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
  if (ImGui::SmallButton("Live")) ed.followLatest = true;
  ImGui::PopStyleColor();
  ImGui::SetItemTooltip("Follow the newest round");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(std::max(80.0f, size.x - 190));
  int vf = std::clamp(ed.viewFrame, 0, maxFrame);
  int shownRound = ed.frames.empty() ? 0 : ed.frames[vf]->round;
  char fmt[48];
  if (info.isVille) std::snprintf(fmt, sizeof fmt, "%s", villeClockForStep(shownRound).c_str());
  else std::snprintf(fmt, sizeof fmt, "Round %d", shownRound);
  ImGui::BeginDisabled(ed.frames.size() <= 1);
  if (ImGui::SliderInt("##timeline", &vf, 0, maxFrame, fmt)) {
    ed.viewFrame = vf;
    ed.followLatest = vf == maxFrame;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (info.isVille) ImGui::TextDisabled("step %d", ed.frames.empty() ? 0 : ed.frames.back()->round);
  else ImGui::TextDisabled("%d rounds", ed.frames.empty() ? 0 : ed.frames.back()->round);

  ImGui::End();
}

}  // namespace app
