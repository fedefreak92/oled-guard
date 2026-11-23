// Protocollo JSON fra l'app e la pagina dell'interfaccia (WebView2).
// App -> pagina: messaggi con campo "type". Pagina -> app: comandi con campo "cmd",
// validati qui prima di arrivare al codice Win32.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "activity_log.h"
#include "app_history.h"
#include "history.h"
#include "i18n.h"
#include "signals.h"
#include "config.h"
#include "json.h"
#include "offset.h"
#include "pause_rules.h"
#include "wear_stats.h"

namespace oled {

struct UiState {
  bool paused = false;
  bool manual_pause = false;
  bool testing_shift = false;
  PauseReason reason = PauseReason::None;
  std::string foreground_process;
  Offset offset;
  std::vector<Offset> trail;  // ultime posizioni, la piu' vecchia per prima
  double next_move_ms = 0;
  int amplitude = 2;
  std::string monitor_name;
  int width = 0;
  int height = 0;
  bool hdr = false;
  double latency_ms = 0;
  bool away = false;
  double session_ms = 0;  // da quando l'app e' partita
  bool setup_pending = false;  // procedura di primo avvio in corso
};

struct MonitorItem {
  std::string device;  // \\.\DISPLAY1
  std::string name;
  int width = 0;
  int height = 0;
  int refresh_hz = 0;
  bool hdr = false;
  bool primary = false;
  bool is_protected = false;
  std::string connection;  // "HDMI" | "DP" | "DVI" | "Interno" | ""
};

// Etichetta nella pagina: chiave "windows.<id>.label".
struct WindowsItem {
  std::string id;
  std::string current_key;
  I18nParams current_params;
  std::string recommended_key;
  I18nParams recommended_params;
  bool ok = false;          // gia' come consigliato
  bool restorable = false;  // valore originale salvato
  bool managed = false;     // bloccato dal sistema
};

std::string StateMessage(const UiState& state);
std::string StatsMessage(const WearStats& stats, int amplitude);
std::string ConfigMessage(const Config& config);
std::string AppsMessage(const AppHistory& history, const Config& config);
std::string WindowsMessage(const std::vector<WindowsItem>& items);
std::string ProcessesMessage(const std::vector<std::string>& processes);
// setting: valore di config.monitor ("auto", nome o device).
std::string MonitorsMessage(const std::vector<MonitorItem>& items, const std::string& setting);
std::string ActivityMessage(const ActivityLog& log);
std::string ActivityAddMessage(const ActivityEvent& e);
std::string AboutMessage(std::string_view version, std::string_view config_path, std::string_view log_dir);
std::string BrowsedMessage(std::string_view exe);
// Segnali dal vivo: il campione dell'ultimo secondo, oppure gli ultimi 60 (null = non misurato).
std::string SignalsMessage(const SignalSample& s);
std::string SignalsHistoryMessage(const SignalRing& r);

struct HistoryPayload {
  std::vector<std::optional<double>> timeline;  // kTimelineBuckets secchi, dal piu' vecchio
  int64_t timeline_end_local_s = 0;
  double risk_peak_24h = 0;
  WearView wear;
  std::string wear_monitor;
  std::vector<AppView> top_apps;
  std::vector<std::string> top_app_rules;  // "auto" | "always" | "never", stesso ordine
  std::array<double, kContentKinds> content{};
  int sessions_total = 0;
  int sessions_week = 0;
  int streak = 0;
  int streak_best = 0;
  std::optional<double> avoided_today;
  std::optional<double> avoided_total;
  double protected_week_s = 0;
};
std::string HistoryMessage(const HistoryPayload& p);

// Avviso breve mostrato dalla pagina: chiave di traduzione e segnaposto.
std::string NoticeMessage(std::string_view key, const I18nParams& params = {});

// Chiede alla pagina di mostrare una pagina (es. "monitor" dal clic su una notifica).
std::string NavigateMessage(std::string_view page);

// Dizionario della lingua attiva per la pagina.
std::string StringsMessage(const Translations& t);

// Mappa dei livelli di oscuramento ridotta per la miniatura della scheda.
struct DimMapSummary {
  int cols = 0;
  int rows = 0;
  std::string cells;  // cols*rows caratteri 0-9a-f (livello / kDimMaxLevel in 16 passi)
  double dimmed_pct = 0;  // % di celle con livello > 0,01 sulla griglia piena
  double max_level = 0;
};
// Riduce con il massimo per blocchi (fattore intero uguale sui due assi) finche'
// cols <= max_cols.
DimMapSummary SummarizeDimLevels(const std::vector<float>& levels, int cols, int rows, int max_cols);

struct DimUiState {
  std::string status;        // "active" | "waiting" | "suspended" | "off" | "paused"
  std::string suspended_by;  // .exe se sospeso da un'app
  DimMapSummary map;         // vuota se non disponibile
  double sdr_white_nits = 0;
  bool hdr = false;
  double highlight_left_ms = 0;
};
std::string DimMessage(const DimUiState& s);

enum class UiCommandType {
  Ready,
  SetConfig,
  SetManualPause,
  TestShift,
  SetAppRule,
  AddApp,
  RemoveApp,
  ListProcesses,
  WindowsApply,
  WindowsApplyAll,
  WindowsRestoreAll,
  DisplayOff,
  HotkeyCapture,
  Window,
  DimWatch,
  DimHighlight,
  OpenLogFolder,
  BrowseExe,
  SetMonitor,     // id = device o "auto"
  ClearActivity,
  SetDimExclude,  // exe, on
  ClearHistory,
  SetLanguage,    // id = codice di lingua o "auto"
  IdentifyMonitors,
  FinishSetup,    // id = device o "auto", on = avvio automatico
  ExportDiagnostics,
};

struct UiCommand {
  UiCommandType type = UiCommandType::Ready;
  JsonValue patch;
  bool on = false;
  std::string exe;
  AppRule rule = AppRule::Auto;
  std::string id;
  std::string action;
};

// nullopt se il comando e' sconosciuto o i campi mancano / hanno il tipo sbagliato.
std::optional<UiCommand> ParseUiCommand(std::string_view json);

// Fonde la patch sulla config corrente e valida tutto con ConfigFromJson.
ConfigLoadResult ApplyConfigPatch(const Config& current, const JsonValue& patch);

}  // namespace oled
