#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <timeapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#include "image.h"
#include "json.h"
#include "log.h"
#include "renderer.h"
#include "spine_player.h"

namespace {

constexpr int kTimerFrame = 1;
constexpr int kTimerWatch = 2;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr float kBaseHeight = 375.0f;
constexpr float kDefaultScale = 0.56f;
constexpr double kFrameIntervalMs = 1000.0 / 60.0;

enum MenuCommand : UINT {
  kCmdToggleShow = 100,
  kCmdOpenCodex,
  kCmdFormBase,
  kCmdFormBattle,
  kCmdBaseStand,
  kCmdBaseSit,
  kCmdBaseLie,
  kCmdBattleDefault,
  kCmdBattleSkill1,
  kCmdBattleSkill2,
  kCmdBattleSkill3,
  kCmdScale50,
  kCmdScale56,
  kCmdScale70,
  kCmdScale85,
  kCmdScale100,
  kCmdScale125,
  kCmdScale150,
  kCmdScaleReset,
  kCmdToggleTop,
  kCmdToggleAutostart,
  kCmdQuit,
};

struct Config {
  Form form = Form::Base;
  BaseState baseState = BaseState::Stand;
  BattleState battleState = BattleState::Default;
  float scale = kDefaultScale;
  int x = INT_MIN;
  int y = INT_MIN;
  bool alwaysOnTop = true;
  bool autostart = true;
};

struct App {
  HINSTANCE instance = nullptr;
  HWND window = nullptr;
  HMENU trayMenu = nullptr;
  HICON icon = nullptr;
  HICON smallIcon = nullptr;
  Renderer renderer;
  SpinePlayer player;
  PetStateMachine stateMachine;
  OperatorManifest manifest;
  Config config;
  std::wstring assetRoot;
  std::wstring configPath;
  std::wstring exePath;
  bool codexRunning = false;
  bool userHidden = false;
  bool dragging = false;
  bool dragMoved = false;
  bool dragPlanStarted = false;
  DragMode dragMode = DragMode::None;
  POINT dragCursorStart{};
  POINT dragWindowStart{};
  POINT windowPosition{};
  int dragLastSign = 0;
  ULONGLONG lastClickTick = 0;
  ULONGLONG lastWheelTick = 0;
  double lastFrameMs = 0.0;
  float dpiScale = 1.0f;
  bool frameTimerRunning = false;
  double nextFrameMs = 0.0;
  bool quitting = false;
};

App g_app;
int snapshotFrames = 20;
std::string g_slotFilter;
bool g_reverseOrder = false;

std::wstring moduleDirectory() {
  wchar_t buffer[MAX_PATH * 4];
  DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
  std::wstring path(buffer, length);
  size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(0, slash);
}

bool fileExists(const std::wstring& path) {
  DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring environmentPath(const wchar_t* name) {
  wchar_t buffer[MAX_PATH * 4];
  DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH * 4);
  return length ? std::wstring(buffer, length) : std::wstring();
}

std::wstring findAssetRoot() {
  std::wstring base = moduleDirectory();
  std::vector<std::wstring> candidates = {
      base + L"\\assets",
      base + L"\\..\\public\\assets",
      base + L"\\..\\..\\public\\assets",
  };
  wchar_t cwd[MAX_PATH * 4];
  DWORD length = GetCurrentDirectoryW(MAX_PATH * 4, cwd);
  if (length) candidates.push_back(std::wstring(cwd, length) + L"\\public\\assets");
  for (const std::wstring& candidate : candidates) {
    wchar_t full[MAX_PATH * 4];
    DWORD size = GetFullPathNameW(candidate.c_str(), MAX_PATH * 4, full, nullptr);
    if (!size || size >= MAX_PATH * 4) continue;
    std::wstring resolved(full, size);
    if (fileExists(resolved + L"\\operators\\char_1015_aglna2\\pet.json")) return resolved;
  }
  return base + L"\\assets";
}

std::wstring configDirectory() {
  std::wstring appData = environmentPath(L"APPDATA");
  if (appData.empty()) appData = moduleDirectory();
  std::wstring dir = appData + L"\\ArknightsOperators4CodexPet";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

void loadConfig(Config& config) {
  std::string text;
  if (!readFileText(g_app.configPath, text)) return;
  mini::Json root;
  if (!mini::parseJson(text, root)) return;
  if (const mini::Json* form = root.find("form")) {
    config.form = form->asText("base") == "battle" ? Form::Battle : Form::Base;
  }
  if (const mini::Json* base = root.find("baseState")) {
    std::string value = base->asText("stand");
    config.baseState = value == "sit" ? BaseState::Sit : (value == "lie" ? BaseState::Lie : BaseState::Stand);
  }
  if (const mini::Json* battle = root.find("battleState")) {
    std::string value = battle->asText("default");
    config.battleState = value == "skill1"   ? BattleState::Skill1
                         : value == "skill2" ? BattleState::Skill2
                         : value == "skill3" ? BattleState::Skill3
                                             : BattleState::Default;
  }
  if (const mini::Json* scale = root.find("scale")) config.scale = static_cast<float>(scale->asNumber(kDefaultScale));
  if (const mini::Json* x = root.find("x")) config.x = static_cast<int>(x->asNumber(INT_MIN));
  if (const mini::Json* y = root.find("y")) config.y = static_cast<int>(y->asNumber(INT_MIN));
  if (const mini::Json* top = root.find("alwaysOnTop")) config.alwaysOnTop = top->asBool(true);
  if (const mini::Json* autostart = root.find("autostart")) config.autostart = autostart->asBool(true);
}

void saveConfig() {
  const char* form = g_app.config.form == Form::Battle ? "battle" : "base";
  const char* baseState = g_app.config.baseState == BaseState::Sit
                              ? "sit"
                              : (g_app.config.baseState == BaseState::Lie ? "lie" : "stand");
  const char* battleState = g_app.config.battleState == BattleState::Skill1
                                ? "skill1"
                                : (g_app.config.battleState == BattleState::Skill2
                                       ? "skill2"
                                       : (g_app.config.battleState == BattleState::Skill3 ? "skill3"
                                                                                          : "default"));
  char buffer[512];
  snprintf(buffer, sizeof(buffer),
           "{\n  \"form\": \"%s\",\n  \"baseState\": \"%s\",\n  \"battleState\": \"%s\",\n"
           "  \"scale\": %.3f,\n  \"x\": %d,\n  \"y\": %d,\n  \"alwaysOnTop\": %s,\n"
           "  \"autostart\": %s\n}\n",
           form, baseState, battleState, g_app.config.scale, g_app.config.x, g_app.config.y,
           g_app.config.alwaysOnTop ? "true" : "false", g_app.config.autostart ? "true" : "false");
  std::vector<unsigned char> bytes(buffer, buffer + strlen(buffer));
  HANDLE file = CreateFileW(g_app.configPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
  CloseHandle(file);
}

bool isCodexRunning() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return true;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, L"ChatGPT.exe") == 0) {
        found = true;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

void setAutostart(bool enabled) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                    KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
    return;
  }
  if (enabled) {
    std::wstring command = L"\"" + g_app.exePath + L"\"";
    RegSetValueExW(key, L"ArknightsOperators4CodexPet", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(command.c_str()),
                   static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, L"ArknightsOperators4CodexPet");
  }
  RegCloseKey(key);
}

