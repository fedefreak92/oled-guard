#include "app.h"

#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <functional>
#include <random>
#include <thread>

#include "ascii.h"
#include "autostart.h"
#include "clock.h"
#include "diagnostics.h"
#include "logger.h"
#include "monitor_select.h"
#include "paths.h"
#include "text.h"

namespace oled {
namespace {

constexpr wchar_t kMainClass[] = L"OledGuardMain";
constexpr UINT_PTR kTimerBeat = 1;
constexpr UINT_PTR kTimerPause = 2;
constexpr UINT_PTR kTimerConfigReload = 3;
constexpr UINT kPausePollMs = 250;
constexpr UINT kConfigQuietMs = 300;
constexpr double kCaptureRetryMs = 30000;
constexpr int kToggleHotkeyId = 1;

constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_CONFIG_CHANGED = WM_APP + 3;
constexpr UINT WM_APP_SECOND_INSTANCE = WM_APP + 4;

constexpr UINT kCmdExit = 100;
constexpr UINT kCmdToggle = 101;
constexpr UINT kCmdOpenConfig = 102;
constexpr UINT kCmdOpenLogs = 103;
constexpr UINT kCmdAutostart = 104;
constexpr UINT kCmdMonitorAuto = 110;
constexpr UINT kCmdMonitorFirst = 111;  // 111.. una voce per monitor

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

// ShellExecute su un thread proprio con COM inizializzato: puo' bloccarsi (estensioni
// della shell) e non deve fermare il thread UI mentre il watchdog e' armato.
void RunShellAction(std::function<void()> action) {
  std::thread([action = std::move(action)] {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    action();
    if (SUCCEEDED(hr)) CoUninitialize();
  }).detach();
}

LogLevel ParseLogLevel(const std::string& level) {
  if (level == "debug") return LogLevel::Debug;
  if (level == "warn") return LogLevel::Warn;
  if (level == "error") return LogLevel::Error;
  return LogLevel::Info;
}

}  // namespace

int App::Run(HINSTANCE instance) {
  instance_ = instance;
  g_app = this;
  SetUnhandledExceptionFilter(CrashFilter);
  LogInit(LogDir(), LogLevel::Info);

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) args_.emplace_back(argv[i]);
  LocalFree(argv);
  debug_ = HasArg(L"--debug");
  fast_shift_ = HasArg(L"--fast-shift");

  if (HasArg(L"--list-monitors")) {
    LoadInitialConfig();
    const int code = RunListMonitors(config_.monitor);
    LogShutdown();
    return code;
  }

  if (!single_instance_.Acquire()) {
    // Avvisa l'istanza gia' attiva, che mostra un messaggio nella tray.
    if (HWND other = FindWindowW(kMainClass, nullptr)) PostMessageW(other, WM_APP_SECOND_INSTANCE, 0, 0);
    LogInfo("Un'altra istanza e' gia' in esecuzione: chiusura");
    LogShutdown();
    return 0;
  }

  LogInfo("OLED Guard avviato");
  LoadInitialConfig();
  if (fast_shift_) LogInfo("Modalita' --fast-shift attiva");

  std::random_device seed_source;
  const uint64_t seed = (static_cast<uint64_t>(seed_source()) << 32) ^ seed_source();
  shift_ = std::make_unique<SharedShift>(seed, config_.amplitude_px, EffectiveTiming(), NowMs());
  hysteresis_.SetDelays(config_.pause_enter_delay_ms, config_.resume_delay_ms);

  StartEmergencyGuard();

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
  tray_ready_ = true;
  // Avvisi nati prima che la tray esistesse (config, hotkey di emergenza).
  for (const auto& [title, text] : pending_notices_) tray_.ShowBalloon(title, text);
  pending_notices_.clear();
  RegisterToggleHotkey();
  // Con una config illeggibile i valori sono quelli predefiniti: non si tocca il
  // registro, altrimenti un errore di battitura toglierebbe l'avvio automatico.
  if (!initial_config_failed_) SyncAutostart();
  config_file_.StartWatching(main_, WM_APP_CONFIG_CHANGED);

