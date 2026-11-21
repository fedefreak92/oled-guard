#include "ui_protocol.h"

#include <algorithm>
#include <cmath>

#include "dim_params.h"

namespace oled {
namespace {

constexpr size_t kMaxExeLength = 260;

JsonValue Point(Offset o) {
  JsonValue p = JsonValue::MakeArray();
  p.Push(JsonValue(o.x));
  p.Push(JsonValue(o.y));
  return p;
}

std::string Compact(const JsonValue& v) { return SerializeJson(v, 0); }

const char* RuleName(AppRule rule) {
  switch (rule) {
    case AppRule::AlwaysPause: return "always";
    case AppRule::NeverPause: return "never";
    default: return "auto";
  }
}

std::optional<AppRule> ParseRule(const std::string& s) {
  if (s == "auto") return AppRule::Auto;
  if (s == "always") return AppRule::AlwaysPause;
  if (s == "never") return AppRule::NeverPause;
  return std::nullopt;
}

const JsonValue* Field(const JsonValue& o, std::string_view key, JsonValue::Type type) {
  const JsonValue* v = o.Find(key);
  return v && v->type() == type ? v : nullptr;
}

}  // namespace

std::string StateMessage(const UiState& s) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("state"));
  o.Set("paused", JsonValue(s.paused));
  o.Set("manual_pause", JsonValue(s.manual_pause));
  o.Set("testing_shift", JsonValue(s.testing_shift));
  o.Set("reason", JsonValue(std::string(PauseReasonText(s.reason))));
  o.Set("foreground", JsonValue(s.foreground_process));
  o.Set("offset", Point(s.offset));
  JsonValue trail = JsonValue::MakeArray();
  for (const Offset& p : s.trail) trail.Push(Point(p));
  o.Set("trail", trail);
  o.Set("next_move_ms", JsonValue(s.next_move_ms));
  o.Set("amplitude", JsonValue(s.amplitude));
  o.Set("monitor", JsonValue(s.monitor_name));
  o.Set("width", JsonValue(s.width));
  o.Set("height", JsonValue(s.height));
  o.Set("hdr", JsonValue(s.hdr));
  o.Set("latency_ms", JsonValue(s.latency_ms));
  o.Set("away", JsonValue(s.away));
  o.Set("session_ms", JsonValue(s.session_ms));
  return Compact(o);
}

std::string StatsMessage(const WearStats& stats, int amplitude) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("stats"));
  JsonValue cells = JsonValue::MakeArray();
  for (int y = -WearStats::kRadius; y <= WearStats::kRadius; ++y) {
    for (int x = -WearStats::kRadius; x <= WearStats::kRadius; ++x) cells.Push(JsonValue(stats.CellMs({x, y})));
  }
  o.Set("cells", cells);
  const auto u = stats.Uniformity(amplitude);
  o.Set("uniformity", u ? JsonValue(*u) : JsonValue(nullptr));
  o.Set("today_ms", JsonValue(stats.TodayProtectedMs()));
  o.Set("total_ms", JsonValue(stats.TotalProtectedMs()));
  o.Set("pauses_today", JsonValue(stats.TodayPauses()));
  JsonValue by_reason = JsonValue::MakeObject();
  for (int r = 0; r < WearStats::kReasons; ++r) {
    const auto reason = static_cast<PauseReason>(r);
    const int n = stats.TodayPauses(reason);
    if (n > 0) by_reason.Set(std::string(PauseReasonText(reason)), JsonValue(n));
  }
  o.Set("pauses_by_reason", by_reason);
  return Compact(o);
}

std::string ConfigMessage(const Config& config) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("config"));
  const auto parsed = ParseJson(ConfigToJson(config));
  o.Set("config", parsed.value ? *parsed.value : JsonValue::MakeObject());
  return Compact(o);
}

