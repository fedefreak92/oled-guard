// Protocollo JSON fra l'app e la pagina dell'interfaccia (WebView2).
// App -> pagina: messaggi con campo "type". Pagina -> app: comandi con campo "cmd",
// validati qui prima di arrivare al codice Win32.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app_history.h"
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
};

struct WindowsItem {
  std::string id;
  std::string label;
  std::string current;
  std::string recommended;
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
// Avviso breve mostrato dalla pagina (testo italiano, UTF-8).
std::string NoticeMessage(std::string_view text);

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
