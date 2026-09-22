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

}  // namespace

void drawScene(EditorState& ed) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool open = beginPanel("Scene", &ed.showScene, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (ImGui::GetWindowDockID()) ed.sceneDockId = ImGui::GetWindowDockID();
  if (!open) {
    ImGui::End();
    return;
  }
  const SimInfo& info = ed.info;
  const Frame* frame = viewedFrame(ed);
  ImVec2 ws = worldSize(info);

  // ---- Scene toolbar (Unity's strip above the view)
  ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6, ImGui::GetCursorPosY() + 4));
  toggleChip("Labels", &ed.showLabels);
  ImGui::SameLine();
  toggleChip("Talk lines", &ed.showTalk);
  ImGui::SameLine();
  toggleChip("Gizmos", &ed.showGizmos);
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
  drawGrid(dl, v, ed);

  if (info.status == SimStatus::Empty) {
    centeredText(ed, v, "Open a game: double-click a .sl file in the Project panel.", ImGui::GetColorU32(ImGuiCol_Text));
  } else if (info.status == SimStatus::CompileError) {
    std::string msg = "All compiler errors have to be fixed before you can enter Play mode!\n\n" + info.error;
    centeredText(ed, v, msg.c_str(), ed.pal.error);
  }

  int hoverAgent = -1, hoverLoc = -1;
  if (frame && !info.agents.empty()) {
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
      if (ed.showGizmos) {
        float L = 44;
        dl->AddLine(p, ImVec2(p.x + L, p.y), ed.pal.gizmoX, 2.5f);
        dl->AddTriangleFilled(ImVec2(p.x + L + 9, p.y), ImVec2(p.x + L, p.y - 5), ImVec2(p.x + L, p.y + 5), ed.pal.gizmoX);
        dl->AddLine(p, ImVec2(p.x, p.y - L), ed.pal.gizmoY, 2.5f);
        dl->AddTriangleFilled(ImVec2(p.x, p.y - L - 9), ImVec2(p.x - 5, p.y - L), ImVec2(p.x + 5, p.y - L), ed.pal.gizmoY);
        dl->AddRectFilled(ImVec2(p.x + 6, p.y - 16), ImVec2(p.x + 16, p.y - 6), IM_COL32(60, 120, 230, 110));
      }
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
  std::snprintf(fmt, sizeof fmt, "Round %d", shownRound);
  ImGui::BeginDisabled(ed.frames.size() <= 1);
  if (ImGui::SliderInt("##timeline", &vf, 0, maxFrame, fmt)) {
    ed.viewFrame = vf;
    ed.followLatest = vf == maxFrame;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%d rounds", ed.frames.empty() ? 0 : ed.frames.back()->round);

  ImGui::End();
}

}  // namespace app