std::string AppsMessage(const AppHistory& history, const Config& config) {
  const auto excluded = [&](const std::string& exe) {
    return std::find(config.dim_exclude.begin(), config.dim_exclude.end(), exe) != config.dim_exclude.end();
  };
  JsonValue apps = JsonValue::MakeArray();
  std::vector<std::string> listed;
  for (const AppEntry& e : history.Entries()) {
    JsonValue a = JsonValue::MakeObject();
    a.Set("exe", JsonValue(e.exe));
    a.Set("reason", JsonValue(std::string(PauseReasonText(e.reason))));
    a.Set("last_seen_s", JsonValue(static_cast<double>(e.last_seen_s)));
    a.Set("count", JsonValue(e.count));
    a.Set("rule", JsonValue(RuleName(RuleFor(config, e.exe))));
    a.Set("dim_exclude", JsonValue(excluded(e.exe)));
    apps.Push(std::move(a));
    listed.push_back(e.exe);
  }
  // Le app con regole o aggiunte a mano ma mai viste compaiono comunque, in fondo.
  for (const auto* list : {&config.denylist, &config.allowlist, &config.app_list, &config.dim_exclude}) {
    for (const std::string& exe : *list) {
      if (std::find(listed.begin(), listed.end(), exe) != listed.end()) continue;
      JsonValue a = JsonValue::MakeObject();
      a.Set("exe", JsonValue(exe));
      a.Set("reason", JsonValue(""));
      a.Set("last_seen_s", JsonValue(0));
      a.Set("count", JsonValue(0));
      a.Set("rule", JsonValue(RuleName(RuleFor(config, exe))));
      a.Set("dim_exclude", JsonValue(excluded(exe)));
      apps.Push(std::move(a));
      listed.push_back(exe);
    }
  }
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("apps"));
  o.Set("apps", apps);
  return Compact(o);
}

std::string WindowsMessage(const std::vector<WindowsItem>& items) {
  JsonValue arr = JsonValue::MakeArray();
  int score = 0;
  for (const WindowsItem& it : items) {
    JsonValue i = JsonValue::MakeObject();
    i.Set("id", JsonValue(it.id));
    i.Set("label", JsonValue(it.label));
    i.Set("current", JsonValue(it.current));
    i.Set("recommended", JsonValue(it.recommended));
    i.Set("ok", JsonValue(it.ok));
    i.Set("restorable", JsonValue(it.restorable));
    i.Set("managed", JsonValue(it.managed));
    arr.Push(std::move(i));
    if (it.ok) ++score;
  }
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("windows"));
  o.Set("items", arr);
  o.Set("score", JsonValue(score));
  o.Set("total", JsonValue(static_cast<int>(items.size())));
  return Compact(o);
}

std::string ProcessesMessage(const std::vector<std::string>& processes) {
  JsonValue arr = JsonValue::MakeArray();
  for (const std::string& p : processes) arr.Push(JsonValue(p));
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("processes"));
  o.Set("processes", arr);
  return Compact(o);
}

std::string NoticeMessage(std::string_view text) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("notice"));
  o.Set("text", JsonValue(std::string(text)));
  return Compact(o);
}

DimMapSummary SummarizeDimLevels(const std::vector<float>& levels, int cols, int rows, int max_cols) {
  DimMapSummary s;
  if (cols <= 0 || rows <= 0 || levels.size() != static_cast<size_t>(cols) * rows) return s;
  int dimmed = 0;
  for (float v : levels) {
    if (v > 0.01f) ++dimmed;
    s.max_level = std::max(s.max_level, static_cast<double>(v));
  }
  s.dimmed_pct = 100.0 * dimmed / static_cast<double>(levels.size());
  const int factor = std::max(1, (cols + max_cols - 1) / std::max(1, max_cols));
  s.cols = (cols + factor - 1) / factor;
  s.rows = (rows + factor - 1) / factor;
  s.cells.assign(static_cast<size_t>(s.cols) * s.rows, '0');
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < cols; ++x) {
      const double v = std::clamp(static_cast<double>(levels[static_cast<size_t>(y) * cols + x]), 0.0, kDimMaxLevel);
      const int q = static_cast<int>(std::lround(v / kDimMaxLevel * 15));
      const char c = "0123456789abcdef"[q];
      char& out = s.cells[static_cast<size_t>(y / factor) * s.cols + x / factor];
      out = std::max(out, c);  // '0'-'9' < 'a'-'f' in ASCII
    }
  }
  return s;
}

