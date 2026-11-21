#include "app.h"

#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <random>

#include "clock.h"
#include "diagnostics.h"
#include "logger.h"
#include "paths.h"
#include "text.h"

namespace oled {
namespace {

constexpr wchar_t kMainClass[] = L"OledGuardMain";
constexpr UINT_PTR kTimerBeat = 1;
constexpr UINT_PTR kTimerPause = 2;
constexpr UINT kPausePollMs = 250;
constexpr double kCaptureRetryMs = 30000;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT kCmdExit = 100;
constexpr UINT kCmdToggle = 101;

App* g_app = nullptr;  // per il callback di SetWinEventHook

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  // Il processo termina: il sistema distrugge l'overlay e il desktop torna normale.
  LogError("Eccezione non gestita 0x{:08X} a {}, chiusura",
           static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
           info->ExceptionRecord->ExceptionAddress);
  LogShutdown();
  return EXCEPTION_EXECUTE_HANDLER;
}

bool IsSupportedRotation(DXGI_MODE_ROTATION r) {
  return r == DXGI_MODE_ROTATION_IDENTITY || r == DXGI_MODE_ROTATION_UNSPECIFIED;
}

}  // namespace

int App::Run(HINSTANCE instance) {
  instance_ = instance;
  g_app = this;
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

  ShiftTiming timing = ToShiftTiming(config_);
  if (HasArg(L"--fast-shift")) {
    // Solo per la verifica a occhio: soste di 3-5 s e transizioni di 1-2 s.
    timing = ShiftTiming{3000, 5000, 1000, 2000};
    LogInfo("Modalita' --fast-shift attiva");
  }
  std::random_device seed_source;
  const uint64_t seed = (static_cast<uint64_t>(seed_source()) << 32) ^ seed_source();
  shift_ = std::make_unique<SharedShift>(seed, config_.amplitude_px, timing, NowMs());
  hysteresis_.SetDelays(config_.pause_enter_delay_ms, config_.resume_delay_ms);

  if (const auto exit_key = ParseHotkey(config_.hotkey_emergency_exit)) guard_.Start(*exit_key);

  if (!CreateMainWindow()) {
    LogError("Impossibile creare la finestra principale");
    return 1;
  }
  SetTimer(main_, kTimerBeat, 1000, nullptr);
  SetTimer(main_, kTimerPause, kPausePollMs, nullptr);
  guard_.BeatMain();
  WTSRegisterSessionNotification(main_, NOTIFY_FOR_THIS_SESSION);
  foreground_hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundHook, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");
  icon_ = CreateAppIcon(GetSystemMetrics(SM_CXSMICON));
  tray_.Create(main_, WM_APP_TRAY, icon_);

  Tick();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  StopOverlay();
  if (foreground_hook_) UnhookWinEvent(foreground_hook_);
  tray_.Destroy();
  if (icon_) DestroyIcon(icon_);
  guard_.Stop();
  g_app = nullptr;
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

void App::RefreshMonitor() {
  monitor_ = FindMonitor(config_.monitor);
  monitor_valid_ = true;
  // Un display ruotato resta in pausa finche' la configurazione non cambia.
  if (monitor_ && !IsSupportedRotation(monitor_->rotation)) display_rotated_ = true;
}

void App::Tick() {
  const double now = NowMs();
  if (!monitor_valid_) RefreshMonitor();

  PauseInputs in = pause_monitor_.Collect(monitor_ ? &*monitor_ : nullptr);
  in.manual = manual_pause_;
  in.capture_failed = now < capture_failed_until_ms_;
  in.display_rotated = display_rotated_;

  const PauseReason raw = EvaluatePause(in, ToPauseRules(config_));
  if (raw != last_raw_ || in.notification_state != last_notification_) {
    LogDebug("Segnali: motivo grezzo \"{}\", notifica {}, copre monitor {}, processo {}, overlay {}",
             PauseReasonText(raw), static_cast<int>(in.notification_state), in.foreground_covers_monitor,
             in.foreground_process, overlay_running_);
    last_raw_ = raw;
    last_notification_ = in.notification_state;
  }
  const bool paused = hysteresis_.Update(raw, now);
  const PauseReason reason = hysteresis_.Reason();

  if (first_tick_ || paused != last_paused_ || (paused && reason != last_reason_)) {
    if (paused) {
      LogInfo("In pausa: {} (processo in primo piano: {})", PauseReasonText(reason),
              in.foreground_process.empty() ? "-" : in.foreground_process);
    } else {
      LogInfo("Attivo");
    }
    first_tick_ = false;
    last_paused_ = paused;
    last_reason_ = reason;
    UpdateTrayTooltip();
  }

  if (!paused && !overlay_running_) {
    if (!StartOverlay()) capture_failed_until_ms_ = now + kCaptureRetryMs;
  } else if (paused && overlay_running_) {
    StopOverlay();
  }
}

bool App::StartOverlay() {
  if (overlay_running_) return true;
  if (!monitor_) return false;
  if (!overlay_.Create(instance_, monitor_->rect)) return false;

  RenderParams params;
  params.overlay = overlay_.Hwnd();
  params.main = main_;
  params.monitor = *monitor_;
  params.allow_tearing = config_.allow_tearing;
  params.shift = shift_.get();
  params.guard = &guard_;
  // Si riparte dall'ultima posizione mostrata, con una sosta nuova.
  shift_->Resume(NowMs());
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
      LogWarn("Cattura non disponibile: nuovo tentativo tra {} s", kCaptureRetryMs / 1000);
      StopOverlay();
      capture_failed_until_ms_ = NowMs() + kCaptureRetryMs;
      Tick();
      break;
    case RenderEvent::DisplayRotated:
      StopOverlay();
      display_rotated_ = true;
      Tick();
      break;
  }
}