  Tick();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  config_file_.StopWatching();
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

// ---------------------------------------------------------------- configurazione

void App::LoadInitialConfig() {
  ConfigLoadResult result = config_file_.Load();
  initial_config_failed_ = result.parse_failed;
  if (!result.parse_failed) config_ = NormalizedConfig(result.config);
  LogSetLevel(debug_ ? LogLevel::Debug : ParseLogLevel(config_.log_level));
  LogInfo("Configurazione: {}", Narrow(config_file_.Path().wstring()));
  ReportConfigWarnings(result, /*at_startup=*/true);
}

Config App::NormalizedConfig(Config config) {
  // Nomi .exe in minuscolo anche oltre l'ASCII, come quelli letti dai processi.
  for (auto* list : {&config.denylist, &config.allowlist}) {
    for (auto& name : *list) name = LowerUnicode(name);
  }
  return config;
}

void App::ShowNotice(const std::wstring& title, const std::wstring& text) {
  LogDebug("Notifica{}: {} - {}", tray_ready_ ? "" : " (in attesa della tray)", Narrow(title), Narrow(text));
  if (tray_ready_) {
    tray_.ShowBalloon(title, text);
  } else {
    pending_notices_.emplace_back(title, text);
  }
}

void App::SyncAutostart() {
  // Ripara anche un percorso vecchio (exe spostato).
  const AutostartState state = GetAutostartState();
  if (config_.autostart && state != AutostartState::ThisExe) SetAutostart(true);
  if (!config_.autostart && state != AutostartState::Missing) SetAutostart(false);
}

void App::ReportConfigWarnings(const ConfigLoadResult& result, bool at_startup) {
  for (const auto& warning : result.warnings) LogWarn("Configurazione: {}", warning);
  if (result.parse_failed && at_startup) {
    ShowNotice(L"Configurazione non valida",
               L"Il file non si legge: uso i valori predefiniti finché non lo correggi. Dettagli nel log.");
  } else if (result.parse_failed) {
    ShowNotice(L"Configurazione non valida",
                      L"Il file non è un JSON valido: tengo i valori precedenti. Dettagli nel log.");
  } else if (!result.warnings.empty()) {
    ShowNotice(L"Configurazione",
                      std::to_wstring(result.warnings.size()) + L" avvisi: alcuni valori sono stati sostituiti "
                                                                L"con i predefiniti. Dettagli nel log.");
  }
}

void App::ReloadConfig() {
  if (!config_file_.ChangedOnDisk()) return;
  const ConfigLoadResult result = config_file_.Load();
  ReportConfigWarnings(result, /*at_startup=*/false);
  if (result.parse_failed) return;
  LogInfo("Configurazione ricaricata");
  initial_config_failed_ = false;
  ApplyConfig(NormalizedConfig(result.config));
}

ShiftTiming App::EffectiveTiming() const {
  // --fast-shift: solo per la verifica a occhio, soste di 3-5 s e transizioni di 1-2 s.
  return fast_shift_ ? ShiftTiming{3000, 5000, 1000, 2000} : ToShiftTiming(config_);
}

void App::ApplyConfig(const Config& next) {
  const Config previous = config_;
  config_ = next;
  LogSetLevel(debug_ ? LogLevel::Debug : ParseLogLevel(config_.log_level));
  shift_->Reconfigure(config_.amplitude_px, EffectiveTiming(), NowMs());
  hysteresis_.SetDelays(config_.pause_enter_delay_ms, config_.resume_delay_ms);
  if (previous.hotkey_toggle != config_.hotkey_toggle) RegisterToggleHotkey();
  if (previous.hotkey_emergency_exit != config_.hotkey_emergency_exit) StartEmergencyGuard();
  SyncAutostart();
  if (previous.monitor != config_.monitor || previous.allow_tearing != config_.allow_tearing) {
    // Serve un overlay nuovo: altro monitor o altra swap chain. Lo stato "ruotato"
    // riguardava il monitor precedente: RefreshMonitor lo rivaluta.
    display_rotated_ = false;
    monitor_valid_ = false;
    StopOverlay();
  }
  Tick();
}

void App::SaveConfig() {
  // Un file che non si legge e' probabilmente in modifica: non lo si sovrascrive.
  if (!config_file_.OnDiskIsValid()) {
    LogWarn("Configurazione non salvata: il file su disco contiene errori");
    ShowNotice(L"Configurazione non salvata",
               L"Il file contiene errori: la modifica vale fino alla chiusura. Correggi il file per salvarla.");
    return;
  }
  if (!config_file_.Save(config_)) ShowNotice(L"Configurazione", L"Impossibile salvare il file.");
}

void App::RegisterToggleHotkey() {
  if (toggle_hotkey_registered_) UnregisterHotKey(main_, kToggleHotkeyId);
  toggle_hotkey_registered_ = false;
  const auto key = ParseHotkey(config_.hotkey_toggle);
  if (!key) return;
  toggle_hotkey_registered_ = RegisterHotKey(main_, kToggleHotkeyId, key->modifiers | MOD_NOREPEAT, key->vk) != FALSE;
  if (!toggle_hotkey_registered_) {
    LogWarn("Hotkey {} non registrata: gia' in uso", config_.hotkey_toggle);
    ShowNotice(L"Hotkey non disponibile", L"La combinazione " + Widen(config_.hotkey_toggle) +
                                                     L" è già usata da un'altra app.");
  }
}

void App::StartEmergencyGuard() {
  const auto key = ParseHotkey(config_.hotkey_emergency_exit);
  if (!key) return;
  if (!guard_.Start(*key)) {
    ShowNotice(L"Hotkey di emergenza non disponibile",
                      L"La combinazione " + Widen(config_.hotkey_emergency_exit) + L" è già in uso.");
  }
  guard_.BeatMain();
  if (overlay_shown_) guard_.SetOverlayActive(true);
}

// ---------------------------------------------------------------- pausa e overlay

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
  params.generation = ++render_generation_;
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
  // Il tempo dello shift si ferma finche' l'overlay non riparte.
  shift_->Pause(NowMs());
  guard_.SetOverlayActive(false);
  overlay_shown_ = false;
  // Prima il thread (rilascia device, duplicazione, swap chain), poi la finestra.
  render_.Stop();
  // Di nuovo dopo il join: il thread potrebbe averlo riarmato mentre usciva.
  guard_.SetOverlayActive(false);
  overlay_.Destroy();
  overlay_running_ = false;
  LogInfo("Overlay fermato");
}

