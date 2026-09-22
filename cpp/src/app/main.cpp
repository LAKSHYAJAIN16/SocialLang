// SocialSandbox -- entry point: a Win32 window, Direct3D 11, and Dear ImGui
// with docking and multi-viewports (panels can be torn out into their own OS
// windows, like Unity's). Based on Dear ImGui's example_win32_directx11.
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "app/editor.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gTarget = nullptr;
bool gOccluded = false;
UINT gResizeW = 0, gResizeH = 0;

void createTarget() {
  ID3D11Texture2D* back = nullptr;
  gSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
  gDevice->CreateRenderTargetView(back, nullptr, &gTarget);
  back->Release();
}

void releaseTarget() {
  if (gTarget) {
    gTarget->Release();
    gTarget = nullptr;
  }
}

bool createDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                             &sd, &gSwapChain, &gDevice, &got, &gContext);
  if (hr == DXGI_ERROR_UNSUPPORTED)  // no GPU driver: fall back to WARP
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                       &gSwapChain, &gDevice, &got, &gContext);
  if (hr != S_OK) return false;
  IDXGIFactory* factory = nullptr;
  if (SUCCEEDED(gSwapChain->GetParent(IID_PPV_ARGS(&factory)))) {
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    factory->Release();
  }
  createTarget();
  return true;
}

void destroyDevice() {
  releaseTarget();
  if (gSwapChain) gSwapChain->Release();
  if (gContext) gContext->Release();
  if (gDevice) gDevice->Release();
}

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
  switch (msg) {
    case WM_SIZE:
      if (wParam == SIZE_MINIMIZED) return 0;
      gResizeW = LOWORD(lParam);
      gResizeH = HIWORD(lParam);
      return 0;
    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::string appDataPath(const char* file) {
  const char* appdata = std::getenv("APPDATA");
  std::filesystem::path dir = appdata ? std::filesystem::path(appdata) / "SocialSandbox" : std::filesystem::path(".");
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return (dir / file).string();
}

ImFont* loadFont(const char* path, float size) {
  ImGuiIO& io = ImGui::GetIO();
  if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return nullptr;
  return io.Fonts->AddFontFromFileTTF(path, size);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
  ImGui_ImplWin32_EnableDpiAwareness();
  float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

  WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wndProc, 0, 0, hInst, LoadIconW(hInst, MAKEINTRESOURCEW(1)), nullptr,
                 nullptr, nullptr, L"SocialSandbox", LoadIconW(hInst, MAKEINTRESOURCEW(1))};
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"SocialSandbox", WS_OVERLAPPEDWINDOW, 80, 60, (int)(1440 * scale),
                            (int)(900 * scale), nullptr, nullptr, hInst, nullptr);
  // Dark title bar to match the default skin.
  BOOL darkTitle = TRUE;
  DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkTitle, sizeof darkTitle);

  if (!createDevice(hwnd)) {
    destroyDevice();
    MessageBoxW(nullptr, L"Direct3D 11 is not available on this machine.", L"SocialSandbox", MB_ICONERROR);
    return 1;
  }
  ShowWindow(hwnd, SW_SHOWMAXIMIZED);
  UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  static std::string iniPath = appDataPath("layout.ini");
  io.IniFilename = iniPath.c_str();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;

  auto ed = std::make_unique<app::EditorState>();
  ImGuiStyle& style = ImGui::GetStyle();
  style.FontSizeBase = 15.0f;
  app::applyTheme(*ed);
  style.ScaleAllSizes(scale);
  style.FontScaleDpi = scale;
  io.ConfigDpiScaleFonts = true;
  io.ConfigDpiScaleViewports = true;

  ed->fonts.ui = loadFont("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);
  if (!ed->fonts.ui) ed->fonts.ui = io.Fonts->AddFontDefault();
  ed->fonts.bold = loadFont("C:\\Windows\\Fonts\\segoeuib.ttf", 15.0f);
  if (!ed->fonts.bold) ed->fonts.bold = ed->fonts.ui;
  ed->fonts.mono = loadFont("C:\\Windows\\Fonts\\consola.ttf", 14.0f);
  if (!ed->fonts.mono) ed->fonts.mono = ed->fonts.ui;
  io.FontDefault = ed->fonts.ui;

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(gDevice, gContext);
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    app::parseLaunchArgs(*ed, argc, argv);
    LocalFree(argv);
  }
  app::initEditor(*ed);

  bool done = false;
  while (!done) {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_QUIT) done = true;
    }
    if (done) break;
    if (gOccluded && gSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
      Sleep(10);  // minimized / locked: don't spin
      continue;
    }
    gOccluded = false;
    if (gResizeW && gResizeH) {
      releaseTarget();
      gSwapChain->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0);
      gResizeW = gResizeH = 0;
      createTarget();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    app::drawEditor(*ed);
    ImGui::Render();

    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_DockingEmptyBg);
    const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
    gContext->OMSetRenderTargets(1, &gTarget, nullptr);
    gContext->ClearRenderTargetView(gTarget, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
    }
    gOccluded = gSwapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED;  // vsync caps the UI at the display rate
  }

  ed.reset();  // stops the sim worker before tearing down ImGui
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  destroyDevice();
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, hInst);
  return 0;
}