void App::SetManualPause(bool paused) {
  manual_pause_ = paused;
  LogInfo("Pausa manuale {}", paused ? "attivata" : "disattivata");
  Tick();
}

void App::UpdateTrayTooltip() {
  std::wstring text = L"OLED Guard — ";
  text += last_paused_ ? L"In pausa: " + Widen(PauseReasonText(last_reason_)) : L"Attivo";
  tray_.SetTooltip(text);
}

void App::OnTrayEvent(UINT event) {
  if (event == WM_CONTEXTMENU || event == NIN_SELECT || event == NIN_KEYSELECT) ShowTrayMenu();
}

void App::ShowTrayMenu() {
  HMENU menu = CreatePopupMenu();
  const std::wstring state =
      last_paused_ ? L"In pausa (" + Widen(PauseReasonText(last_reason_)) + L")" : std::wstring(L"Attivo");
  AppendMenuW(menu, MF_STRING | (last_paused_ ? MF_UNCHECKED : MF_CHECKED), kCmdToggle, state.c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdExit, L"Esci");
  POINT pt;
  GetCursorPos(&pt);
  // Necessario perche' il menu si chiuda cliccando altrove.
  SetForegroundWindow(main_);
  const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, main_, nullptr);
  PostMessageW(main_, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (cmd == kCmdToggle) SetManualPause(!manual_pause_);
  if (cmd == kCmdExit) Quit();
}

void App::Quit() {
  StopOverlay();
  WTSUnRegisterSessionNotification(main_);
  DestroyWindow(main_);
}

void CALLBACK App::ForegroundHook(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
  if (!g_app) return;
  // La barra delle applicazioni o altre finestre topmost possono salire sopra l'overlay.
  g_app->overlay_.ReassertTopmost();
  g_app->Tick();
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
    UpdateTrayTooltip();
    return 0;
  }
  switch (msg) {
    case WM_TIMER:
      if (wp == kTimerBeat) guard_.BeatMain();
      if (wp == kTimerPause) Tick();
      return 0;
    case WM_APP_TRAY:
      OnTrayEvent(LOWORD(lp));
      return 0;
    case WM_APP_RENDER_EVENT:
      OnRenderEvent(static_cast<RenderEvent>(wp));
      return 0;
    case WM_WTSSESSION_CHANGE:
      pause_monitor_.OnSessionChange(wp);
      Tick();
      return 0;
    case WM_DISPLAYCHANGE:
      // Risoluzione, rotazione o monitor collegati/scollegati: si ricrea l'overlay.
      LogInfo("Configurazione dei display cambiata");
      display_rotated_ = false;
      monitor_valid_ = false;
      StopOverlay();
      Tick();
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
