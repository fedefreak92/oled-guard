#include "app.h"

#include <dwmapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <ctime>
#include <functional>
#include <random>
#include <thread>

#include "ascii.h"
#include "history_file.h"
#include "autostart.h"
#include "clock.h"
#include "diagnostics.h"
#include "logger.h"
#include "monitor_select.h"
#include "paths.h"
#include "text.h"
#include "window_zones.h"
#include "ui_protocol.h"

namespace oled {
namespace {

constexpr wchar_t kMainClass[] = L"OledGuardMain";
constexpr UINT_PTR kTimerBeat = 1;
constexpr UINT_PTR kTimerPause = 2;
constexpr UINT_PTR kTimerConfigReload = 3;
constexpr UINT_PTR kTimerStatsSave = 4;
constexpr UINT_PTR kTimerTestShift = 5;
constexpr UINT_PTR kTimerDisplayOff = 6;
constexpr UINT kDisplayOffDelayMs = 700;
constexpr UINT kStatsSaveMs = 60000;
constexpr UINT kTestShiftMs = 30000;
constexpr UINT kPausePollMs = 250;
constexpr UINT kConfigQuietMs = 300;
constexpr double kCaptureRetryMs = 30000;
constexpr int kToggleHotkeyId = 1;

constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_CONFIG_CHANGED = WM_APP + 3;
constexpr UINT WM_APP_SECOND_INSTANCE = WM_APP + 4;
constexpr UINT WM_APP_UI_CLOSE = WM_APP + 5;
constexpr UINT WM_APP_WINDOWS_DONE = WM_APP + 6;
constexpr UINT WM_APP_BROWSED = WM_APP + 7;  // lParam: std::string* (nome .exe, vuoto se annullato)
constexpr int kDisplayOffHotkeyId = 2;

constexpr UINT kCmdExit = 100;
constexpr UINT kCmdToggle = 101;
constexpr UINT kCmdOpenUi = 105;
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
  // WebView2 richiede un thread STA con coda dei messaggi: il thread UI.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) args_.emplace_back(argv[i]);
  LocalFree(argv);
  debug_ = HasArg(L"--debug");
  fast_shift_ = HasArg(L"--fast-shift");
  dim_diagnostics_ = HasArg(L"--dim-diagnostics");

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
  {
    StatsData stats = LoadStats(StatsPath());
    wear_ = std::move(stats.wear);
    app_history_ = std::move(stats.apps);
    ui_rect_ = stats.ui_rect;
  }
  history_ = LoadHistory(AppDataDir() / L"history.json");
  {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    history_.CountSession(local.wYear * 10000 + local.wMonth * 100 + local.wDay);
  }

  std::random_device seed_source;
  const uint64_t seed = (static_cast<uint64_t>(seed_source()) << 32) ^ seed_source();
  shift_ = std::make_unique<SharedShift>(seed, config_.amplitude_px, EffectiveTiming(), NowMs());
  hysteresis_.SetDelays(config_.pause_enter_delay_ms, config_.resume_delay_ms);
  app_start_ms_ = NowMs();
  PushShadeInputs();

  StartEmergencyGuard();

  if (!CreateMainWindow()) {
    LogError("Impossibile creare la finestra principale");
    return 1;
  }
  SetTimer(main_, kTimerBeat, 1000, nullptr);
  SetTimer(main_, kTimerPause, kPausePollMs, nullptr);
  SetTimer(main_, kTimerStatsSave, kStatsSaveMs, nullptr);
  guard_.BeatMain();
  WTSRegisterSessionNotification(main_, NOTIFY_FOR_THIS_SESSION);
  foreground_hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundHook, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");
  icon_ = CreateAppIcon(GetSystemMetrics(SM_CXSMICON));
  tray_.Create(main_, WM_APP_TRAY, icon_);
  tray_ready_ = true;
  ui_.SetFailedHandler([this](const std::string& reason) {
    ShowNotice(L"Interfaccia non disponibile", Widen(reason));
  });
  // Avvisi nati prima che la tray esistesse (config, hotkey di emergenza).
  for (const auto& [title, text] : pending_notices_) tray_.ShowBalloon(title, text);
  pending_notices_.clear();
  RegisterToggleHotkey();
  RegisterDisplayOffHotkey();
  // Con una config illeggibile i valori sono quelli predefiniti: non si tocca il
  // registro, altrimenti un errore di battitura toglierebbe l'avvio automatico.
  if (!initial_config_failed_) SyncAutostart();
  config_file_.StartWatching(main_, WM_APP_CONFIG_CHANGED);

