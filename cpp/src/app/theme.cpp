// Unity editor skins: the dark "Pro" skin and the light "Personal" skin.
// Flat gray panels, darker tab strips with the selected tab merging into its
// panel, 3px-rounded controls, and Unity's blue selection.
#include "app/editor.h"

namespace app {

namespace {

ImVec4 hex(unsigned rgb, float a = 1.0f) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}
ImU32 u32(unsigned rgb, float a = 1.0f) { return ImGui::ColorConvertFloat4ToU32(hex(rgb, a)); }

}  // namespace

void applyTheme(EditorState& ed) {
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 0;
  s.ChildRounding = 0;
  s.FrameRounding = 3;
  s.PopupRounding = 3;
  s.GrabRounding = 3;
  s.TabRounding = 3;
  s.ScrollbarRounding = 6;
  s.WindowBorderSize = 1;
  s.FrameBorderSize = 1;
  s.PopupBorderSize = 1;
  s.TabBorderSize = 0;
  s.TabBarBorderSize = 1;
  s.WindowPadding = ImVec2(8, 6);
  s.FramePadding = ImVec2(6, 3);
  s.ItemSpacing = ImVec2(6, 4);
  s.ItemInnerSpacing = ImVec2(5, 4);
  s.IndentSpacing = 14;
  s.ScrollbarSize = 11;
  s.GrabMinSize = 8;
  s.WindowMenuButtonPosition = ImGuiDir_None;
  s.DockingSeparatorSize = 2;
  s.SeparatorTextBorderSize = 1;

  ImVec4* c = s.Colors;
  if (ed.dark) {
    c[ImGuiCol_Text] = hex(0xD2D2D2);
    c[ImGuiCol_TextDisabled] = hex(0x858585);
    c[ImGuiCol_WindowBg] = hex(0x383838);
    c[ImGuiCol_ChildBg] = hex(0x383838, 0);
    c[ImGuiCol_PopupBg] = hex(0x2B2B2B);
    c[ImGuiCol_Border] = hex(0x1A1A1A);
    c[ImGuiCol_BorderShadow] = hex(0, 0);
    c[ImGuiCol_FrameBg] = hex(0x2A2A2A);
    c[ImGuiCol_FrameBgHovered] = hex(0x303030);
    c[ImGuiCol_FrameBgActive] = hex(0x2A2A2A);
    c[ImGuiCol_TitleBg] = hex(0x282828);
    c[ImGuiCol_TitleBgActive] = hex(0x282828);
    c[ImGuiCol_TitleBgCollapsed] = hex(0x282828);
    c[ImGuiCol_MenuBarBg] = hex(0x282828);
    c[ImGuiCol_ScrollbarBg] = hex(0x383838);
    c[ImGuiCol_ScrollbarGrab] = hex(0x5A5A5A);
    c[ImGuiCol_ScrollbarGrabHovered] = hex(0x6A6A6A);
    c[ImGuiCol_ScrollbarGrabActive] = hex(0x7A7A7A);
    c[ImGuiCol_CheckMark] = hex(0xD2D2D2);
    c[ImGuiCol_SliderGrab] = hex(0x9A9A9A);
    c[ImGuiCol_SliderGrabActive] = hex(0xBDBDBD);
    c[ImGuiCol_Button] = hex(0x585858);
    c[ImGuiCol_ButtonHovered] = hex(0x676767);
    c[ImGuiCol_ButtonActive] = hex(0x46607C);
    c[ImGuiCol_Header] = hex(0x2C5D87);
    c[ImGuiCol_HeaderHovered] = hex(0x454545);
    c[ImGuiCol_HeaderActive] = hex(0x2C5D87);
    c[ImGuiCol_Separator] = hex(0x1F1F1F);
    c[ImGuiCol_SeparatorHovered] = hex(0x3A79BB);
    c[ImGuiCol_SeparatorActive] = hex(0x3A79BB);
    c[ImGuiCol_ResizeGrip] = hex(0, 0);
    c[ImGuiCol_ResizeGripHovered] = hex(0x3A79BB, 0.6f);
    c[ImGuiCol_ResizeGripActive] = hex(0x3A79BB);
    c[ImGuiCol_InputTextCursor] = hex(0xD2D2D2);
    c[ImGuiCol_Tab] = hex(0x282828);
    c[ImGuiCol_TabHovered] = hex(0x444444);
    c[ImGuiCol_TabSelected] = hex(0x383838);
    c[ImGuiCol_TabSelectedOverline] = hex(0x3A79BB);
    c[ImGuiCol_TabDimmed] = hex(0x282828);
    c[ImGuiCol_TabDimmedSelected] = hex(0x383838);
    c[ImGuiCol_TabDimmedSelectedOverline] = hex(0x383838, 0);
    c[ImGuiCol_DockingPreview] = hex(0x3A79BB, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = hex(0x1F1F1F);
    c[ImGuiCol_PlotLines] = hex(0x9A9A9A);
    c[ImGuiCol_PlotHistogram] = hex(0x3A79BB);
    c[ImGuiCol_TableHeaderBg] = hex(0x303030);
    c[ImGuiCol_TableBorderStrong] = hex(0x1F1F1F);
    c[ImGuiCol_TableBorderLight] = hex(0x2C2C2C);
    c[ImGuiCol_TableRowBg] = hex(0, 0);
    c[ImGuiCol_TableRowBgAlt] = hex(0xFFFFFF, 0.03f);
    c[ImGuiCol_TextLink] = hex(0x5BA3E8);
    c[ImGuiCol_TextSelectedBg] = hex(0x2C5D87);
    c[ImGuiCol_TreeLines] = hex(0x555555);
    c[ImGuiCol_DragDropTarget] = hex(0x3A79BB);
    c[ImGuiCol_NavCursor] = hex(0x3A79BB);
    c[ImGuiCol_NavWindowingHighlight] = hex(0xFFFFFF, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = hex(0, 0.3f);
    c[ImGuiCol_ModalWindowDimBg] = hex(0, 0.45f);
    c[ImGuiCol_UnsavedMarker] = hex(0xD2D2D2);

    ed.pal.sceneBg = u32(0x2E2E2E);
    ed.pal.gridMinor = u32(0x363636);
    ed.pal.gridMajor = u32(0x414141);
    ed.pal.worldBorder = u32(0x6E6E6E);
    ed.pal.agent = u32(0xC8C8C8);
    ed.pal.agentOutline = u32(0x1A1A1A);
    ed.pal.dead = u32(0xE0605A);
    ed.pal.location = u32(0x8A8A8A);
    ed.pal.locationLabel = u32(0x9A9A9A);
    ed.pal.label = u32(0xDADADA);
    ed.pal.talk = u32(0x4FB3F0);
    ed.pal.select = u32(0xF7A531);
    ed.pal.gizmoX = u32(0xE0443E);
    ed.pal.gizmoY = u32(0x7DC444);
    ed.pal.dim = u32(0x858585);
    ed.pal.accent = u32(0x3A79BB);
    ed.pal.error = u32(0xFF6B68);
    ed.pal.warning = u32(0xF2C744);
    ed.pal.playTint = u32(0x3A79BB, 0.22f);
  } else {
    c[ImGuiCol_Text] = hex(0x0B0B0B);
    c[ImGuiCol_TextDisabled] = hex(0x5E5E5E);
    c[ImGuiCol_WindowBg] = hex(0xC8C8C8);
    c[ImGuiCol_ChildBg] = hex(0xC8C8C8, 0);
    c[ImGuiCol_PopupBg] = hex(0xECECEC);
    c[ImGuiCol_Border] = hex(0x999999);
    c[ImGuiCol_BorderShadow] = hex(0, 0);
    c[ImGuiCol_FrameBg] = hex(0xF0F0F0);
    c[ImGuiCol_FrameBgHovered] = hex(0xF7F7F7);
    c[ImGuiCol_FrameBgActive] = hex(0xFFFFFF);
    c[ImGuiCol_TitleBg] = hex(0xA5A5A5);
    c[ImGuiCol_TitleBgActive] = hex(0xA5A5A5);
    c[ImGuiCol_TitleBgCollapsed] = hex(0xA5A5A5);
    c[ImGuiCol_MenuBarBg] = hex(0xCBCBCB);
    c[ImGuiCol_ScrollbarBg] = hex(0xC8C8C8);
    c[ImGuiCol_ScrollbarGrab] = hex(0x9A9A9A);
    c[ImGuiCol_ScrollbarGrabHovered] = hex(0x8A8A8A);
    c[ImGuiCol_ScrollbarGrabActive] = hex(0x7A7A7A);
    c[ImGuiCol_CheckMark] = hex(0x1A1A1A);
    c[ImGuiCol_SliderGrab] = hex(0x7A7A7A);
    c[ImGuiCol_SliderGrabActive] = hex(0x5A5A5A);
    c[ImGuiCol_Button] = hex(0xE4E4E4);
    c[ImGuiCol_ButtonHovered] = hex(0xEFEFEF);
    c[ImGuiCol_ButtonActive] = hex(0x96C3FB);
    c[ImGuiCol_Header] = hex(0x8FB4E3);
    c[ImGuiCol_HeaderHovered] = hex(0xB5B5B5);
    c[ImGuiCol_HeaderActive] = hex(0x8FB4E3);
    c[ImGuiCol_Separator] = hex(0x999999);
    c[ImGuiCol_SeparatorHovered] = hex(0x3A72B0);
    c[ImGuiCol_SeparatorActive] = hex(0x3A72B0);
    c[ImGuiCol_ResizeGrip] = hex(0, 0);
    c[ImGuiCol_ResizeGripHovered] = hex(0x3A72B0, 0.6f);
    c[ImGuiCol_ResizeGripActive] = hex(0x3A72B0);
    c[ImGuiCol_InputTextCursor] = hex(0x0B0B0B);
    c[ImGuiCol_Tab] = hex(0xA5A5A5);
    c[ImGuiCol_TabHovered] = hex(0xB9B9B9);
    c[ImGuiCol_TabSelected] = hex(0xC8C8C8);
    c[ImGuiCol_TabSelectedOverline] = hex(0x3A72B0);
    c[ImGuiCol_TabDimmed] = hex(0xA5A5A5);
    c[ImGuiCol_TabDimmedSelected] = hex(0xC8C8C8);
    c[ImGuiCol_TabDimmedSelectedOverline] = hex(0xC8C8C8, 0);
    c[ImGuiCol_DockingPreview] = hex(0x3A72B0, 0.4f);
    c[ImGuiCol_DockingEmptyBg] = hex(0x9C9C9C);
    c[ImGuiCol_PlotLines] = hex(0x555555);
    c[ImGuiCol_PlotHistogram] = hex(0x3A72B0);
    c[ImGuiCol_TableHeaderBg] = hex(0xBDBDBD);
    c[ImGuiCol_TableBorderStrong] = hex(0x999999);
    c[ImGuiCol_TableBorderLight] = hex(0xB0B0B0);
    c[ImGuiCol_TableRowBg] = hex(0, 0);
    c[ImGuiCol_TableRowBgAlt] = hex(0x000000, 0.04f);
    c[ImGuiCol_TextLink] = hex(0x1F5FA8);
    c[ImGuiCol_TextSelectedBg] = hex(0x8FB4E3);
    c[ImGuiCol_TreeLines] = hex(0x8A8A8A);
    c[ImGuiCol_DragDropTarget] = hex(0x3A72B0);
    c[ImGuiCol_NavCursor] = hex(0x3A72B0);
    c[ImGuiCol_NavWindowingHighlight] = hex(0x000000, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = hex(0, 0.2f);
    c[ImGuiCol_ModalWindowDimBg] = hex(0, 0.3f);
    c[ImGuiCol_UnsavedMarker] = hex(0x0B0B0B);

    ed.pal.sceneBg = u32(0xB4B4B4);
    ed.pal.gridMinor = u32(0xAAAAAA);
    ed.pal.gridMajor = u32(0x9C9C9C);
    ed.pal.worldBorder = u32(0x5E5E5E);
    ed.pal.agent = u32(0x2E2E2E);
    ed.pal.agentOutline = u32(0xE8E8E8);
    ed.pal.dead = u32(0xB3261E);
    ed.pal.location = u32(0x5E5E5E);
    ed.pal.locationLabel = u32(0x3E3E3E);
    ed.pal.label = u32(0x151515);
    ed.pal.talk = u32(0x1569B8);
    ed.pal.select = u32(0xD9780F);
    ed.pal.gizmoX = u32(0xC62D27);
    ed.pal.gizmoY = u32(0x3E8E1E);
    ed.pal.dim = u32(0x5E5E5E);
    ed.pal.accent = u32(0x3A72B0);
    ed.pal.error = u32(0xB3261E);
    ed.pal.warning = u32(0x8A6100);
    ed.pal.playTint = u32(0x3A72B0, 0.18f);
  }
}

}  // namespace app