std::string DimMessage(const DimUiState& s) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("dim"));
  o.Set("status", JsonValue(s.status));
  o.Set("suspended_by", JsonValue(s.suspended_by));
  o.Set("cols", JsonValue(s.map.cols));
  o.Set("rows", JsonValue(s.map.rows));
  o.Set("cells", JsonValue(s.map.cells));
  o.Set("dimmed_pct", JsonValue(s.map.dimmed_pct));
  o.Set("max_level", JsonValue(s.map.max_level));
  o.Set("sdr_white_nits", JsonValue(s.sdr_white_nits));
  o.Set("hdr", JsonValue(s.hdr));
  o.Set("highlight_left_ms", JsonValue(s.highlight_left_ms));
  return Compact(o);
}

std::string MonitorsMessage(const std::vector<MonitorItem>& items, const std::string& setting) {
  JsonValue list = JsonValue::MakeArray();
  for (const MonitorItem& m : items) {
    JsonValue i = JsonValue::MakeObject();
    i.Set("device", JsonValue(m.device));
    i.Set("name", JsonValue(m.name));
    i.Set("width", JsonValue(m.width));
    i.Set("height", JsonValue(m.height));
    i.Set("refresh_hz", JsonValue(m.refresh_hz));
    i.Set("hdr", JsonValue(m.hdr));
    i.Set("primary", JsonValue(m.primary));
    i.Set("protected", JsonValue(m.is_protected));
    i.Set("connection", JsonValue(m.connection));
    list.Push(std::move(i));
  }
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("monitors"));
  o.Set("setting", JsonValue(setting));
  o.Set("monitors", list);
  return Compact(o);
}

namespace {
JsonValue EventJson(const ActivityEvent& e) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("t", JsonValue(static_cast<double>(e.t_ms)));
  o.Set("kind", JsonValue(e.kind));
  o.Set("text", JsonValue(e.text));
  o.Set("count", JsonValue(e.count));
  return o;
}
}  // namespace

std::string ActivityMessage(const ActivityLog& log) {
  JsonValue events = JsonValue::MakeArray();
  for (const ActivityEvent& e : log.Events()) events.Push(EventJson(e));
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("activity"));
  o.Set("events", events);
  return Compact(o);
}

std::string ActivityAddMessage(const ActivityEvent& e) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("activity"));
  o.Set("add", EventJson(e));
  return Compact(o);
}

std::string AboutMessage(std::string_view version, std::string_view config_path, std::string_view log_dir) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("about"));
  o.Set("version", JsonValue(std::string(version)));
  o.Set("config_path", JsonValue(std::string(config_path)));
  o.Set("log_dir", JsonValue(std::string(log_dir)));
  return Compact(o);
}

std::string BrowsedMessage(std::string_view exe) {
  JsonValue o = JsonValue::MakeObject();
  o.Set("type", JsonValue("browsed"));
  o.Set("exe", JsonValue(std::string(exe)));
  return Compact(o);
}