  Tick();
  if (HasArg(L"--open-ui")) OpenUi();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  config_file_.StopWatching();
  if (windows_worker_.joinable()) windows_worker_.join();
  ui_.Close();
  StopOverlay();
  SaveStatsNow();
  if (foreground_hook_) UnhookWinEvent(foreground_hook_);
  tray_.Destroy();
  if (icon_) DestroyIcon(icon_);
  guard_.Stop();
  g_app = nullptr;
  if (SUCCEEDED(com)) CoUninitialize();
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
  if (result.parse_failed) Record("config", "activity.config_invalid");
  else if (!result.warnings.empty()) Record("config", "activity.config_warnings", {{"n", std::to_string(result.warnings.size())}});
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
  // --fast-shift e "Prova ora": soste di 3-5 s e transizioni di 1-2 s.
  const bool fast = fast_shift_ || testing_shift_until_ms_ > NowMs();
  return fast ? ShiftTiming{3000, 5000, 1000, 2000} : ToShiftTiming(config_);
}

void App::ApplyConfig(const Config& next) {
  const Config previous = config_;
  config_ = next;
  LogSetLevel(debug_ ? LogLevel::Debug : ParseLogLevel(config_.log_level));
  shift_->Reconfigure(config_.amplitude_px, EffectiveTiming(), NowMs());
  hysteresis_.SetDelays(config_.pause_enter_delay_ms, config_.resume_delay_ms);
  if (previous.hotkey_toggle != config_.hotkey_toggle) RegisterToggleHotkey();
  if (previous.hotkey_emergency_exit != config_.hotkey_emergency_exit) StartEmergencyGuard();
  if (previous.hotkey_display_off != config_.hotkey_display_off) RegisterDisplayOffHotkey();
  SyncAutostart();
  PushDimInputs();
  PushShadeInputs();
  if (previous.monitor != config_.monitor || previous.allow_tearing != config_.allow_tearing) {
    // Serve un overlay nuovo: altro monitor o altra swap chain. Lo stato "ruotato"
    // riguardava il monitor precedente: RefreshMonitor lo rivaluta.
    display_rotated_ = false;
    monitor_valid_ = false;
    StopOverlay();
  }
  Tick();
  if (ui_.IsReady()) ui_.Post(ConfigMessage(config_));
  if (previous.monitor != config_.monitor) {
    Record("monitor", "activity.monitor_protected", {{"name", monitor_ ? Narrow(monitor_->friendly_name) : config_.monitor}});
    PushMonitorsToUi();
  }
}

void App::Record(std::string_view kind, std::string_view key, I18nParams params) {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const int64_t t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  const ActivityEvent& e = activity_.Add(t_ms, kind, key, std::move(params));
  if (ui_.IsReady()) ui_.Post(ActivityAddMessage(e));
}

void App::BrowseExe() {
  if (browsing_) return;  // una finestra "Apri" alla volta
  browsing_ = true;
  const HWND target = main_;
  RunShellAction([target] {
    auto result = std::make_unique<std::string>();
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
      const COMDLG_FILTERSPEC filter[] = {{L"Programmi (*.exe)", L"*.exe"}};
      dialog->SetFileTypes(1, filter);
      dialog->SetTitle(L"Scegli l'app");
      Microsoft::WRL::ComPtr<IShellItem> item;
      PWSTR path = nullptr;
      if (SUCCEEDED(dialog->Show(nullptr)) && SUCCEEDED(dialog->GetResult(&item)) &&
          SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        *result = LowerUnicode(Narrow(std::filesystem::path(path).filename().wstring()));
        CoTaskMemFree(path);
      }
    }
    if (PostMessageW(target, WM_APP_BROWSED, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
  });
}

void App::PushMonitorsToUi() {
  if (!ui_.IsReady()) return;
  std::vector<MonitorItem> items;
  for (const MonitorDetails& d : ListMonitorDetails()) {
    MonitorItem m;
    m.device = Narrow(d.info.device_name);
    m.name = Narrow(d.info.friendly_name);
    m.width = d.info.rect.right - d.info.rect.left;
    m.height = d.info.rect.bottom - d.info.rect.top;
    m.refresh_hz = d.refresh_hz;
    m.hdr = d.info.hdr;
    m.primary = d.info.primary;
    m.is_protected = monitor_ && monitor_->device_name == d.info.device_name;
    m.connection = d.connection;
    LogDebug("Monitor per la pagina: {} {} {}x{} {} Hz {} {}", m.device, m.name, m.width, m.height, m.refresh_hz,
             m.hdr ? "HDR" : "SDR", m.connection.empty() ? "-" : m.connection);
    items.push_back(std::move(m));
  }
  ui_.Post(MonitorsMessage(items, config_.monitor));
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

void App::RegisterDisplayOffHotkey() {
  if (display_off_hotkey_registered_) UnregisterHotKey(main_, kDisplayOffHotkeyId);
  display_off_hotkey_registered_ = false;
  const auto key = ParseHotkey(config_.hotkey_display_off);
  if (!key) return;
  display_off_hotkey_registered_ =
      RegisterHotKey(main_, kDisplayOffHotkeyId, key->modifiers | MOD_NOREPEAT, key->vk) != FALSE;
  if (!display_off_hotkey_registered_) {
    LogWarn("Hotkey {} non registrata: gia' in uso", config_.hotkey_display_off);
    ShowNotice(L"Hotkey non disponibile",
               L"La combinazione " + Widen(config_.hotkey_display_off) + L" per spegnere lo schermo è già in uso.");
  }
}

std::filesystem::path App::StatsPath() { return AppDataDir() / L"stats.json"; }

void App::SaveStatsNow() {
  if (!SaveStats(StatsPath(), StatsData{wear_, app_history_, ui_rect_})) LogWarn("stats.json non salvato");
  SaveHistoryNow();
}

void App::SaveHistoryNow() {
  if (!SaveHistory(AppDataDir() / L"history.json", history_)) LogWarn("history.json non salvato");
}

namespace {

// Ora locale: secondi "locali" dall'epoca (l'ora locale trattata come UTC) e giorno AAAAMMGG.
void LocalNow(int64_t& local_s, int& day_key) {
  SYSTEMTIME local{};
  GetLocalTime(&local);
  FILETIME ft{};
  SystemTimeToFileTime(&local, &ft);
  const uint64_t ticks = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  local_s = static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
  day_key = local.wYear * 10000 + local.wMonth * 100 + local.wDay;
}

const char* RuleText(AppRule rule) {
  switch (rule) {
    case AppRule::AlwaysPause: return "always";
    case AppRule::NeverPause: return "never";
    default: return "auto";
  }
}

}  // namespace

void App::UpdateStatistics(double now) {
  const bool shown = overlay_running_ && overlay_shown_;
  const std::optional<FrameSummary> summary = shown ? signals_.Latest(now, 3000) : std::nullopt;
  SignalSample sample;
  if (summary) sample = SignalSample{true, summary->risk, summary->motion, summary->brightness};
  signal_ring_.Push(sample);

  SecondSample s;
  LocalNow(s.local_s, s.day_key);
  s.summary = summary ? &*summary : nullptr;
  s.overlay_shown = shown;
  if (last_paused_) {
    if (last_reason_ == PauseReason::Manual) s.content = ContentKind::ManualPause;
    else if (last_reason_ == PauseReason::NotificationState || last_reason_ == PauseReason::FullscreenWindow ||
             last_reason_ == PauseReason::Denylisted) {
      s.content = ContentKind::Games;
    }
  } else if (away_.Away()) {
    s.content = ContentKind::Away;
  } else if (shown) {
    s.content = summary && summary->motion > 0.15 ? ContentKind::Video : ContentKind::StillWork;
  }
  const bool app_counts = shown || (last_paused_ && CausedByApp(last_reason_));
  if (config_.app_stats_enabled && app_counts && !last_foreground_.empty()) {
    s.app = last_foreground_;
    s.app_paused = last_paused_;
  }
  if (monitor_) s.monitor = Narrow(monitor_->friendly_name);
  history_.AddSecond(s);

  AlertParams alert;
  alert.enabled = config_.alert_enabled;
  alert.threshold = AlertThreshold(config_.alert_threshold);
  alert.after_min = config_.alert_after_min;
  const std::optional<double> unprotected = summary ? std::optional<double>(summary->risk_unprotected) : std::nullopt;
  if (alert_.Update(now, unprotected, alert)) {
    const int minutes = static_cast<int>(alert_.AboveForMs(now) / 60000.0 + 0.5);
    const int pct = static_cast<int>(summary->risk_unprotected * 100 + 0.5);
    const std::string text = "Da " + std::to_string(minutes) + " minuti il " + std::to_string(pct) +
                             "% dello schermo è fermo, luminoso e non protetto. Una pausa o \"Spegni schermo\" "
                             "aiutano il pannello.";
    LogInfo("Avviso: rischio non protetto {}% da {} min", pct, minutes);
    tray_.ShowBalloon(L"OLED Guard", Widen(text));
    Record("alert", "activity.alert", {{"min", std::to_string(minutes)}});
  }

  if (ui_.IsReady()) {
    ui_.Post(SignalsMessage(sample));
    if (now - last_history_push_ms_ >= 60000) PushHistoryToUi();
  }
}

void App::PushHistoryToUi() {
  if (!ui_.IsReady()) return;
  last_history_push_ms_ = NowMs();
  int64_t local_s = 0;
  int day = 0;
  LocalNow(local_s, day);
  HistoryPayload p;
  p.timeline = history_.Timeline(local_s, kTimelineBuckets);
  p.timeline_end_local_s = local_s;
  p.risk_peak_24h = history_.RiskPeak(local_s, 288);
  p.wear_monitor = monitor_ ? Narrow(monitor_->friendly_name) : "";
  p.wear = history_.Wear(p.wear_monitor);
  p.top_apps = history_.TopApps(day, 8);
  for (const AppView& a : p.top_apps) p.top_app_rules.push_back(RuleText(RuleFor(config_, a.exe)));
  p.content = history_.ContentShares(day);
  p.sessions_total = history_.SessionsTotal();
  p.sessions_week = history_.SessionsThisWeek(day);
  const auto [streak, best] = history_.Streak(day);
  p.streak = streak;
  p.streak_best = best;
  p.avoided_today = history_.WearAvoidedToday(day);
  p.avoided_total = history_.WearAvoidedTotal();
  p.protected_week_s = history_.ProtectedThisWeekS(day);
  ui_.Post(HistoryMessage(p));
}

void App::RunWindowsTask(std::function<void()> task) {
  // Tutto cio' che tocca le impostazioni di Windows (anche le letture) gira su questo
  // thread, una cosa alla volta: le notifiche di Windows possono richiedere secondi e
  // il thread UI non deve mai aspettarle (il watchdog lo considererebbe bloccato).
  if (windows_busy_.exchange(true)) {
    if (task) {
      ui_.Post(NoticeMessage("notice.windows_busy"));
    } else {
      windows_refresh_pending_ = true;  // si rilegge appena il lavoro in corso finisce
    }
    return;
  }
  if (windows_worker_.joinable()) windows_worker_.join();
  windows_worker_ = std::thread([this, task = std::move(task)] {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (task) task();
    const auto items = windows_.ReadAll();
    for (const auto& it : items) {
      LogDebug("Windows per OLED: {} = {} (consigliato: {})", it.id, it.current_key, it.ok ? "si'" : "no");
    }
    {
      std::lock_guard lock(windows_msg_mutex_);
      windows_msg_ = WindowsMessage(items);
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    windows_busy_ = false;
    PostMessageW(main_, WM_APP_WINDOWS_DONE, 0, 0);
  });
}

void App::PushWindowsToUi() { RunWindowsTask(nullptr); }

void App::ScheduleDisplayOff() {
  // Rilasciare i tasti della hotkey (o il clic) conta come input e riaccenderebbe
  // subito lo schermo: si spegne dopo un attimo.
  SetTimer(main_, kTimerDisplayOff, kDisplayOffDelayMs, nullptr);
}

void App::OnWindowsDone() {
  std::string message;
  {
    std::lock_guard lock(windows_msg_mutex_);
    message = windows_msg_;
  }
  if (ui_.IsReady() && !message.empty()) ui_.Post(message);
  if (windows_refresh_pending_) {
    windows_refresh_pending_ = false;
    PushWindowsToUi();
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
  // Latenza aggiunta stimata: un periodo di refresh (frame catturato e ripresentato).
  DWM_TIMING_INFO timing{};
  timing.cbSize = sizeof(timing);
  if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) && timing.rateRefresh.uiNumerator > 0) {
    latency_ms_ = 1000.0 * timing.rateRefresh.uiDenominator / timing.rateRefresh.uiNumerator;
  }
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

  last_foreground_ = in.foreground_process;
  if (monitor_ && now - sdr_white_checked_ms_ >= 5000) RefreshSdrWhite(now);
  PushDimInputs();
  if (overlay_running_) {
    RefreshDimZones(now);
    PushDimInputs();  // le finestre attive cambiano con il mouse e il primo piano
  }
  const bool entered_pause = !first_tick_ && paused && !last_paused_;
  if (first_tick_ || paused != last_paused_ || (paused && reason != last_reason_)) {
    if (paused) {
      LogInfo("In pausa: {} (processo in primo piano: {})", PauseReasonText(reason),
              in.foreground_process.empty() ? "-" : in.foreground_process);
      const std::string_view kind = reason == PauseReason::Manual ? "manual" : "pause";
      const std::string reason_key(PauseReasonKey(reason));
      if (CausedByApp(reason) && !in.foreground_process.empty()) {
        Record(kind, "activity.paused_app", {{"reason_key", reason_key}, {"app", in.foreground_process}});
      } else {
        Record(kind, "activity.paused", {{"reason_key", reason_key}});
      }
    } else {
      LogInfo("Attivo");
      Record("resume", "activity.resumed");
    }
    first_tick_ = false;
    last_paused_ = paused;
    last_reason_ = reason;
    UpdateTrayTooltip();
  }

  // Statistiche: tempo con l'overlay a schermo sulla posizione corrente.
  SYSTEMTIME local{};
  GetLocalTime(&local);
  wear_.RollDay(local.wYear * 10000 + local.wMonth * 100 + local.wDay);
  // Solo le pause dovute all'app in primo piano: un blocco schermo o un errore di
  // cattura non sono "giochi" e spingerebbero fuori dallo storico quelli veri.
  if (entered_pause && CausedByApp(reason)) {
    wear_.CountPause(reason);
    // Storico: quale app era in primo piano quando la protezione si e' fermata.
    if (!in.foreground_process.empty()) {
      app_history_.Record(in.foreground_process, reason, static_cast<int64_t>(std::time(nullptr)));
      if (ui_.IsReady()) ui_.Post(AppsMessage(app_history_, config_));
    }
  }
  if (overlay_running_ && overlay_shown_) {
    current_offset_ = shift_->OffsetAt(now);
    if (trail_.empty() || !(trail_.back() == current_offset_)) {
      trail_.push_back(current_offset_);
      if (trail_.size() > 4) trail_.erase(trail_.begin());
    }
    // Buchi lunghi (sospensione, blocchi) non contano come tempo protetto.
    if (last_wear_ms_ > 0) wear_.AddProtectedTime(current_offset_, std::min(now - last_wear_ms_, 1000.0));
    last_wear_ms_ = now;
  } else {
    last_wear_ms_ = 0;
  }

  if (!paused && !overlay_running_) {
    if (!StartOverlay()) capture_failed_until_ms_ = now + kCaptureRetryMs;
  } else if (paused && overlay_running_) {
    StopOverlay();
  }
  PushUiUpdates(now);
}

UiState App::BuildUiState(double now) {
  UiState s;
  s.paused = last_paused_;
  s.manual_pause = manual_pause_;
  s.testing_shift = testing_shift_until_ms_ > now;
  s.reason = last_reason_;
  s.foreground_process = last_foreground_;
  s.offset = current_offset_;
  s.trail = trail_;
  // Lo scheduler si interroga solo con l'overlay attivo: in pausa il suo tempo e' fermo.
  if (overlay_running_) s.next_move_ms = std::max(0.0, shift_->NextChangeAt(now) - now);
  s.amplitude = config_.amplitude_px;
  if (monitor_) {
    s.monitor_name = Narrow(monitor_->friendly_name);
    s.width = monitor_->rect.right - monitor_->rect.left;
    s.height = monitor_->rect.bottom - monitor_->rect.top;
    s.hdr = monitor_->hdr;
  }
  s.latency_ms = latency_ms_;
  s.away = away_.Away();
  s.session_ms = now - app_start_ms_;
  return s;
}

void App::PushUiUpdates(double now) {
  if (!ui_.IsReady()) return;
  // Lo stato al massimo 4 volte al secondo (Tick arriva anche dagli eventi di sistema).
  if (now - last_state_push_ms_ >= 200) {
    ui_.Post(StateMessage(BuildUiState(now)));
    last_state_push_ms_ = now;
    ++ui_updates_sent_;
  }
  if (now - last_stats_push_ms_ >= 5000) {
    ui_.Post(StatsMessage(wear_, config_.amplitude_px));
    last_stats_push_ms_ = now;
  }
  if (dim_watch_ && now - last_dim_push_ms_ >= 1000) PushDimToUi(now);
}

void App::PushAllToUi() {
  if (!ui_.IsReady()) return;
  const double now = NowMs();
  ui_.Post(ConfigMessage(config_));
  ui_.Post(StatsMessage(wear_, config_.amplitude_px));
  ui_.Post(AppsMessage(app_history_, config_));
  PushWindowsToUi();
  PushMonitorsToUi();
  ui_.Post(ActivityMessage(activity_));
  ui_.Post(SignalsHistoryMessage(signal_ring_));
  PushHistoryToUi();
  ui_.Post(AboutMessage(OLED_GUARD_VERSION, Narrow(config_file_.Path().wstring()), Narrow(LogDir().wstring())));
  ui_.Post(StateMessage(BuildUiState(now)));
  last_state_push_ms_ = now;
  last_stats_push_ms_ = now;
}

void App::RefreshSdrWhite(double now) {
  sdr_white_checked_ms_ = now;
  if (!monitor_) return;
  const auto white = QuerySdrWhiteLevel(*monitor_);
  if (white) {
    if (*white != sdr_white_) LogDebug("Bianco SDR: {:.0f} nit", *white * 80.0f);
    sdr_white_ = *white;
    sdr_white_warned_ = false;
  } else if (!sdr_white_warned_) {
    LogWarn("Bianco SDR non leggibile, uso 200 nit");
    sdr_white_ = 2.5f;
    sdr_white_warned_ = true;
  }
}

bool App::DimSuspended() const {
  return !last_foreground_.empty() &&
         std::find(config_.dim_exclude.begin(), config_.dim_exclude.end(), last_foreground_) !=
             config_.dim_exclude.end();
}

void App::RefreshDimZones(double now) {
  if (!monitor_) return;
  const int w = monitor_->rect.right - monitor_->rect.left;
  const int h = monitor_->rect.bottom - monitor_->rect.top;
  const int cols = (w + kDimCellPx - 1) / kDimCellPx;
  const int rows = (h + kDimCellPx - 1) / kDimCellPx;
  std::vector<std::string> described;
  std::vector<HWND> windows;
  const bool log_windows = dim_diagnostics_ && now - dim_windows_logged_ms_ >= 60000;
  auto zones = CollectDimZones(*monitor_, cols, rows, &windows, log_windows ? &described : nullptr);

  // Finestre "attive" che tornano normali: sotto il mouse (da almeno 0,3 s) e/o in primo piano.
  POINT cursor{};
  const HWND under = GetCursorPos(&cursor) ? GetAncestor(WindowFromPoint(cursor), GA_ROOT) : nullptr;
  if (under != hover_window_) {
    hover_window_ = under;
    hover_since_ms_ = now;
  }
  const std::string& mode = config_.dim_activate;
  std::vector<uint8_t> active;
  const auto add = [&](uint8_t zone) {
    if (zone != kDimZoneShell && std::find(active.begin(), active.end(), zone) == active.end()) active.push_back(zone);
  };
  if ((mode == "hover" || mode == "both") && now - hover_since_ms_ >= 300) add(ZoneOfWindow(hover_window_, windows));
  if (mode == "click" || mode == "both") add(ZoneOfWindow(GetForegroundWindow(), windows));
  if (active != dim_active_zones_) {
    wchar_t cls[128] = {};
    if (hover_window_) GetClassNameW(hover_window_, cls, ARRAYSIZE(cls));
    std::string ids;
    for (uint8_t z : active) ids += (ids.empty() ? "" : ",") + std::to_string(z);
    LogDebug("Oscuramento: finestre attive [{}] (sotto il mouse: {})", ids, Narrow(cls));
  }
  dim_active_zones_ = std::move(active);
  if (log_windows) {
    dim_windows_logged_ms_ = now;
    const auto app_cells = std::count(zones.begin(), zones.end(), kDimZoneApp);
    LogInfo("Oscuramento diag: zone app {} celle su {}; finestre: {}", app_cells, zones.size(), described.size());
    for (const std::string& d : described) LogInfo("  {}", d);
  }
  dim_.SetZones(std::move(zones), cols, rows);
}

void App::SetDimWatch(bool on) {
  if (dim_watch_ == on) return;
  dim_watch_ = on;
  LogDebug("Mappa dell'oscuramento per la scheda: {}", on ? "attiva" : "spenta");
  PushDimInputs();
  if (on) PushDimToUi(NowMs());
}

void App::PushDimToUi(double now) {
  if (!ui_.IsReady()) return;
  last_dim_push_ms_ = now;
  DimUiState s;
  const bool suspended = DimSuspended();
  if (last_paused_ || !overlay_shown_) s.status = "paused";
  else if (!config_.dim_enabled) s.status = "off";
  else if (suspended) s.status = "suspended";
  else if (now - overlay_shown_since_ms_ < config_.dim_delay_s * 1000.0) s.status = "waiting";
  else s.status = "active";
  if (suspended) s.suspended_by = last_foreground_;
  if (auto map = dim_.TakeMap()) last_dim_map_ = std::move(*map);
  // Mappa vecchia (overlay ricreato o fermo): meglio nessuna che una sbagliata.
  if (overlay_shown_ && last_dim_map_.time_ms >= overlay_shown_since_ms_) {
    s.map = SummarizeDimLevels(last_dim_map_.levels, last_dim_map_.cols, last_dim_map_.rows, 120);
  }
  s.sdr_white_nits = sdr_white_ * 80.0;
  s.hdr = monitor_ && monitor_->hdr;
  s.highlight_left_ms = std::max(0.0, dim_highlight_until_ms_ - now);
  ui_.Post(DimMessage(s));
}

void App::PushShadeInputs() {
  ShadeInputs in;
  in.base = ToShadeParams(config_, 1.0);
  in.away_since_ms = away_.Away() ? away_.SinceMs() : -1;
  in.away_target = config_.away_brightness_pct / 100.0;
  shade_.Set(in);
}

void App::OnSecond() {
  const double now = NowMs();
  UpdateStatistics(now);
  // Il movimento si misura solo con l'overlay a schermo (i frame li conta il render).
  const int frames = shade_.TakeNewFrames();
  if (overlay_running_ && overlay_shown_) motion_.AddSecond(frames);
  else motion_.Reset();

  LASTINPUTINFO input{sizeof(input)};
  const double idle_ms = GetLastInputInfo(&input) ? static_cast<double>(GetTickCount() - input.dwTime) : 0.0;
  AwayInputs in;
  in.now_ms = now;
  in.idle_ms = idle_ms;
  in.continuous_motion = motion_.Continuous();
  in.overlay_shown = overlay_running_ && overlay_shown_;
  in.enabled = config_.away_enabled;
  in.after_min = config_.away_after_min;
  if (away_.Update(in)) {
    if (away_.Away()) {
      LogInfo("Modalita' assente: schermo scurito");
      Record("away", "activity.away");
    } else {
      LogInfo("Modalita' assente: fine");
      Record("back", "activity.back");
    }
    PushShadeInputs();
    UpdateTrayTooltip();
  }
}

void App::PushDimInputs() {
  DimInputs in;
  in.params = ToDimParams(config_);
  in.suspended = DimSuspended();
  in.sdr_white_scrgb = sdr_white_;
  in.highlight_until_ms = dim_highlight_until_ms_;
  in.watch = dim_watch_;
  in.active_zones = dim_active_zones_;
  dim_.Set(in);
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
  params.dim = &dim_;
  params.shade = &shade_;
  params.signals = &signals_;
  params.frame_period_ms = latency_ms_ > 0 ? latency_ms_ : 7.0;
  params.dim_diagnostics = dim_diagnostics_;
  params.diagnostics_dir = LogDir();
  RefreshSdrWhite(NowMs());
  PushDimInputs();
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
      overlay_shown_since_ms_ = NowMs();
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
  // Clic sinistro (o Invio da tastiera): apre l'interfaccia. Clic destro: menu.
  if (event == NIN_SELECT || event == NIN_KEYSELECT) OpenUi();
  if (event == WM_CONTEXTMENU) ShowTrayMenu();
}

void App::OpenUi() {
  if (ui_.IsOpen()) {
    ui_.Open(instance_, nullptr, nullptr, false, std::nullopt);  // porta in primo piano
    return;
  }
  const bool ok = ui_.Open(
      instance_, [this](const std::string& json) { OnUiMessage(json); },
      [this](const RECT& last) {
        ui_rect_ = last;
        LogInfo("Interfaccia chiusa ({} aggiornamenti di stato inviati)", ui_updates_sent_);
        ui_updates_sent_ = 0;
        SetDimWatch(false);
        // Finestra chiusa (o pagina caduta) a meta' registrazione: le hotkey tornano attive.
        if (hotkey_capture_active_) {
          hotkey_capture_active_ = false;
          RegisterToggleHotkey();
          RegisterDisplayOffHotkey();
        }
        SaveStatsNow();
      },
      debug_, ui_rect_);
  if (ok) {
    SendMessageW(ui_.Hwnd(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_));
    SendMessageW(ui_.Hwnd(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_));
    LogInfo("Interfaccia aperta");
  }
}

void App::OnUiMessage(const std::string& json) {
  const auto command = ParseUiCommand(json);
  if (!command) {
    LogWarn("Interfaccia: comando non valido ignorato");
    return;
  }
  switch (command->type) {
    case UiCommandType::Ready:
      ui_.MarkReady();
      LogInfo("Interfaccia pronta");
      PushAllToUi();
      break;
    case UiCommandType::SetConfig: {
      const ConfigLoadResult result = ApplyConfigPatch(config_, command->patch);
      if (result.parse_failed || !result.warnings.empty()) {
        // Un valore non valido dalla pagina non sostituisce quello attuale con un default.
        for (const auto& w : result.warnings) LogWarn("Interfaccia: {}", w);
        ui_.Post(ConfigMessage(config_));
        if (!result.warnings.empty()) ui_.Post(NoticeMessage("notice.setting_rejected"));
        break;
      }
      ApplyConfig(NormalizedConfig(result.config));
      SaveConfig();
      break;
    }
    case UiCommandType::SetManualPause:
      SetManualPause(command->on);
      break;
    case UiCommandType::WindowsApply: {
      const std::string id = command->id;
      RunWindowsTask([this, id] { windows_.Apply(id); });
      Record("windows", "activity.windows_applied");
      break;
    }
    case UiCommandType::WindowsApplyAll:
      RunWindowsTask([this] { windows_.ApplyAll(); });
      Record("windows", "activity.windows_applied_all");
      break;
    case UiCommandType::WindowsRestoreAll:
      RunWindowsTask([this] { windows_.RestoreAll(); });
      Record("windows", "activity.windows_restored");
      break;
    case UiCommandType::DisplayOff:
      LogInfo("Schermo spento dall'interfaccia");
      ScheduleDisplayOff();
      break;
    case UiCommandType::SetAppRule:
    case UiCommandType::AddApp:
    case UiCommandType::RemoveApp: {
      const std::string exe = LowerUnicode(command->exe);
      Config next = config_;
      if (command->type == UiCommandType::SetAppRule) {
        LogInfo("Regola per {}: {}", exe, command->rule == AppRule::AlwaysPause ? "sempre in pausa"
                                         : command->rule == AppRule::NeverPause ? "mai in pausa"
                                                                                : "automatica");
        next = WithAppRule(next, exe, command->rule);
      } else if (command->type == UiCommandType::AddApp) {
        LogInfo("App aggiunta all'elenco: {}", exe);
        next = WithListedApp(next, exe);
      } else {
        LogInfo("App tolta dall'elenco: {}", exe);
        app_history_.Remove(exe);
        next = WithoutApp(next, exe);
      }
      ApplyConfig(NormalizedConfig(next));
      SaveConfig();
      ui_.Post(AppsMessage(app_history_, config_));
      break;
    }
    case UiCommandType::ListProcesses:
      ui_.Post(ProcessesMessage(ListWindowedProcesses()));
      break;
    case UiCommandType::TestShift: {
      if (!overlay_running_) {
        ui_.Post(NoticeMessage("notice.test_needs_protection"));
        break;
      }
      const double now = NowMs();
      testing_shift_until_ms_ = now + kTestShiftMs;
      shift_->Reconfigure(config_.amplitude_px, EffectiveTiming(), now);
      shift_->MoveSoon(now);
      SetTimer(main_, kTimerTestShift, kTestShiftMs, nullptr);
      LogInfo("Prova dello spostamento per {} s", kTestShiftMs / 1000);
      Record("test", "activity.test_shift", {{"s", std::to_string(kTestShiftMs / 1000)}});
      PushUiUpdates(now);
      break;
    }
    case UiCommandType::HotkeyCapture:
      // Mentre l'utente registra una combinazione, le hotkey globali non devono scattare.
      // L'uscita di emergenza resta sempre attiva.
      if (command->on) {
        if (toggle_hotkey_registered_) UnregisterHotKey(main_, kToggleHotkeyId);
        if (display_off_hotkey_registered_) UnregisterHotKey(main_, kDisplayOffHotkeyId);
        toggle_hotkey_registered_ = false;
        display_off_hotkey_registered_ = false;
        hotkey_capture_active_ = true;
      } else {
        hotkey_capture_active_ = false;
        RegisterToggleHotkey();
        RegisterDisplayOffHotkey();
      }
      break;
    case UiCommandType::DimWatch:
      SetDimWatch(command->on);
      break;
    case UiCommandType::DimHighlight: {
      const bool suspended = DimSuspended();
      if (!config_.dim_enabled || suspended || !overlay_shown_) {
        ui_.Post(NoticeMessage("notice.nothing_to_highlight"));
        break;
      }
      dim_highlight_until_ms_ = NowMs() + 15000;
      LogInfo("Evidenziazione delle zone oscurate per 15 s");
      PushDimInputs();
      PushDimToUi(NowMs());
      break;
    }
    case UiCommandType::SetDimExclude: {
      const std::string exe = LowerUnicode(command->exe);
      LogInfo("Oscuramento {} con {}", command->on ? "sospeso" : "attivo", exe);
      ApplyConfig(NormalizedConfig(WithAppDimExclude(config_, exe, command->on)));
      SaveConfig();
      ui_.Post(AppsMessage(app_history_, config_));
      break;
    }
    case UiCommandType::OpenLogFolder:
      OpenLogFolder();
      break;
    case UiCommandType::ClearHistory:
      history_.Clear();
      SaveHistoryNow();
      LogInfo("Storico delle statistiche cancellato");
      Record("config", "activity.history_cleared");
      PushHistoryToUi();
      break;
    case UiCommandType::ClearActivity:
      activity_.Clear();
      ui_.Post(ActivityMessage(activity_));
      break;
    case UiCommandType::BrowseExe:
      BrowseExe();
      break;
    case UiCommandType::SetMonitor: {
      // Si salva il nome amichevole: il numero DISPLAYn puo' cambiare dopo un riavvio della TV.
      std::vector<MonitorDesc> descs;
      for (const auto& m : EnumerateMonitors()) descs.push_back({Narrow(m.device_name), Narrow(m.friendly_name), m.primary});
      SelectMonitorSetting(MonitorSettingForDevice(descs, command->id));
      break;
    }
    case UiCommandType::Window:
      // Mai chiudere WebView2 dentro un suo evento: si rimanda al ciclo dei messaggi.
      if (command->action == "close") PostMessageW(main_, WM_APP_UI_CLOSE, 0, 0);
      else if (command->action == "minimize") ui_.Minimize();
      else if (command->action == "maximize") ui_.ToggleMaximize();
      break;
    default:
      LogDebug("Interfaccia: comando {} non ancora gestito", static_cast<int>(command->type));
      break;
  }
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
  AppendMenuW(menu, MF_STRING, kCmdOpenUi, L"Apri OLED Guard");
  SetMenuDefaultItem(menu, kCmdOpenUi, FALSE);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
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

  if (cmd == kCmdOpenUi) OpenUi();
  else if (cmd == kCmdToggle) SetManualPause(!manual_pause_);
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
  ui_.Close();
  StopOverlay();
  if (toggle_hotkey_registered_) UnregisterHotKey(main_, kToggleHotkeyId);
  if (display_off_hotkey_registered_) UnregisterHotKey(main_, kDisplayOffHotkeyId);
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
      if (wp == kTimerBeat) {
        guard_.BeatMain();
        OnSecond();
      }
      if (wp == kTimerPause) Tick();
      if (wp == kTimerConfigReload) {
        KillTimer(main_, kTimerConfigReload);
        ReloadConfig();
      }
      if (wp == kTimerStatsSave) SaveStatsNow();
      if (wp == kTimerDisplayOff) {
        KillTimer(main_, kTimerDisplayOff);
        TurnDisplayOff(main_);
      }
      if (wp == kTimerTestShift) {
        KillTimer(main_, kTimerTestShift);
        testing_shift_until_ms_ = 0;
        shift_->Reconfigure(config_.amplitude_px, EffectiveTiming(), NowMs());
        LogInfo("Fine della prova dello spostamento");
      }
      return 0;
    case WM_HOTKEY:
      if (wp == kToggleHotkeyId) SetManualPause(!manual_pause_);
      if (wp == kDisplayOffHotkeyId) {
        LogInfo("Schermo spento con la hotkey");
        ScheduleDisplayOff();
      }
      return 0;
    case WM_APP_WINDOWS_DONE:
      OnWindowsDone();
      return 0;
    case WM_APP_BROWSED: {
      const std::unique_ptr<std::string> exe(reinterpret_cast<std::string*>(lp));
      browsing_ = false;
      if (exe && !exe->empty() && ui_.IsReady()) ui_.Post(BrowsedMessage(*exe));
      return 0;
    }
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
    case WM_APP_UI_CLOSE:
      ui_.Close();
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
      PushMonitorsToUi();
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
