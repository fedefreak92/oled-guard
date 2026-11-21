#include "app.h"

#include <shellapi.h>

#include <algorithm>

#include "diagnostics.h"
#include "logger.h"
#include "paths.h"
#include "text.h"

namespace oled {
namespace {

constexpr wchar_t kMainClass[] = L"OledGuardMain";
constexpr UINT_PTR kTimerBeat = 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT kCmdExit = 100;

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  // Il processo termina: il sistema distrugge l'overlay e il desktop torna normale.
  LogError("Eccezione non gestita 0x{:08X} a {}, chiusura",
           static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
           info->ExceptionRecord->ExceptionAddress);
  LogShutdown();
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

int App::Run(HINSTANCE instance) {
  instance_ = instance;
  SetUnhandledExceptionFilter(CrashFilter);
  LogInit(LogDir(), LogLevel::Info);
  LogInfo("OLED Guard avviato");

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) args_.emplace_back(argv[i]);
  LocalFree(argv);
  if (HasArg(L"--debug")) LogSetLevel(LogLevel::Debug);
  if (HasArg(L"--list-monitors")) {
    const int code = RunListMonitors(config_.monitor);
    LogShutdown();
    return code;
  }

  if (const auto exit_key = ParseHotkey(config_.hotkey_emergency_exit)) guard_.Start(*exit_key);

  if (!CreateMainWindow()) {
    LogError("Impossibile creare la finestra principale");
    return 1;
  }
  SetTimer(main_, kTimerBeat, 1000, nullptr);
  guard_.BeatMain();

  taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");
  icon_ = CreateAppIcon(GetSystemMetrics(SM_CXSMICON));
  tray_.Create(main_, WM_APP_TRAY, icon_);
  tray_.SetTooltip(L"OLED Guard");

  StartOverlay();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  StopOverlay();
  tray_.Destroy();
  if (icon_) DestroyIcon(icon_);
  guard_.Stop();
  LogInfo("OLED Guard chiuso");
  LogShutdown();
  return 0;
}

bool App::HasArg(std::wstring_view arg) const {
  return std::find(args_.begin(), args_.end(), arg) != args_.end();
}

bool App::CreateMainWindow() {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance_;
  wc.lpszClassName = kMainClass;
  RegisterClassExW(&wc);
  // Finestra top-level mai mostrata: riceve i messaggi broadcast (TaskbarCreated)
  // che una finestra HWND_MESSAGE non riceverebbe.
  main_ = CreateWindowExW(WS_EX_TOOLWINDOW, kMainClass, L"OLED Guard", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                          instance_, this);
  return main_ != nullptr;
}

bool App::StartOverlay() {
  if (overlay_running_) return true;
  monitor_ = FindMonitor(config_.monitor);
  if (!monitor_) {
    LogWarn("Monitor \"{}\" non trovato", config_.monitor);
    return false;
  }
  if (!overlay_.Create(instance_, monitor_->rect)) return false;

  RenderParams params;
  params.overlay = overlay_.Hwnd();
  params.main = main_;
  params.monitor = *monitor_;
  params.allow_tearing = config_.allow_tearing;
  params.shift = nullptr;
  params.guard = &guard_;
  if (!render_.Start(params)) {
    overlay_.Destroy();
    return false;
  }
  overlay_running_ = true;
  LogInfo("Overlay avviato su \"{}\" ({}x{})", Narrow(monitor_->friendly_name), monitor_->rect.right - monitor_->rect.left,
          monitor_->rect.bottom - monitor_->rect.top);
  return true;
}

void App::StopOverlay() {
  if (!overlay_running_) return;
  guard_.SetOverlayActive(false);
  // Prima il thread (rilascia device, duplicazione, swap chain), poi la finestra.
  render_.Stop();
  overlay_.Destroy();
  overlay_running_ = false;
  LogInfo("Overlay fermato");
}

void App::OnRenderEvent(RenderEvent event) {
  switch (event) {
    case RenderEvent::FirstFrameShown:
      LogInfo("Overlay a schermo");
      guard_.SetOverlayActive(true);
      break;
    case RenderEvent::CaptureLost:
      guard_.SetOverlayActive(false);
      break;
    case RenderEvent::CaptureFailed:
      LogWarn("Cattura non disponibile: overlay disattivato");
      StopOverlay();
      break;
    case RenderEvent::DisplayRotated:
      LogWarn("Display ruotato: overlay disattivato");
      StopOverlay();
      break;
  }
}

void App::OnTrayEvent(UINT event) {
  if (event == WM_CONTEXTMENU || event == NIN_SELECT || event == NIN_KEYSELECT) ShowTrayMenu();
}

void App::ShowTrayMenu() {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kCmdExit, L"Esci");
  POINT pt;
  GetCursorPos(&pt);
  // Necessario perche' il menu si chiuda cliccando altrove.
  SetForegroundWindow(main_);
  const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, main_, nullptr);
  PostMessageW(main_, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (cmd == kCmdExit) Quit();
}

void App::Quit() {
  StopOverlay();
  DestroyWindow(main_);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app && app->main_ == hwnd) return app->HandleMessage(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == taskbar_created_msg_ && taskbar_created_msg_ != 0) {
    tray_.Recreate();
    return 0;
  }
  switch (msg) {
    case WM_TIMER:
      if (wp == kTimerBeat) guard_.BeatMain();
      return 0;
    case WM_APP_TRAY:
      OnTrayEvent(LOWORD(lp));
      return 0;
    case WM_APP_RENDER_EVENT:
      OnRenderEvent(static_cast<RenderEvent>(wp));
      return 0;
    case WM_DISPLAYCHANGE:
      // Risoluzione o disposizione cambiate: si ricrea l'overlay con le nuove misure.
      LogInfo("Configurazione dei display cambiata");
      if (overlay_running_) {
        StopOverlay();
        StartOverlay();
      }
      return 0;
    case WM_CLOSE:
      Quit();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(main_, msg, wp, lp);
  }
}

}  // namespace oled
