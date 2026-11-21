// Configurazione utente: default, validazione e (de)serializzazione JSON.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pause_rules.h"
#include "shift_scheduler.h"

namespace oled {

struct Config {
  std::string monitor = "auto";
  int amplitude_px = 2;
  double dwell_min_s = 20;
  double dwell_max_s = 60;
  double transition_min_s = 2;
  double transition_max_s = 5;
  bool pause_on_notification_state = true;
  bool pause_on_fullscreen_window = true;
  int pause_enter_delay_ms = 300;
  int resume_delay_ms = 2500;
  std::vector<std::string> denylist;
  std::vector<std::string> allowlist;
  std::string hotkey_toggle = "Ctrl+Alt+Shift+P";
  std::string hotkey_emergency_exit = "Ctrl+Alt+Shift+F12";
  std::string hotkey_display_off = "Ctrl+Alt+Shift+O";
  bool allow_tearing = false;
  bool autostart = false;
  std::string log_level = "info";

  bool operator==(const Config&) const = default;
};

struct ConfigLoadResult {
  Config config;
  std::vector<std::string> warnings;  // italiano
  bool parse_failed = false;          // il chiamante tiene la config precedente
};

ConfigLoadResult ConfigFromJson(std::string_view text);
std::string ConfigToJson(const Config& config);

PauseRulesConfig ToPauseRules(const Config& config);
ShiftTiming ToShiftTiming(const Config& config);

}  // namespace oled
