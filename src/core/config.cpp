#include "config.h"

#include <algorithm>
#include <cmath>

#include "ascii.h"
#include "hotkey_parse.h"
#include "json.h"
#include "utf.h"

namespace oled {
namespace {

std::string Num(double v) { return SerializeJson(JsonValue(v)); }

class Reader {
 public:
  explicit Reader(std::vector<std::string>& warnings) : warnings_(warnings) {}

  void Warn(std::string text) { warnings_.push_back(std::move(text)); }

  void Int(const std::string& key, const JsonValue& v, int lo, int hi, int& field) {
    if (v.type() != JsonValue::Type::Number || std::floor(v.AsNumber()) != v.AsNumber()) {
      Warn(key + ": serve un numero intero, uso " + std::to_string(field));
      return;
    }
    if (v.AsNumber() < lo || v.AsNumber() > hi) {
      Warn(key + ": valore " + Num(v.AsNumber()) + " fuori intervallo " + std::to_string(lo) + "-" +
           std::to_string(hi) + ", uso " + std::to_string(field));
      return;
    }
    field = static_cast<int>(v.AsNumber());
  }

  void Double(const std::string& key, const JsonValue& v, double lo, double hi, double& field) {
    if (v.type() != JsonValue::Type::Number) {
      Warn(key + ": serve un numero, uso " + Num(field));
      return;
    }
    if (v.AsNumber() < lo || v.AsNumber() > hi) {
      Warn(key + ": valore " + Num(v.AsNumber()) + " fuori intervallo " + Num(lo) + "-" + Num(hi) + ", uso " +
           Num(field));
      return;
    }
    field = v.AsNumber();
  }

  void Bool(const std::string& key, const JsonValue& v, bool& field) {
    if (v.type() != JsonValue::Type::Bool) {
      Warn(key + ": serve true o false, uso " + (field ? "true" : "false"));
      return;
    }
    field = v.AsBool();
  }

  void String(const std::string& key, const JsonValue& v, std::string& field) {
    if (v.type() != JsonValue::Type::String || TrimAscii(v.AsString()).empty()) {
      Warn(key + ": serve un testo non vuoto, uso \"" + field + "\"");
      return;
    }
    field = std::string(TrimAscii(v.AsString()));
  }

  // Testo fra valori ammessi (maiuscole ignorate).
  void Choice(const std::string& key, const JsonValue& v, std::initializer_list<const char*> allowed,
              std::string& field) {
    std::string value = field;
    String(key, v, value);
    value = ToLowerAscii(value);
    for (const char* a : allowed) {
      if (value == a) {
        field = value;
        return;
      }
    }
    std::string list;
    for (const char* a : allowed) list += (list.empty() ? "" : ", ") + std::string(a);
    Warn(key + ": valore \"" + value + "\" non valido (" + list + "), uso " + field);
  }

  void List(const std::string& key, const JsonValue& v, std::vector<std::string>& field) {
    if (v.type() != JsonValue::Type::Array) {
      Warn(key + ": serve un elenco di nomi .exe, ignorato");
      return;
    }
    field.clear();
    for (const auto& item : v.AsArray()) {
      if (item.type() != JsonValue::Type::String || TrimAscii(item.AsString()).empty()) {
        Warn(key + ": elemento non valido ignorato (servono nomi .exe tra virgolette)");
        continue;
      }
      field.push_back(ToLowerAscii(TrimAscii(item.AsString())));
    }
  }

