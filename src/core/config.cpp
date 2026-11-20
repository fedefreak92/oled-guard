#include "config.h"

#include <cmath>

#include "ascii.h"
#include "hotkey_parse.h"
#include "json.h"

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
  const Config defaults;
  const auto toggle = ParseHotkey(c.hotkey_toggle);
  const auto exit = ParseHotkey(c.hotkey_emergency_exit);
  if (!toggle) {
    reader.Warn("hotkey_toggle: combinazione \"" + c.hotkey_toggle + "\" non valida, uso " + defaults.hotkey_toggle);
    c.hotkey_toggle = defaults.hotkey_toggle;
  }
  if (!exit) {
    reader.Warn("hotkey_emergency_exit: combinazione \"" + c.hotkey_emergency_exit + "\" non valida, uso " +
                defaults.hotkey_emergency_exit);
    c.hotkey_emergency_exit = defaults.hotkey_emergency_exit;
  }
  if (toggle && exit && *toggle == *exit) {
    reader.Warn("hotkey_toggle e hotkey_emergency_exit sono uguali, uso i valori predefiniti");
    c.hotkey_toggle = defaults.hotkey_toggle;
    c.hotkey_emergency_exit = defaults.hotkey_emergency_exit;
  }
}

}  // namespace

ConfigLoadResult ConfigFromJson(std::string_view text) {
  ConfigLoadResult result;
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
    else if (key == "allow_tearing") r.Bool(key, v, c.allow_tearing);
    else if (key == "autostart") r.Bool(key, v, c.autostart);
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
  o.Set("allow_tearing", JsonValue(c.allow_tearing));
  o.Set("autostart", JsonValue(c.autostart));
  o.Set("log_level", JsonValue(c.log_level));
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