int scaledWidth(float scale) {
  float height = kBaseHeight * scale;
  float ratio = g_app.manifest.referenceWidth / g_app.manifest.referenceHeight;
  return static_cast<int>(std::lround(height * ratio * g_app.dpiScale));
}

int scaledHeight(float scale) {
  return static_cast<int>(std::lround(kBaseHeight * scale * g_app.dpiScale));
}

void applyViewportSize() {
  int width = scaledWidth(g_app.config.scale);
  int height = scaledHeight(g_app.config.scale);
  g_app.renderer.resize(width, height);
  g_app.player.setViewport(g_app.renderer.width(), g_app.renderer.height(), g_app.dpiScale);
}

void clampPosition(POINT& position, int width, int height) {
  HMONITOR monitor = MonitorFromPoint(position, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) return;
  const RECT& area = info.rcWork;
  position.x = (std::max)(area.left - width + 80, (std::min)(position.x, area.right - 80));
  position.y = (std::max)(area.top - height + 80, (std::min)(position.y, area.bottom - 40));
}

void defaultPosition(POINT& position, int width, int height) {
  RECT area{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
  position.x = area.right - width - 40;
  position.y = area.bottom - height - 40;
}

void moveWindowTo(POINT position) {
  int width = scaledWidth(g_app.config.scale);
  int height = scaledHeight(g_app.config.scale);
  clampPosition(position, width, height);
  g_app.windowPosition = position;
  SetWindowPos(g_app.window, HWND_TOPMOST, position.x, position.y, width, height,
               SWP_NOACTIVATE | SWP_NOSIZE);
}

void renderFrame() {
  g_app.renderer.begin();
  g_app.player.render(g_app.renderer);
  g_app.renderer.end();
  g_app.renderer.present(g_app.window, g_app.windowPosition.x, g_app.windowPosition.y);
}

double nowMilliseconds() {
  static LARGE_INTEGER frequency = [] {
    LARGE_INTEGER value{};
    QueryPerformanceFrequency(&value);
    return value;
  }();
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  return static_cast<double>(counter.QuadPart) * 1000.0 /
         static_cast<double>(frequency.QuadPart);
}

void updateFrameTimer() {
  bool visible = g_app.codexRunning && !g_app.userHidden;
  if (visible && !g_app.frameTimerRunning) {
    g_app.lastFrameMs = nowMilliseconds();
    g_app.nextFrameMs = g_app.lastFrameMs + kFrameIntervalMs;
    g_app.frameTimerRunning = true;
  } else if (!visible && g_app.frameTimerRunning) {
    g_app.frameTimerRunning = false;
  }
}

void updateVisibility() {
  bool visible = g_app.codexRunning && !g_app.userHidden;
  if (visible) {
    ShowWindow(g_app.window, SW_SHOWNOACTIVATE);
    SetWindowPos(g_app.window, g_app.config.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 g_app.windowPosition.x, g_app.windowPosition.y, 0, 0,
                 SWP_NOACTIVATE | SWP_NOSIZE);
    renderFrame();
  } else {
    ShowWindow(g_app.window, SW_HIDE);
  }
  updateFrameTimer();
}

void playPlan(PetPlan plan) {
  g_app.player.play(plan);
}

void applyScale(float scale, POINT anchorScreen, bool fromCursor) {
  float next = (std::min)(2.0f, (std::max)(0.4f, scale));
  POINT oldPosition = g_app.windowPosition;
  int oldWidth = scaledWidth(g_app.config.scale);
  int oldHeight = scaledHeight(g_app.config.scale);
  bool inside = anchorScreen.x >= oldPosition.x && anchorScreen.x <= oldPosition.x + oldWidth &&
                anchorScreen.y >= oldPosition.y && anchorScreen.y <= oldPosition.y + oldHeight;
  float worldX = 0.0f;
  float worldY = 0.0f;
  if (fromCursor && inside) {
    g_app.player.screenToWorld(static_cast<float>(anchorScreen.x - oldPosition.x),
                               static_cast<float>(anchorScreen.y - oldPosition.y), worldX, worldY);
  }
  g_app.config.scale = next;
  int newWidth = scaledWidth(next);
  int newHeight = scaledHeight(next);
  POINT position = oldPosition;
  applyViewportSize();
  if (fromCursor && inside) {
    float screenX = 0.0f;
    float screenY = 0.0f;
    g_app.player.worldToScreen(worldX, worldY, screenX, screenY);
    position.x = static_cast<LONG>(std::lround(anchorScreen.x - screenX));
    position.y = static_cast<LONG>(std::lround(anchorScreen.y - screenY));
  } else {
    position.x = oldPosition.x + (oldWidth - newWidth) / 2;
    position.y = oldPosition.y + (oldHeight - newHeight);
  }
  g_app.windowPosition = position;
  clampPosition(g_app.windowPosition, newWidth, newHeight);
  SetWindowPos(g_app.window, HWND_TOPMOST, g_app.windowPosition.x, g_app.windowPosition.y, newWidth,
               newHeight, SWP_NOACTIVATE);
  if (g_app.dragging) {
    GetCursorPos(&g_app.dragCursorStart);
    g_app.dragWindowStart = g_app.windowPosition;
  }
  renderFrame();
  saveConfig();
}

void openCodex() {
  ShellExecuteW(nullptr, L"open", L"codex://launch", nullptr, nullptr, SW_SHOWNORMAL);
}

HICON createIconFromPng(const std::wstring& path, int size) {
  std::vector<unsigned char> rgba;
  int width = 0;
  int height = 0;
  if (!loadImageRGBA(path, rgba, width, height) || width <= 0 || height <= 0) return nullptr;
  BITMAPV5HEADER header{};
  header.bV5Size = sizeof(header);
  header.bV5Width = size;
  header.bV5Height = -size;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00FF0000;
  header.bV5GreenMask = 0x0000FF00;
  header.bV5BlueMask = 0x000000FF;
  header.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits,
                                   nullptr, 0);
  if (!color || !bits) {
    if (color) DeleteObject(color);
    DeleteDC(dc);
    return nullptr;
  }
  auto* destination = static_cast<unsigned char*>(bits);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      int sx = x * width / size;
      int sy = y * height / size;
      const unsigned char* src = rgba.data() + (static_cast<size_t>(sy) * width + sx) * 4;
      unsigned char* dst = destination + (static_cast<size_t>(y) * size + x) * 4;
      unsigned int alpha = src[3];
      dst[0] = static_cast<unsigned char>(src[2] * alpha / 255);
      dst[1] = static_cast<unsigned char>(src[1] * alpha / 255);
      dst[2] = static_cast<unsigned char>(src[0] * alpha / 255);
      dst[3] = static_cast<unsigned char>(alpha);
    }
  }
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO info{};
  info.fIcon = TRUE;
  info.hbmColor = color;
  info.hbmMask = mask;
  HICON icon = CreateIconIndirect(&info);
  DeleteObject(mask);
  DeleteObject(color);
  DeleteDC(dc);
  return icon;
}