void App::OnRenderEvent(RenderEvent event) {
  switch (event) {
    case RenderEvent::FirstFrameShown:
      LogInfo("Overlay a schermo");
      overlay_shown_ = true;
      guard_.SetOverlayActive(true);
      break;
    case RenderEvent::CaptureLost:
      overlay_shown_ = false;
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

// ---------------------------------------------------------------- tray

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
  const auto monitors = EnumerateMonitors();
  const bool auto_monitor = EqualsIgnoreCase(config_.monitor, "auto");

  HMENU monitor_menu = CreatePopupMenu();
  AppendMenuW(monitor_menu, MF_STRING | (auto_monitor ? MF_CHECKED : MF_UNCHECKED), kCmdMonitorAuto,
              L"Automatico (LG TV, altrimenti principale)");
  AppendMenuW(monitor_menu, MF_SEPARATOR, 0, nullptr);
  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& m = monitors[i];
    const bool in_use = !auto_monitor && monitor_ && monitor_->device_name == m.device_name;
    const std::wstring label = m.friendly_name + L" (" + std::to_wstring(m.rect.right - m.rect.left) + L"×" +
                               std::to_wstring(m.rect.bottom - m.rect.top) + L")";
    AppendMenuW(monitor_menu, MF_STRING | (in_use ? MF_CHECKED : MF_UNCHECKED), kCmdMonitorFirst + static_cast<UINT>(i),
                label.c_str());
  }

  HMENU menu = CreatePopupMenu();
  // Riga di sola informazione: lo stato reale, con il motivo della pausa automatica.
  const std::wstring state = last_paused_ ? L"Stato: in pausa (" + Widen(PauseReasonText(last_reason_)) + L")"
                                          : std::wstring(L"Stato: attivo");
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, state.c_str());
  // La pausa manuale e' una voce a parte: cliccare durante una pausa automatica non
  // lascia una pausa manuale attiva dopo la fine del gioco.
  AppendMenuW(menu, MF_STRING | (manual_pause_ ? MF_CHECKED : MF_UNCHECKED), kCmdToggle,
              (L"Pausa manuale\t" + Widen(config_.hotkey_toggle)).c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(monitor_menu), L"Monitor");
  AppendMenuW(menu, MF_STRING, kCmdOpenConfig, L"Apri configurazione");
  AppendMenuW(menu, MF_STRING, kCmdOpenLogs, L"Apri cartella log");
  AppendMenuW(menu, MF_STRING | (config_.autostart ? MF_CHECKED : MF_UNCHECKED), kCmdAutostart, L"Avvio automatico");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdExit, L"Esci");

  POINT pt;
  GetCursorPos(&pt);
  // Necessario perche' il menu si chiuda cliccando altrove.
  SetForegroundWindow(main_);
  const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, main_, nullptr);
  PostMessageW(main_, WM_NULL, 0, 0);
  DestroyMenu(menu);  // distrugge anche il sottomenu

  if (cmd == kCmdToggle) SetManualPause(!manual_pause_);
  else if (cmd == kCmdOpenConfig) OpenConfig();
  else if (cmd == kCmdOpenLogs) OpenLogFolder();
  else if (cmd == kCmdAutostart) ToggleAutostart();
  else if (cmd == kCmdMonitorAuto) SelectMonitorSetting("auto");
  else if (cmd >= kCmdMonitorFirst && cmd < kCmdMonitorFirst + monitors.size()) {
    // Nome amichevole se unico, altrimenti device name (monitor identici).
    std::vector<MonitorDesc> descs;
    for (const auto& m : monitors) descs.push_back({Narrow(m.device_name), Narrow(m.friendly_name), m.primary});
    SelectMonitorSetting(MonitorSettingFor(descs, cmd - kCmdMonitorFirst));
  }
  else if (cmd == kCmdExit) Quit();
}