 private:
  std::vector<std::string>& warnings_;
};

void FixRange(Reader& reader, const char* name, double& lo, double& hi, double default_lo, double default_hi) {
  if (lo > hi) {
    reader.Warn(std::string(name) + ": minimo maggiore del massimo, uso " + Num(default_lo) + "-" + Num(default_hi));
    lo = default_lo;
    hi = default_hi;
  }
}

void FixHotkeys(Reader& reader, Config& c) {
  const Config d;
  const std::pair<const char*, std::pair<std::string*, const std::string*>> keys[] = {
      {"hotkey_toggle", {&c.hotkey_toggle, &d.hotkey_toggle}},
      {"hotkey_emergency_exit", {&c.hotkey_emergency_exit, &d.hotkey_emergency_exit}},
      {"hotkey_display_off", {&c.hotkey_display_off, &d.hotkey_display_off}},
  };
  for (const auto& [name, values] : keys) {
    if (!ParseHotkey(*values.first)) {
      reader.Warn(std::string(name) + ": combinazione \"" + *values.first + "\" non valida, uso " + *values.second);
      *values.first = *values.second;
    }
  }
  // Il confronto avviene dopo le sostituzioni: un default puo' coincidere con un'altra.
  const auto t = ParseHotkey(c.hotkey_toggle);
  const auto e = ParseHotkey(c.hotkey_emergency_exit);
  const auto o = ParseHotkey(c.hotkey_display_off);
  if (t == e || t == o || e == o) {
    reader.Warn("le hotkey devono essere tutte diverse fra loro, uso i valori predefiniti");
    c.hotkey_toggle = d.hotkey_toggle;
    c.hotkey_emergency_exit = d.hotkey_emergency_exit;
    c.hotkey_display_off = d.hotkey_display_off;
  }
}

}  // namespace

// Toglie duplicati e nomi troppo lunghi, limita a kMaxAppList voci.
void CleanAppList(Reader& reader, std::vector<std::string>& list) {
  constexpr size_t kMaxAppList = 200;
  constexpr size_t kMaxExe = 260;
  std::vector<std::string> clean;
  bool dropped = false;
  for (auto& exe : list) {
    if (exe.size() > kMaxExe || std::find(clean.begin(), clean.end(), exe) != clean.end()) {
      dropped |= exe.size() > kMaxExe;
      continue;
    }
    clean.push_back(std::move(exe));
  }
  if (dropped) reader.Warn("app_list: nomi piu' lunghi di 260 caratteri ignorati");
  if (clean.size() > kMaxAppList) {
    clean.resize(kMaxAppList);
    reader.Warn("app_list: al massimo 200 app, le altre sono ignorate");
  }
  list = std::move(clean);
}

ConfigLoadResult ConfigFromJson(std::string_view raw) {
  ConfigLoadResult result;
  // Blocco note puo' salvare in UTF-16 (LE o BE, con BOM): si converte in UTF-8.
  std::string converted;
  std::string_view text = raw;
  const bool utf16_le = raw.substr(0, 2) == "\xFF\xFE";
  const bool utf16_be = raw.substr(0, 2) == "\xFE\xFF";
  if (utf16_le || utf16_be) {
    const auto decoded = Utf16ToUtf8(raw.substr(2), utf16_le);
    if (!decoded) {
      result.parse_failed = true;
      result.warnings.push_back("file in UTF-16 non valido: salvalo in UTF-8 (Blocco note: Salva con nome > Codifica)");
      return result;
    }
    converted = *decoded;
    text = converted;
  }
  const auto parsed = ParseJson(text);
  if (!parsed.value) {
    result.parse_failed = true;
    result.warnings.push_back("JSON non valido: " + parsed.error);
    return result;
  }
  if (parsed.value->type() != JsonValue::Type::Object) {
    result.parse_failed = true;
    result.warnings.push_back("JSON non valido: la radice deve essere un oggetto { ... }");
    return result;
  }

  Config& c = result.config;
  const Config d;
  Reader r(result.warnings);
  for (const auto& [key, v] : parsed.value->AsObject()) {
    if (key == "monitor") r.String(key, v, c.monitor);
    else if (key == "amplitude_px") r.Int(key, v, 1, 3, c.amplitude_px);
    else if (key == "dwell_min_s") r.Double(key, v, 5, 600, c.dwell_min_s);
    else if (key == "dwell_max_s") r.Double(key, v, 5, 600, c.dwell_max_s);
    else if (key == "transition_min_s") r.Double(key, v, 0.5, 30, c.transition_min_s);
    else if (key == "transition_max_s") r.Double(key, v, 0.5, 30, c.transition_max_s);
    else if (key == "pause_on_notification_state") r.Bool(key, v, c.pause_on_notification_state);
    else if (key == "pause_on_fullscreen_window") r.Bool(key, v, c.pause_on_fullscreen_window);
    else if (key == "pause_enter_delay_ms") r.Int(key, v, 0, 10000, c.pause_enter_delay_ms);
    else if (key == "resume_delay_ms") r.Int(key, v, 0, 10000, c.resume_delay_ms);
    else if (key == "denylist") r.List(key, v, c.denylist);
    else if (key == "allowlist") r.List(key, v, c.allowlist);
    else if (key == "hotkey_toggle") r.String(key, v, c.hotkey_toggle);
    else if (key == "hotkey_emergency_exit") r.String(key, v, c.hotkey_emergency_exit);
    else if (key == "hotkey_display_off") r.String(key, v, c.hotkey_display_off);
    else if (key == "allow_tearing") r.Bool(key, v, c.allow_tearing);
    else if (key == "autostart") r.Bool(key, v, c.autostart);
    else if (key == "dim_enabled") r.Bool(key, v, c.dim_enabled);
    else if (key == "dim_strength_pct") r.Int(key, v, 10, 80, c.dim_strength_pct);
    else if (key == "dim_delay_s") r.Int(key, v, 30, 3600, c.dim_delay_s);
    else if (key == "dim_ramp_s") r.Int(key, v, 30, 3600, c.dim_ramp_s);
    else if (key == "dim_recover_ms") r.Int(key, v, 100, 5000, c.dim_recover_ms);
    else if (key == "dim_large_enabled") r.Bool(key, v, c.dim_large_enabled);
    else if (key == "dim_large_delay_s") r.Int(key, v, 300, 14400, c.dim_large_delay_s);
    else if (key == "dim_large_strength_pct") r.Int(key, v, 5, 80, c.dim_large_strength_pct);
    else if (key == "dim_bright_threshold_pct") r.Int(key, v, 5, 100, c.dim_bright_threshold_pct);
    else if (key == "dim_motion_fraction_pct") r.Int(key, v, 10, 90, c.dim_motion_fraction_pct);
    else if (key == "dim_exclude") r.List(key, v, c.dim_exclude);
    else if (key == "dim_window_mode") r.Choice(key, v, {"desktop", "video"}, c.dim_window_mode);
    else if (key == "dim_activate") r.Choice(key, v, {"hover", "click", "both", "never"}, c.dim_activate);
    else if (key == "brightness_pct") r.Int(key, v, 20, 100, c.brightness_pct);
    else if (key == "vignette_enabled") r.Bool(key, v, c.vignette_enabled);
    else if (key == "vignette_strength_pct") r.Int(key, v, 0, 80, c.vignette_strength_pct);
    else if (key == "vignette_left_pct") r.Int(key, v, 0, 40, c.vignette_left_pct);
    else if (key == "vignette_right_pct") r.Int(key, v, 0, 40, c.vignette_right_pct);
    else if (key == "vignette_top_pct") r.Int(key, v, 0, 40, c.vignette_top_pct);
    else if (key == "vignette_bottom_pct") r.Int(key, v, 0, 40, c.vignette_bottom_pct);
    else if (key == "away_enabled") r.Bool(key, v, c.away_enabled);
    else if (key == "away_after_min") r.Int(key, v, 1, 60, c.away_after_min);
    else if (key == "away_brightness_pct") r.Int(key, v, 10, 80, c.away_brightness_pct);
    else if (key == "ui_accent") r.Choice(key, v, {"teal", "indigo", "coral", "green", "amber", "violet"}, c.ui_accent);
    else if (key == "ui_text_size") r.Choice(key, v, {"small", "normal", "large", "xl"}, c.ui_text_size);
    else if (key == "app_list") {
      r.List(key, v, c.app_list);
      CleanAppList(r, c.app_list);
    }
    else if (key == "log_level") {
      std::string level = c.log_level;
      r.String(key, v, level);
      level = ToLowerAscii(level);
      if (level == "debug" || level == "info" || level == "warn" || level == "error") {
        c.log_level = level;
      } else if (level != c.log_level) {
        r.Warn("log_level: valore \"" + level + "\" non valido (debug, info, warn, error), uso " + d.log_level);
      }
    } else {
      r.Warn("chiave sconosciuta \"" + key + "\" ignorata");
    }
  }

  FixRange(r, "dwell_min_s/dwell_max_s", c.dwell_min_s, c.dwell_max_s, d.dwell_min_s, d.dwell_max_s);
  FixRange(r, "transition_min_s/transition_max_s", c.transition_min_s, c.transition_max_s, d.transition_min_s,
           d.transition_max_s);
  FixHotkeys(r, c);
  if (c.dim_large_delay_s < c.dim_delay_s) {
    r.Warn("dim_delay_s/dim_large_delay_s: le grandi aree devono iniziare dopo gli elementi piccoli, uso " +
           std::to_string(d.dim_delay_s) + "-" + std::to_string(d.dim_large_delay_s));
    c.dim_delay_s = d.dim_delay_s;
    c.dim_large_delay_s = d.dim_large_delay_s;
  }
  return result;
}

std::string ConfigToJson(const Config& c) {
  const auto list = [](const std::vector<std::string>& items) {
    JsonValue arr = JsonValue::MakeArray();
    for (const auto& item : items) arr.Push(JsonValue(item));
    return arr;
  };
  JsonValue o = JsonValue::MakeObject();
  o.Set("monitor", JsonValue(c.monitor));
  o.Set("amplitude_px", JsonValue(c.amplitude_px));
  o.Set("dwell_min_s", JsonValue(c.dwell_min_s));
  o.Set("dwell_max_s", JsonValue(c.dwell_max_s));
  o.Set("transition_min_s", JsonValue(c.transition_min_s));
  o.Set("transition_max_s", JsonValue(c.transition_max_s));
  o.Set("pause_on_notification_state", JsonValue(c.pause_on_notification_state));
  o.Set("pause_on_fullscreen_window", JsonValue(c.pause_on_fullscreen_window));
  o.Set("pause_enter_delay_ms", JsonValue(c.pause_enter_delay_ms));
  o.Set("resume_delay_ms", JsonValue(c.resume_delay_ms));
  o.Set("denylist", list(c.denylist));
  o.Set("allowlist", list(c.allowlist));
  o.Set("hotkey_toggle", JsonValue(c.hotkey_toggle));
  o.Set("hotkey_emergency_exit", JsonValue(c.hotkey_emergency_exit));
  o.Set("hotkey_display_off", JsonValue(c.hotkey_display_off));
  o.Set("allow_tearing", JsonValue(c.allow_tearing));
  o.Set("autostart", JsonValue(c.autostart));
  o.Set("log_level", JsonValue(c.log_level));
  o.Set("dim_enabled", JsonValue(c.dim_enabled));
  o.Set("dim_strength_pct", JsonValue(c.dim_strength_pct));
  o.Set("dim_delay_s", JsonValue(c.dim_delay_s));
  o.Set("dim_ramp_s", JsonValue(c.dim_ramp_s));
  o.Set("dim_recover_ms", JsonValue(c.dim_recover_ms));
  o.Set("dim_large_enabled", JsonValue(c.dim_large_enabled));
  o.Set("dim_large_delay_s", JsonValue(c.dim_large_delay_s));
  o.Set("dim_large_strength_pct", JsonValue(c.dim_large_strength_pct));
  o.Set("dim_bright_threshold_pct", JsonValue(c.dim_bright_threshold_pct));
  o.Set("dim_motion_fraction_pct", JsonValue(c.dim_motion_fraction_pct));
  o.Set("dim_exclude", list(c.dim_exclude));
  o.Set("dim_window_mode", JsonValue(c.dim_window_mode));
  o.Set("dim_activate", JsonValue(c.dim_activate));
  o.Set("brightness_pct", JsonValue(c.brightness_pct));
  o.Set("vignette_enabled", JsonValue(c.vignette_enabled));
  o.Set("vignette_strength_pct", JsonValue(c.vignette_strength_pct));
  o.Set("vignette_left_pct", JsonValue(c.vignette_left_pct));
  o.Set("vignette_right_pct", JsonValue(c.vignette_right_pct));
  o.Set("vignette_top_pct", JsonValue(c.vignette_top_pct));
  o.Set("vignette_bottom_pct", JsonValue(c.vignette_bottom_pct));
  o.Set("away_enabled", JsonValue(c.away_enabled));
  o.Set("away_after_min", JsonValue(c.away_after_min));
  o.Set("away_brightness_pct", JsonValue(c.away_brightness_pct));
  o.Set("ui_accent", JsonValue(c.ui_accent));
  o.Set("ui_text_size", JsonValue(c.ui_text_size));
  o.Set("app_list", list(c.app_list));
  return SerializeJson(o) + "\n";
}

PauseRulesConfig ToPauseRules(const Config& c) {
  PauseRulesConfig rules;
  rules.pause_on_notification_state = c.pause_on_notification_state;
  rules.pause_on_fullscreen_window = c.pause_on_fullscreen_window;
  rules.denylist = c.denylist;
  rules.allowlist = c.allowlist;
  return rules;
}

ShiftTiming ToShiftTiming(const Config& c) {
  return ShiftTiming{c.dwell_min_s * 1000.0, c.dwell_max_s * 1000.0, c.transition_min_s * 1000.0,
                     c.transition_max_s * 1000.0};
}

}  // namespace oled
