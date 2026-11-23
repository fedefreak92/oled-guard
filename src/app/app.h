// Applicazione: thread UI, finestra nascosta per i messaggi, ciclo di vita.
#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app_history.h"
#include "i18n.h"
#include "startup.h"
#include "config.h"
#include "config_file.h"
#include "ui_protocol.h"
#include "wear_stats.h"
#include "emergency.h"
#include "monitors.h"
#include "overlay_window.h"
#include "paths.h"
#include "pause_monitor.h"
#include "pause_rules.h"
#include "render_thread.h"
#include "away_model.h"
#include "shared_dim.h"
#include "alert_model.h"
#include "history.h"
#include "shared_shade.h"
#include "shared_signals.h"
#include "signals.h"
#include "single_instance.h"
#include "stats_file.h"
#include "tray.h"
#include "ui_window.h"
#include "windows_settings.h"

namespace oled {

class App {
 public:
  int Run(HINSTANCE instance);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static void CALLBACK ForegroundHook(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
  bool CreateMainWindow();
  bool HasArg(std::wstring_view arg) const;

  // Configurazione.
  void LoadInitialConfig();
  void ReloadConfig();
  void ApplyConfig(const Config& next);
  void SaveConfig();
  void ReportConfigWarnings(const ConfigLoadResult& result, bool at_startup);
  static Config NormalizedConfig(Config config);
  void SyncAutostart();
  // Notifica nella tray; se la tray non esiste ancora la mostra appena creata.
  // Notifica nella tray: titolo e testo sono chiavi di traduzione.
  void ShowNotice(std::string_view title_key, std::string_view body_key, const I18nParams& params = {});
  // Testo tradotto nella lingua attiva.
  std::wstring T(std::string_view key, const I18nParams& params = {}) const;
  // Sceglie la lingua (config + Windows), ricarica i testi e li manda alla pagina e alla tray.
  void ApplyLanguage();
  ShiftTiming EffectiveTiming() const;
  void RegisterToggleHotkey();
  void RegisterDisplayOffHotkey();
  // Esegue una modifica alle impostazioni di Windows su un thread di lavoro.
  void RunWindowsTask(std::function<void()> task);
  void PushWindowsToUi();
  void PushMonitorsToUi();
  // Statistiche: un secondo di segnali e storico, avviso; storico verso la pagina.
  void UpdateStatistics(double now);
  void PushHistoryToUi();
  void SaveHistoryNow();
  // Attivita' recente: aggiunge l'evento e lo invia alla pagina se aperta.
  // Evento dell'attivita' recente: chiave di traduzione e segnaposto. I segnaposto che
  // finiscono in "_key" sono a loro volta chiavi (la pagina le traduce).
  void Record(std::string_view kind, std::string_view key, I18nParams params = {});
  // Finestra "Apri" per scegliere un .exe, su un thread proprio.
  void BrowseExe();
  void OnWindowsDone();
  void ScheduleDisplayOff();
  static std::filesystem::path StatsPath();
  void SaveStatsNow();
  void StartEmergencyGuard();

  // Valuta i segnali di pausa e crea/distrugge l'overlay di conseguenza.
  void Tick();
  bool StartOverlay();
  void StopOverlay();
  void RefreshMonitor();
  // Oscuramento: config, app in primo piano, bianco SDR -> thread di render.
  void PushDimInputs();
  // Luminosita', bordi e modalita' assente -> thread di render.
  void PushShadeInputs();
  // Ogni secondo: movimento sul desktop, input, modalita' assente.
  void OnSecond();
  void RefreshSdrWhite(double now);
  // Zone dell'oscuramento (desktop/barra o app) dalle finestre visibili.
  void RefreshDimZones(double now);
  void PushDimToUi(double now);
  void SetDimWatch(bool on);
  bool DimSuspended() const;  // app in primo piano fra le esclusioni
  void OnRenderEvent(RenderEvent event);

  // Interfaccia.
  void OpenUi();
  void OnUiMessage(const std::string& json);
  UiState BuildUiState(double now);
  void PushUiUpdates(double now);
  void PushAllToUi();

  // Tray.
  void OnTrayEvent(UINT event);
  void ShowTrayMenu();
  void UpdateTrayTooltip();
  void SetManualPause(bool paused);
  void SelectMonitorSetting(const std::string& setting);
  void OpenConfig();
  void OpenLogFolder();
  void ToggleAutostart();
  void Quit();

  std::vector<std::wstring> args_;
  HINSTANCE instance_ = nullptr;
  HWND main_ = nullptr;
  UINT taskbar_created_msg_ = 0;
  HICON icon_ = nullptr;
  HWINEVENTHOOK foreground_hook_ = nullptr;
  SingleInstance single_instance_;
  ConfigFile config_file_{ConfigPath()};
  Config config_;
  Translations tr_;
  bool initial_config_failed_ = false;
  // Primo avvio: procedura nell'interfaccia, protezione ferma e config non salvata.
  bool setup_pending_ = false;
  bool notify_auto_choice_ = false;      // schermo scelto in automatico senza procedura
  MissingMonitorNotifier missing_notifier_;
  std::string balloon_page_;             // pagina da aprire al clic sull'ultima notifica
  std::string pending_page_;             // pagina da mostrare appena l'interfaccia e' pronta
  bool tray_ready_ = false;
  std::vector<std::pair<std::wstring, std::wstring>> pending_notices_;
  unsigned render_generation_ = 0;
  bool fast_shift_ = false;
  bool debug_ = false;
  bool toggle_hotkey_registered_ = false;
  bool display_off_hotkey_registered_ = false;
  WindowsSettings windows_{AppDataDir() / L"windows-backup.json"};
  std::thread windows_worker_;
  std::atomic<bool> windows_busy_{false};
  bool windows_refresh_pending_ = false;
  std::mutex windows_msg_mutex_;
  std::string windows_msg_;
  bool hotkey_capture_active_ = false;
  EmergencyGuard guard_;
  Tray tray_;
  OverlayWindow overlay_;
  RenderThread render_;
  UiWindow ui_;
  std::optional<RECT> ui_rect_;
  double last_state_push_ms_ = 0;
  double last_stats_push_ms_ = 0;
  unsigned ui_updates_sent_ = 0;

  // Statistiche e dati per l'interfaccia.
  WearStats wear_;
  AppHistory app_history_;
  Offset current_offset_{};
  std::vector<Offset> trail_;
  double last_wear_ms_ = 0;
  double latency_ms_ = 0;
  double testing_shift_until_ms_ = 0;
  std::string last_foreground_;
  // Stato dello shift: appartiene all'App e sopravvive alle pause dell'overlay.
  std::unique_ptr<SharedShift> shift_;
  // Luminosita', bordi scuriti e modalita' assente.
  SharedShade shade_;
  ActivityLog activity_;
  // Segnali dal vivo e statistiche (progetto 2).
  SharedSignals signals_;
  SignalRing signal_ring_;
  History history_;
  AlertModel alert_;
  double last_history_push_ms_ = 0;
  bool browsing_ = false;
  MotionWindow motion_;
  AwayModel away_;
  double app_start_ms_ = 0;
  // Oscuramento delle aree statiche.
  SharedDim dim_;
  float sdr_white_ = 2.5f;
  double sdr_white_checked_ms_ = -1e9;
  bool sdr_white_warned_ = false;
  bool dim_watch_ = false;
  double dim_highlight_until_ms_ = 0;
  double overlay_shown_since_ms_ = 0;
  double last_dim_push_ms_ = 0;
  DimMap last_dim_map_;
  bool dim_diagnostics_ = false;
  double dim_windows_logged_ms_ = -1e9;
  std::vector<uint8_t> dim_active_zones_;  // finestre attive (zone), dall'ultimo RefreshDimZones
  HWND hover_window_ = nullptr;             // finestra sotto il mouse e da quando
  double hover_since_ms_ = 0;

  PauseMonitor pause_monitor_;
  PauseHysteresis hysteresis_{300, 2500};
  bool manual_pause_ = false;
  double capture_failed_until_ms_ = 0;
  bool display_rotated_ = false;
  bool last_paused_ = false;
  PauseReason last_reason_ = PauseReason::None;
  PauseReason last_raw_ = PauseReason::None;
  NotificationState last_notification_ = NotificationState::AcceptsNotifications;
  bool first_tick_ = true;

  std::optional<MonitorInfo> monitor_;
  bool monitor_valid_ = false;
  bool overlay_running_ = false;
  bool overlay_shown_ = false;
};

}  // namespace oled