std::optional<UiCommand> ParseUiCommand(std::string_view json) {
  const auto parsed = ParseJson(json);
  if (!parsed.value || parsed.value->type() != JsonValue::Type::Object) return std::nullopt;
  const JsonValue& o = *parsed.value;
  const JsonValue* cmd = Field(o, "cmd", JsonValue::Type::String);
  if (!cmd) return std::nullopt;
  const std::string& name = cmd->AsString();
  UiCommand c;

  const auto need_exe = [&]() -> bool {
    const JsonValue* exe = Field(o, "exe", JsonValue::Type::String);
    if (!exe || exe->AsString().empty() || exe->AsString().size() > kMaxExeLength) return false;
    c.exe = exe->AsString();
    return true;
  };
  const auto need_on = [&]() -> bool {
    const JsonValue* on = Field(o, "on", JsonValue::Type::Bool);
    if (!on) return false;
    c.on = on->AsBool();
    return true;
  };

  if (name == "ready") {
    c.type = UiCommandType::Ready;
  } else if (name == "setConfig") {
    const JsonValue* patch = Field(o, "patch", JsonValue::Type::Object);
    if (!patch) return std::nullopt;
    c.type = UiCommandType::SetConfig;
    c.patch = *patch;
  } else if (name == "setManualPause") {
    if (!need_on()) return std::nullopt;
    c.type = UiCommandType::SetManualPause;
  } else if (name == "hotkeyCapture") {
    if (!need_on()) return std::nullopt;
    c.type = UiCommandType::HotkeyCapture;
  } else if (name == "testShift") {
    c.type = UiCommandType::TestShift;
  } else if (name == "setAppRule") {
    const JsonValue* rule = Field(o, "rule", JsonValue::Type::String);
    const auto parsed_rule = rule ? ParseRule(rule->AsString()) : std::nullopt;
    if (!need_exe() || !parsed_rule) return std::nullopt;
    c.type = UiCommandType::SetAppRule;
    c.rule = *parsed_rule;
  } else if (name == "addApp") {
    if (!need_exe()) return std::nullopt;
    c.type = UiCommandType::AddApp;
  } else if (name == "removeApp") {
    if (!need_exe()) return std::nullopt;
    c.type = UiCommandType::RemoveApp;
  } else if (name == "listProcesses") {
    c.type = UiCommandType::ListProcesses;
  } else if (name == "windowsApply") {
    const JsonValue* id = Field(o, "id", JsonValue::Type::String);
    if (!id || id->AsString().empty()) return std::nullopt;
    c.type = UiCommandType::WindowsApply;
    c.id = id->AsString();
  } else if (name == "windowsApplyAll") {
    c.type = UiCommandType::WindowsApplyAll;
  } else if (name == "windowsRestoreAll") {
    c.type = UiCommandType::WindowsRestoreAll;
  } else if (name == "displayOff") {
    c.type = UiCommandType::DisplayOff;
  } else if (name == "dimWatch") {
    if (!need_on()) return std::nullopt;
    c.type = UiCommandType::DimWatch;
  } else if (name == "dimHighlight") {
    c.type = UiCommandType::DimHighlight;
  } else if (name == "openLogFolder") {
    c.type = UiCommandType::OpenLogFolder;
  } else if (name == "browseExe") {
    c.type = UiCommandType::BrowseExe;
  } else if (name == "clearActivity") {
    c.type = UiCommandType::ClearActivity;
  } else if (name == "setMonitor") {
    const JsonValue* device = Field(o, "device", JsonValue::Type::String);
    if (!device || device->AsString().empty() || device->AsString().size() > kMaxExeLength) return std::nullopt;
    c.type = UiCommandType::SetMonitor;
    c.id = device->AsString();
  } else if (name == "setDimExclude") {
    if (!need_exe() || !need_on()) return std::nullopt;
    c.type = UiCommandType::SetDimExclude;
  } else if (name == "window") {
    const JsonValue* action = Field(o, "action", JsonValue::Type::String);
    if (!action) return std::nullopt;
    const std::string& a = action->AsString();
    if (a != "minimize" && a != "maximize" && a != "close") return std::nullopt;
    c.type = UiCommandType::Window;
    c.action = a;
  } else {
    return std::nullopt;
  }
  return c;
}

ConfigLoadResult ApplyConfigPatch(const Config& current, const JsonValue& patch) {
  auto base = ParseJson(ConfigToJson(current));
  JsonValue merged = base.value ? *base.value : JsonValue::MakeObject();
  if (patch.type() == JsonValue::Type::Object) {
    for (const auto& [key, value] : patch.AsObject()) merged.Set(key, value);
  }
  return ConfigFromJson(SerializeJson(merged));
}

}  // namespace oled
