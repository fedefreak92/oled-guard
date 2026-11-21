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

  // Oscuramento delle aree statiche (elementi piccoli fermi e luminosi).
  bool dim_enabled = true;
  int dim_strength_pct = 40;          // 10-80
  int dim_delay_s = 60;               // 30-3600
  int dim_ramp_s = 120;               // 30-3600
  int dim_recover_ms = 500;           // 100-5000
  bool dim_large_enabled = false;
  int dim_large_delay_s = 1200;       // 300-14400
  int dim_large_strength_pct = 20;    // 5-80
  int dim_bright_threshold_pct = 25;  // 5-100, % del bianco SDR in luce lineare
  int dim_motion_fraction_pct = 25;   // 10-90, movimento intorno a un logo dentro un'app
  std::vector<std::string> dim_exclude;
  std::string dim_window_mode = "desktop";  // "desktop" (come il desktop) | "video" (solo loghi su video)
  std::string dim_activate = "both";        // finestra che torna normale: "hover" | "click" | "both" | "never"

  // Luminosita' via software e bordi scuriti (applicati dall'overlay).
  int brightness_pct = 100;         // 20-100
  bool vignette_enabled = false;
  int vignette_strength_pct = 50;   // 0-80
  int vignette_left_pct = 8;        // 0-40, % del lato dello schermo
  int vignette_right_pct = 8;       // 0-40
  int vignette_top_pct = 0;         // 0-40
  int vignette_bottom_pct = 12;     // 0-40

  // Modalita' assente: schermo scurito dopo un po' senza input e senza movimento.
  bool away_enabled = false;
  int away_after_min = 10;          // 1-60
  int away_brightness_pct = 30;     // 10-80

  // Aspetto dell'interfaccia.
  std::string ui_accent = "teal";      // teal | indigo | coral | green | amber | violet
  std::string ui_text_size = "normal"; // small | normal | large | xl

  // App aggiunte a mano nella pagina Profili app (restano in elenco anche con regola automatica).
  std::vector<std::string> app_list;   // .exe minuscoli, max 200

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