void App::SelectMonitorSetting(const std::string& setting) {
  if (config_.monitor == setting) return;
  Config next = config_;
  next.monitor = setting;
  LogInfo("Monitor scelto dalla tray: {}", setting);
  ApplyConfig(next);
  SaveConfig();
}

void App::ToggleAutostart() {
  Config next = config_;
  next.autostart = !config_.autostart;
  ApplyConfig(next);
  SaveConfig();
}

void App::OpenConfig() {
  const std::wstring path = config_file_.Path().wstring();
  RunShellAction([path] {
    // Editor associato ai .json; se non c'e', Blocco note.
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
      const std::wstring quoted = L"\"" + path + L"\"";
      ShellExecuteW(nullptr, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
    }
  });
}

void App::OpenLogFolder() {
  const std::wstring dir = LogDir().wstring();
  RunShellAction([dir] { ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); });
}

void App::Quit() {
  StopOverlay();
  if (toggle_hotkey_registered_) UnregisterHotKey(main_, kToggleHotkeyId);
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
      if (wp == kTimerConfigReload) {
        KillTimer(main_, kTimerConfigReload);
        ReloadConfig();
      }
      return 0;
    case WM_HOTKEY:
      if (wp == kToggleHotkeyId) SetManualPause(!manual_pause_);
      return 0;
    case WM_APP_TRAY:
      OnTrayEvent(LOWORD(lp));
      return 0;
    case WM_APP_RENDER_EVENT:
      // Eventi di un thread di render gia' fermato: arrivano dopo StopOverlay e
      // riarmerebbero il watchdog senza nessun thread a battere.
      if (!overlay_running_ || static_cast<unsigned>(lp) != render_generation_) {
        LogDebug("Evento di render {} ignorato (generazione {} invece di {})", wp, lp, render_generation_);
        return 0;
      }
      OnRenderEvent(static_cast<RenderEvent>(wp));
      return 0;
    case WM_APP_CONFIG_CHANGED:
      // Gli editor scrivono piu' volte: si ricarica dopo un attimo di quiete.
      SetTimer(main_, kTimerConfigReload, kConfigQuietMs, nullptr);
      return 0;
    case WM_APP_SECOND_INSTANCE:
      LogInfo("Avvio di una seconda istanza ignorato");
      tray_.ShowBalloon(L"OLED Guard", L"OLED Guard è già in esecuzione: lo trovi nell'area di notifica.");
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
