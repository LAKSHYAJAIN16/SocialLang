cmake_minimum_required(VERSION 3.20)
project(SocialSandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive- /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

# ---- Engine: the SocialLang lexer/parser/interpreter plus LLM providers. No UI,
# no window -- linked by both the editor app and the headless CLI runner.
add_library(sl_engine STATIC
  src/engine/value.cpp
  src/engine/lexer.cpp
  src/engine/parser.cpp
  src/engine/memory.cpp
  src/engine/interpreter.cpp
  src/providers/provider.cpp
  src/providers/http_win.cpp
  src/providers/settings.cpp
  src/ville/world.cpp
  src/ville/personas.cpp
  src/ville/cognition.cpp
  src/ville/sim.cpp
  src/ville/spec.cpp
)
target_include_directories(sl_engine PUBLIC src third_party)
target_link_libraries(sl_engine PUBLIC winhttp crypt32)

# ---- Headless runner: `sl_run game.sl --seed N` -- engine smoke tests and
# benchmarks without opening a window.
add_executable(sl_run src/cli/main.cpp)
target_link_libraries(sl_run PRIVATE sl_engine)

option(SL_BUILD_APP "Build the SocialSandbox editor (needs Windows + DirectX 11)" ON)
if(NOT SL_BUILD_APP)
  return()
endif()

# ---- The editor app.
set(IMGUI_DIR third_party/imgui)
add_executable(SocialSandbox WIN32
  src/app/main.cpp
  src/app/theme.cpp
  src/app/sim_controller.cpp
  src/app/editor.cpp
  src/app/panels_scene.cpp
  src/app/panels_hierarchy.cpp
  src/app/panels_inspector.cpp
  src/app/panels_console.cpp
  src/app/panels_project.cpp
  src/app/panels_settings.cpp
  src/app/panels_town_edit.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
  resources/app.rc
)
target_include_directories(SocialSandbox PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends)
# 32-bit ImWchar: emoji (the residents' activity bubbles) live above U+FFFF.
target_compile_definitions(SocialSandbox PRIVATE IMGUI_USE_WCHAR32)
target_link_libraries(SocialSandbox PRIVATE sl_engine d3d11 dxgi d3dcompiler dwmapi shell32 ole32 comdlg32)

# The example games ship next to the exe as the Project panel's Assets folder
# (the app prefers the repo's own games/ when run from a checkout -- see
# resolveAssetsDir in editor.cpp).
add_custom_command(TARGET SocialSandbox POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/../games $<TARGET_FILE_DIR:SocialSandbox>/games)