void showTrayMenu(HWND window) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kCmdToggleShow, g_app.userHidden ? L"显示宠物" : L"隐藏宠物");
  AppendMenuW(menu, MF_STRING, kCmdOpenCodex, L"打开 Codex");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  {
    HMENU form = CreatePopupMenu();
    AppendMenuW(form, MF_STRING | (g_app.stateMachine.form == Form::Base ? MF_CHECKED : 0),
                kCmdFormBase, L"基建形态");
    AppendMenuW(form, MF_STRING | (g_app.stateMachine.form == Form::Battle ? MF_CHECKED : 0),
                kCmdFormBattle, L"战斗形态");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(form), L"形态");
  }
  {
    HMENU state = CreatePopupMenu();
    if (g_app.stateMachine.form == Form::Base) {
      AppendMenuW(state,
                  MF_STRING | (g_app.stateMachine.baseState == BaseState::Stand ? MF_CHECKED : 0),
                  kCmdBaseStand, L"站");
      AppendMenuW(state,
                  MF_STRING | (g_app.stateMachine.baseState == BaseState::Sit ? MF_CHECKED : 0),
                  kCmdBaseSit, L"坐");
      AppendMenuW(state,
                  MF_STRING | (g_app.stateMachine.baseState == BaseState::Lie ? MF_CHECKED : 0),
                  kCmdBaseLie, L"躺");
    } else {
      AppendMenuW(state,
                  MF_STRING | (g_app.stateMachine.battleState == BattleState::Default ? MF_CHECKED : 0),
                  kCmdBattleDefault, L"默认");
      AppendMenuW(state,
                  MF_STRING | (g_app.stateMachine.battleState == BattleState::Skill1 ? MF_CHECKED : 0),
                  kCmdBattleSkill1, L"技能一");
      AppendMenuW(state,
                  MF_STRING | (g_app.stateMachine.battleState == BattleState::Skill2 ? MF_CHECKED : 0),
                  kCmdBattleSkill2, L"技能二");
      AppendMenuW(state,
                  MF_STRING | (g_app.stateMachine.battleState == BattleState::Skill3 ? MF_CHECKED : 0),
                  kCmdBattleSkill3, L"技能三");
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(state), L"状态");
  }
  {
    HMENU size = CreatePopupMenu();
    AppendMenuW(size, MF_STRING, kCmdScale50, L"50%");
    AppendMenuW(size, MF_STRING, kCmdScale56, L"56%");
    AppendMenuW(size, MF_STRING, kCmdScale70, L"70%");
    AppendMenuW(size, MF_STRING, kCmdScale85, L"85%");
    AppendMenuW(size, MF_STRING, kCmdScale100, L"100%");
    AppendMenuW(size, MF_STRING, kCmdScale125, L"125%");
    AppendMenuW(size, MF_STRING, kCmdScale150, L"150%");
    AppendMenuW(size, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(size, MF_STRING, kCmdScaleReset, L"恢复 56%");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(size), L"大小");
  }
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (g_app.config.alwaysOnTop ? MF_CHECKED : 0), kCmdToggleTop,
              L"始终置顶");
  AppendMenuW(menu, MF_STRING | (g_app.config.autostart ? MF_CHECKED : 0), kCmdToggleAutostart,
              L"随 Windows 启动");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdQuit, L"退出");

  POINT cursor{};
  GetCursorPos(&cursor);
  SetForegroundWindow(window);
  UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x,
                                cursor.y, 0, window, nullptr);
  PostMessageW(window, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (!command) return;

  POINT anchor = g_app.windowPosition;
  anchor.x += scaledWidth(g_app.config.scale) / 2;
  anchor.y += scaledHeight(g_app.config.scale) / 2;
  switch (command) {
    case kCmdToggleShow:
      g_app.userHidden = !g_app.userHidden;
      updateVisibility();
      break;
    case kCmdOpenCodex:
      openCodex();
      break;
    case kCmdFormBase:
      playPlan(g_app.stateMachine.setForm(Form::Base));
      saveConfig();
      break;
    case kCmdFormBattle:
      playPlan(g_app.stateMachine.setForm(Form::Battle));
      saveConfig();
      break;
    case kCmdBaseStand:
      playPlan(g_app.stateMachine.setBaseState(BaseState::Stand));
      g_app.config.form = Form::Base;
      g_app.config.baseState = BaseState::Stand;
      saveConfig();
      break;
    case kCmdBaseSit:
      playPlan(g_app.stateMachine.setBaseState(BaseState::Sit));
      g_app.config.form = Form::Base;
      g_app.config.baseState = BaseState::Sit;
      saveConfig();
      break;
    case kCmdBaseLie:
      playPlan(g_app.stateMachine.setBaseState(BaseState::Lie));
      g_app.config.form = Form::Base;
      g_app.config.baseState = BaseState::Lie;
      saveConfig();
      break;
    case kCmdBattleDefault:
      playPlan(g_app.stateMachine.setBattleState(BattleState::Default));
      g_app.config.form = Form::Battle;
      g_app.config.battleState = BattleState::Default;
      saveConfig();
      break;
    case kCmdBattleSkill1:
      playPlan(g_app.stateMachine.setBattleState(BattleState::Skill1));
      g_app.config.form = Form::Battle;
      g_app.config.battleState = BattleState::Skill1;
      saveConfig();
      break;
    case kCmdBattleSkill2:
      playPlan(g_app.stateMachine.setBattleState(BattleState::Skill2));
      g_app.config.form = Form::Battle;
      g_app.config.battleState = BattleState::Skill2;
      saveConfig();
      break;
    case kCmdBattleSkill3:
      playPlan(g_app.stateMachine.setBattleState(BattleState::Skill3));
      g_app.config.form = Form::Battle;
      g_app.config.battleState = BattleState::Skill3;
      saveConfig();
      break;
    case kCmdScale50:
      applyScale(0.50f, anchor, false);
      break;
    case kCmdScale56:
      applyScale(0.56f, anchor, false);
      break;
    case kCmdScale70:
      applyScale(0.70f, anchor, false);
      break;
    case kCmdScale85:
      applyScale(0.85f, anchor, false);
      break;
    case kCmdScale100:
      applyScale(1.00f, anchor, false);
      break;
    case kCmdScale125:
      applyScale(1.25f, anchor, false);
      break;
    case kCmdScale150:
      applyScale(1.50f, anchor, false);
      break;
    case kCmdScaleReset:
      applyScale(kDefaultScale, anchor, false);
      break;
    case kCmdToggleTop:
      g_app.config.alwaysOnTop = !g_app.config.alwaysOnTop;
      SetWindowPos(g_app.window, g_app.config.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      saveConfig();
      break;
    case kCmdToggleAutostart:
      g_app.config.autostart = !g_app.config.autostart;
      setAutostart(g_app.config.autostart);
      saveConfig();
      break;
    case kCmdQuit:
      PostMessageW(window, WM_CLOSE, 0, 0);
      break;
    default:
      break;
  }
}

void onPointerDown(HWND window, LPARAM lParam) {
  int x = GET_X_LPARAM(lParam);
  int y = GET_Y_LPARAM(lParam);
  if (!g_app.player.hitTest(static_cast<float>(x), static_cast<float>(y))) return;
  g_app.dragging = true;
  g_app.dragMoved = false;
  g_app.dragPlanStarted = false;
  g_app.dragMode = DragMode::None;
  g_app.dragLastSign = 0;
  GetCursorPos(&g_app.dragCursorStart);
  g_app.dragWindowStart = g_app.windowPosition;
  SetCapture(window);
}

void onPointerMove(HWND window, LPARAM lParam) {
  int x = GET_X_LPARAM(lParam);
  int y = GET_Y_LPARAM(lParam);
  if (!g_app.dragging) {
    bool over = g_app.player.hitTest(static_cast<float>(x), static_cast<float>(y));
    SetCursor(LoadCursorW(nullptr, over ? IDC_HAND : IDC_ARROW));
    return;
  }
  POINT cursor{};
  GetCursorPos(&cursor);
  int dx = cursor.x - g_app.dragCursorStart.x;
  int dy = cursor.y - g_app.dragCursorStart.y;
  if (!g_app.dragMoved && std::abs(dx) + std::abs(dy) > 3) {
    g_app.dragMoved = true;
    g_app.dragPlanStarted = true;
    PetPlan plan = g_app.stateMachine.dragStart();
    g_app.dragMode = plan.dragMode;
    playPlan(plan);
    SetCursor(LoadCursorW(nullptr, plan.dragMode == DragMode::None ? IDC_ARROW : IDC_SIZEALL));
  }
  if (!g_app.dragMoved || g_app.dragMode == DragMode::None) return;
  if (std::abs(dx) >= 2) {
    int sign = dx > 0 ? 1 : -1;
    if (sign != g_app.dragLastSign) {
      g_app.dragLastSign = sign;
      g_app.player.setFacing(sign);
    }
  }
  POINT target{g_app.dragWindowStart.x + dx, g_app.dragWindowStart.y + dy};
  moveWindowTo(target);
}

void onPointerUp(HWND window) {
  if (!g_app.dragging) return;
  g_app.dragging = false;
  ReleaseCapture();
  if (g_app.dragMoved) {
    playPlan(g_app.stateMachine.dragEnd());
    g_app.config.x = g_app.windowPosition.x;
    g_app.config.y = g_app.windowPosition.y;
    saveConfig();
    SetCursor(LoadCursorW(nullptr, IDC_HAND));
    return;
  }
  ULONGLONG now = GetTickCount64();
  if (g_app.lastClickTick && now - g_app.lastClickTick < 300) {
    g_app.lastClickTick = 0;
    openCodex();
    return;
  }
  g_app.lastClickTick = now;
  playPlan(g_app.stateMachine.click());
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_NCHITTEST: {
      POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(window, &point);
      if (!g_app.player.hitTest(static_cast<float>(point.x), static_cast<float>(point.y))) {
        return HTTRANSPARENT;
      }
      return HTCLIENT;
    }
    case WM_LBUTTONDOWN:
      onPointerDown(window, lParam);
      return 0;
    case WM_MOUSEMOVE:
      onPointerMove(window, lParam);
      return 0;
    case WM_LBUTTONUP:
      onPointerUp(window);
      return 0;
    case WM_MBUTTONDOWN: {
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      if (g_app.player.hitTest(static_cast<float>(x), static_cast<float>(y))) {
        POINT cursor{};
        GetCursorPos(&cursor);
        applyScale(kDefaultScale, cursor, true);
      }
      return 0;
    }
    case WM_MOUSEWHEEL: {
      if (!g_app.dragging) return 0;
      int delta = GET_WHEEL_DELTA_WPARAM(wParam);
      ULONGLONG now = GetTickCount64();
      if (now - g_app.lastWheelTick < 100) return 0;
      g_app.lastWheelTick = now;
      POINT cursor{};
      GetCursorPos(&cursor);
      applyScale(g_app.config.scale + (delta > 0 ? 0.06f : -0.06f), cursor, true);
      return 0;
    }
    case WM_XBUTTONDOWN: {
      if (g_app.stateMachine.busy) return TRUE;
      WORD button = GET_XBUTTON_WPARAM(wParam);
      playPlan(button == XBUTTON1 ? g_app.stateMachine.cycleState()
                                  : g_app.stateMachine.toggleForm());
      g_app.config.form = g_app.stateMachine.form;
      g_app.config.baseState = g_app.stateMachine.baseState;
      g_app.config.battleState = g_app.stateMachine.battleState;
      saveConfig();
      return TRUE;
    }
    case WM_RBUTTONUP: {
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      if (g_app.player.hitTest(static_cast<float>(x), static_cast<float>(y))) {
        showTrayMenu(window);
      }
      return 0;
    }
    case WM_TIMER:
      if (wParam == kTimerWatch) {
        bool running = isCodexRunning();
        if (running != g_app.codexRunning) {
          g_app.codexRunning = running;
          updateVisibility();
        }
        return 0;
      }
      break;
    case kTrayMessage:
      if (LOWORD(lParam) == WM_LBUTTONUP) {
        g_app.userHidden = !g_app.userHidden;
        updateVisibility();
      } else if (LOWORD(lParam) == WM_RBUTTONUP) {
        showTrayMenu(window);
      }
      return 0;
    case WM_CLOSE:
      g_app.quitting = true;
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      KillTimer(window, kTimerWatch);
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

int runSnapshot(const std::wstring& outputPath) {
  PETLOG("pet: snapshot form=%d base=%d battle=%d scale=%.2f\n",
         static_cast<int>(g_app.config.form), static_cast<int>(g_app.config.baseState),
         static_cast<int>(g_app.config.battleState), g_app.config.scale);
  if (!g_app.renderer.init()) return 2;
  if (!g_app.player.load(g_app.renderer, g_app.manifest, g_app.config.form)) return 3;
  g_app.player.setSlotFilter(g_slotFilter);
  g_app.player.setReverseOrder(g_reverseOrder);
  applyViewportSize();
  g_app.stateMachine.reset(g_app.config.form, g_app.config.baseState, g_app.config.battleState);
  g_app.player.play(g_app.stateMachine.idlePlan());
  for (int i = 0; i < snapshotFrames; ++i) g_app.player.update(1.0f / 30.0f);
  g_app.renderer.begin();
  g_app.player.render(g_app.renderer);
  g_app.renderer.end();
  if (!g_app.renderer.savePng(outputPath)) return 4;
  g_app.player.shutdown();
  g_app.renderer.shutdown();
  return 0;
}

void printPlan(const char* label, const PetPlan& plan) {
  printf("%-28s busy=%d drag=%d:", label, plan.busy ? 1 : 0, static_cast<int>(plan.dragMode));
  for (const Step& step : plan.steps) {
    printf(" %s/%s%s", step.model == Form::Base ? "base" : "battle", step.anim.c_str(),
           step.loop ? "(loop)" : "");
  }
  printf("\n");
}

void testPlan(PetStateMachine& sm, const char* label, PetPlan plan) {
  printPlan(label, plan);
  sm.markIdle();
}

void runSelfTest() {
  PetStateMachine sm;
  testPlan(sm, "base idle", sm.idlePlan());
  testPlan(sm, "base click", sm.click());
  testPlan(sm, "base drag start", sm.dragStart());
  testPlan(sm, "base drag end", sm.dragEnd());
  testPlan(sm, "base -> sit", sm.setBaseState(BaseState::Sit));
  testPlan(sm, "base -> lie", sm.setBaseState(BaseState::Lie));
  testPlan(sm, "base -> stand", sm.setBaseState(BaseState::Stand));
  testPlan(sm, "form -> battle(default)", sm.setForm(Form::Battle));
  testPlan(sm, "battle click(default)", sm.click());
  testPlan(sm, "battle drag start(default)", sm.dragStart());
  testPlan(sm, "battle drag end(default)", sm.dragEnd());
  testPlan(sm, "state -> skill1", sm.setBattleState(BattleState::Skill1));
  testPlan(sm, "battle click(skill1)", sm.click());
  testPlan(sm, "battle drag start(skill1)", sm.dragStart());
  testPlan(sm, "state -> skill2", sm.setBattleState(BattleState::Skill2));
  testPlan(sm, "battle drag start(skill2)", sm.dragStart());
  testPlan(sm, "battle drag end(skill2)", sm.dragEnd());
  testPlan(sm, "battle click(skill2)", sm.click());
  testPlan(sm, "state -> skill3", sm.setBattleState(BattleState::Skill3));
  testPlan(sm, "battle drag start(skill3)", sm.dragStart());
  testPlan(sm, "battle drag end(skill3)", sm.dragEnd());
  testPlan(sm, "battle click(skill3)", sm.click());
  testPlan(sm, "state -> default", sm.setBattleState(BattleState::Default));
  testPlan(sm, "form -> base", sm.setForm(Form::Base));
  testPlan(sm, "form -> battle(default)", sm.setForm(Form::Battle));
  testPlan(sm, "form -> base", sm.setForm(Form::Base));
  testPlan(sm, "state -> skill3 (from base)", sm.setBattleState(BattleState::Skill3));
  testPlan(sm, "form -> base (skill3)", sm.setForm(Form::Base));
  testPlan(sm, "form -> battle (skill3)", sm.setForm(Form::Battle));
  testPlan(sm, "state -> default (from battle)", sm.setBattleState(BattleState::Default));
}

void runDump(const std::string& key) {
  if (!g_app.renderer.init()) return;
  if (!g_app.player.load(g_app.renderer, g_app.manifest, Form::Base)) return;
  g_app.player.dumpAnimation(key);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  SetProcessDPIAware();
  g_app.instance = instance;
  g_app.exePath = [] {
    wchar_t buffer[MAX_PATH * 4];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
    return std::wstring(buffer, length);
  }();
  g_app.assetRoot = findAssetRoot();
  g_app.configPath = configDirectory() + L"\\config.json";
  loadConfig(g_app.config);
  if (!loadOperatorManifest(g_app.assetRoot, "char_1015_aglna2", g_app.manifest)) {
    MessageBoxW(nullptr, L"无法读取干员资源，请确认 assets/operators 目录存在。", L"Codex Pet",
                MB_ICONERROR);
    return 1;
  }

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::wstring snapshotPath;
  bool selfTest = false;
  std::string dumpKey;
  for (int i = 1; i < argc; ++i) {
    if (_wcsicmp(argv[i], L"--snapshot") == 0 && i + 1 < argc) {
      snapshotPath = argv[i + 1];
    } else if (_wcsicmp(argv[i], L"--selftest") == 0) {
      selfTest = true;
    } else if (_wcsicmp(argv[i], L"--dump") == 0 && i + 1 < argc) {
      dumpKey = narrow(argv[i + 1]);
    } else if (_wcsicmp(argv[i], L"--slot") == 0 && i + 1 < argc) {
      g_slotFilter = narrow(argv[i + 1]);
    } else if (_wcsicmp(argv[i], L"--reverse") == 0) {
      g_reverseOrder = true;
    } else if (_wcsicmp(argv[i], L"--scale") == 0 && i + 1 < argc) {
      g_app.config.scale = static_cast<float>(_wtof(argv[i + 1]));
    } else if (_wcsicmp(argv[i], L"--form") == 0 && i + 1 < argc) {
      g_app.config.form = _wcsicmp(argv[i + 1], L"battle") == 0 ? Form::Battle : Form::Base;
    } else if (_wcsicmp(argv[i], L"--base") == 0 && i + 1 < argc) {
      g_app.config.baseState = _wcsicmp(argv[i + 1], L"sit") == 0
                                   ? BaseState::Sit
                                   : (_wcsicmp(argv[i + 1], L"lie") == 0 ? BaseState::Lie
                                                                         : BaseState::Stand);
    } else if (_wcsicmp(argv[i], L"--battle") == 0 && i + 1 < argc) {
      g_app.config.battleState =
          _wcsicmp(argv[i + 1], L"skill1") == 0
              ? BattleState::Skill1
              : (_wcsicmp(argv[i + 1], L"skill2") == 0
                     ? BattleState::Skill2
                     : (_wcsicmp(argv[i + 1], L"skill3") == 0 ? BattleState::Skill3
                                                              : BattleState::Default));
    } else if (_wcsicmp(argv[i], L"--frames") == 0 && i + 1 < argc) {
      snapshotFrames = _wtoi(argv[i + 1]);
    }
  }
  if (argv) LocalFree(argv);

  if (selfTest) {
    runSelfTest();
    return 0;
  }
  if (!dumpKey.empty()) {
    runDump(dumpKey);
    return 0;
  }

  using SetDpiContextFn = BOOL(WINAPI*)(HANDLE);
  using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  auto setDpiContext = reinterpret_cast<SetDpiContextFn>(
      reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
  if (setDpiContext) {
    setDpiContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
  } else {
    SetProcessDPIAware();
  }
  auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
      reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
  g_app.dpiScale = 1.0f;

  if (!snapshotPath.empty()) {
    int code = runSnapshot(snapshotPath);
    return code;
  }

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ArknightsOperators4CodexPetNative");
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    return 0;
  }

  int width = scaledWidth(g_app.config.scale);
  int height = scaledHeight(g_app.config.scale);
  POINT position{g_app.config.x, g_app.config.y};
  if (g_app.config.x == INT_MIN || g_app.config.y == INT_MIN) {
    defaultPosition(position, width, height);
  }
  clampPosition(position, width, height);
  g_app.windowPosition = position;

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = windowProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = L"ArknightsOperators4CodexPetNative";
  g_app.icon = createIconFromPng(g_app.manifest.avatarPath, 32);
  g_app.smallIcon = createIconFromPng(g_app.manifest.avatarPath, 16);
  windowClass.hIcon = g_app.icon;
  windowClass.hIconSm = g_app.smallIcon;
  if (!RegisterClassExW(&windowClass)) return 1;

  g_app.window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
                                     WS_EX_NOACTIVATE,
                                 windowClass.lpszClassName, L"Codex Pet", WS_POPUP, position.x,
                                 position.y, width, height, nullptr, nullptr, instance, nullptr);
  if (!g_app.window) return 1;
  if (getDpiForWindow) {
    UINT dpi = getDpiForWindow(g_app.window);
    PETLOG("pet: window dpi %u\n", dpi);
    if (dpi >= 96) g_app.dpiScale = static_cast<float>(dpi) / 96.0f;
  }
  width = scaledWidth(g_app.config.scale);
  height = scaledHeight(g_app.config.scale);
  if (g_app.config.x == INT_MIN || g_app.config.y == INT_MIN) {
    defaultPosition(position, width, height);
  }
  clampPosition(position, width, height);
  g_app.windowPosition = position;
  if (!g_app.renderer.initForWindow(g_app.window)) {
    MessageBoxW(nullptr, L"无法初始化 Direct3D 11 / DirectComposition。", L"Codex Pet",
                MB_ICONERROR);
    return 1;
  }
  if (!g_app.player.load(g_app.renderer, g_app.manifest, g_app.config.form)) {
    MessageBoxW(nullptr, L"无法加载 Spine 模型。", L"Codex Pet", MB_ICONERROR);
    return 1;
  }
  g_app.stateMachine.reset(g_app.config.form, g_app.config.baseState, g_app.config.battleState);
  g_app.player.setOnIdle([] { g_app.stateMachine.markIdle(); });
  applyViewportSize();
  g_app.player.play(g_app.stateMachine.idlePlan());
  SetWindowPos(g_app.window, HWND_TOPMOST, position.x, position.y, width, height, SWP_NOACTIVATE);
  PETLOG("pet: dpi=%.2f size=%dx%d\n", g_app.dpiScale, width, height);
  renderFrame();

  NOTIFYICONDATAW tray{};
  tray.cbSize = sizeof(tray);
  tray.hWnd = g_app.window;
  tray.uID = kTrayId;
  tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  tray.uCallbackMessage = kTrayMessage;
  tray.hIcon = g_app.icon ? g_app.icon : LoadIconW(nullptr, IDI_APPLICATION);
  wcsncpy(tray.szTip, L"予愿安洁莉娜 · Codex Pet", 127);
  tray.szTip[127] = L'\0';
  Shell_NotifyIconW(NIM_ADD, &tray);

  g_app.codexRunning = isCodexRunning();
  if (g_app.codexRunning) {
    ShowWindow(g_app.window, SW_SHOWNOACTIVATE);
  }
  updateFrameTimer();
  SetTimer(g_app.window, kTimerWatch, 2000, nullptr);
  timeBeginPeriod(1);

  MSG message{};
  while (!g_app.quitting) {
    DWORD timeout = INFINITE;
    if (g_app.frameTimerRunning) {
      double now = nowMilliseconds();
      double wait = g_app.nextFrameMs - now;
      timeout = wait > 0.0 ? static_cast<DWORD>(wait) + 1 : 0;
    }
    MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        g_app.quitting = true;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (g_app.quitting) break;
    if (!g_app.frameTimerRunning) continue;
    double now = nowMilliseconds();
    if (now < g_app.nextFrameMs) continue;
    float delta = static_cast<float>((now - g_app.lastFrameMs) / 1000.0);
    g_app.lastFrameMs = now;
#ifdef PET_CONSOLE
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
#endif
    g_app.player.update(delta);
    renderFrame();
#ifdef PET_CONSOLE
    QueryPerformanceCounter(&t1);
    static double statStart = now;
    static int statFrames = 0;
    static double statMs = 0.0;
    statFrames++;
    statMs += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
    if (now - statStart >= 2000.0) {
      double seconds = (now - statStart) / 1000.0;
      PETLOG("pet: fps=%.1f frame=%.2fms\n", statFrames / seconds, statMs / statFrames);
      statStart = now;
      statFrames = 0;
      statMs = 0.0;
    }
#endif
    g_app.nextFrameMs += kFrameIntervalMs;
    if (g_app.nextFrameMs <= now) g_app.nextFrameMs = now + kFrameIntervalMs;
  }
  timeEndPeriod(1);
  NOTIFYICONDATAW removeTray{};
  removeTray.cbSize = sizeof(removeTray);
  removeTray.hWnd = g_app.window;
  removeTray.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &removeTray);
  g_app.player.shutdown();
  g_app.renderer.shutdown();
  if (g_app.icon) DestroyIcon(g_app.icon);
  if (g_app.smallIcon) DestroyIcon(g_app.smallIcon);
  if (mutex) CloseHandle(mutex);
  return 0;
}

#ifdef PET_CONSOLE
int main() { return wWinMain(GetModuleHandleW(nullptr), nullptr, nullptr, 0); }
#endif
