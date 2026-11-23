#include <string>

#include <cmath>

#include "activity_log.h"
#include "history.h"
#include "signals.h"
#include "app_history.h"
#include "json.h"
#include "i18n.h"
#include "test_framework.h"
#include "ui_protocol.h"
#include "wear_stats.h"

using oled::JsonValue;
using oled::ParseJson;
using oled::UiCommandType;

namespace {
JsonValue Parse(const std::string& s) {
  auto r = ParseJson(s);
  return r.value ? *r.value : JsonValue();
}
}  // namespace

TEST(ui_state_message_fields) {
  oled::UiState s;
  s.paused = true;
  s.reason = oled::PauseReason::FullscreenWindow;
  s.offset = {1, -1};
  s.trail = {{0, 0}, {1, -1}};
  s.hdr = true;
  s.width = 3840;
  const JsonValue v = Parse(oled::StateMessage(s));
  CHECK(v.type() == JsonValue::Type::Object);
  if (v.type() != JsonValue::Type::Object) return;
  CHECK_EQ(v.Find("type")->AsString(), std::string("state"));
  CHECK(v.Find("paused")->AsBool());
  CHECK_EQ(v.Find("reason_key")->AsString(), std::string("pause.reason.fullscreen_window"));
  CHECK(v.Find("reason") == nullptr);
  CHECK_EQ(v.Find("trail")->AsArray().size(), size_t{2});
  CHECK(v.Find("hdr")->AsBool());
  CHECK_NEAR(v.Find("width")->AsNumber(), 3840, 1e-9);
  CHECK_NEAR(v.Find("offset")->AsArray()[1].AsNumber(), -1, 1e-9);
}

TEST(ui_stats_message_has_49_cells_and_null_uniformity) {
  oled::WearStats w;
  const JsonValue v = Parse(oled::StatsMessage(w, 2));
  CHECK_EQ(v.Find("type")->AsString(), std::string("stats"));
  CHECK_EQ(v.Find("cells")->AsArray().size(), size_t{49});
  CHECK(v.Find("uniformity")->type() == JsonValue::Type::Null);
  CHECK_NEAR(v.Find("total_ms")->AsNumber(), 0, 1e-9);
}

TEST(ui_apps_message_includes_rules) {
  oled::AppHistory h;
  h.Record("game.exe", oled::PauseReason::FullscreenWindow, 10);
  oled::Config c;
  c.denylist = {"game.exe"};
  c.allowlist = {"mpv.exe"};
  const JsonValue v = Parse(oled::AppsMessage(h, c));
  CHECK_EQ(v.Find("type")->AsString(), std::string("apps"));
  const auto& apps = v.Find("apps")->AsArray();
  CHECK_EQ(apps.size(), size_t{2});
  if (apps.size() != 2) return;
  CHECK_EQ(apps[0].Find("exe")->AsString(), std::string("game.exe"));
  CHECK_EQ(apps[0].Find("rule")->AsString(), std::string("always"));
  CHECK_EQ(apps[0].Find("reason_key")->AsString(), std::string("pause.reason.fullscreen_window"));
  CHECK(apps[0].Find("reason") == nullptr);
  CHECK_NEAR(apps[0].Find("count")->AsNumber(), 1, 1e-9);
  CHECK_EQ(apps[1].Find("exe")->AsString(), std::string("mpv.exe"));
  CHECK_EQ(apps[1].Find("rule")->AsString(), std::string("never"));
  CHECK_NEAR(apps[1].Find("count")->AsNumber(), 0, 1e-9);
}

TEST(ui_windows_message_score) {
  std::vector<oled::WindowsItem> items(3);
  items[0].id = "a";
  items[0].ok = true;
  items[1].id = "b";
  items[1].ok = true;
  items[2].id = "c";
  const JsonValue v = Parse(oled::WindowsMessage(items));
  CHECK_EQ(v.Find("type")->AsString(), std::string("windows"));
  CHECK_NEAR(v.Find("score")->AsNumber(), 2, 1e-9);
  CHECK_NEAR(v.Find("total")->AsNumber(), 3, 1e-9);
  CHECK_EQ(v.Find("items")->AsArray().size(), size_t{3});
}

TEST(ui_config_and_processes_messages) {
  const JsonValue c = Parse(oled::ConfigMessage(oled::Config{}));
  CHECK_EQ(c.Find("type")->AsString(), std::string("config"));
  CHECK_NEAR(c.Find("config")->Find("amplitude_px")->AsNumber(), 2, 1e-9);
  const JsonValue p = Parse(oled::ProcessesMessage({"a.exe", "b.exe"}));
  CHECK_EQ(p.Find("processes")->AsArray().size(), size_t{2});
}

TEST(ui_notice_message_key) {
  const JsonValue v = Parse(oled::NoticeMessage("notice.test_needs_protection", {{"s", "30"}}));
  CHECK_EQ(v.Find("type")->AsString(), std::string("notice"));
  CHECK_EQ(v.Find("key")->AsString(), std::string("notice.test_needs_protection"));
  CHECK(v.Find("text") == nullptr);
  CHECK_EQ(v.Find("params")->Find("s")->AsString(), std::string("30"));
}

TEST(ui_strings_message) {
  JsonValue en = JsonValue::MakeObject();
  en.Set("a", JsonValue("A"));
  en.Set("b", JsonValue("B"));
  JsonValue de = JsonValue::MakeObject();
  de.Set("a", JsonValue("D"));
  const JsonValue v = Parse(oled::StringsMessage(oled::Translations::FromJson("de", de, en)));
  CHECK_EQ(v.Find("type")->AsString(), std::string("strings"));
  CHECK_EQ(v.Find("lang")->AsString(), std::string("de"));
  CHECK_EQ(v.Find("strings")->Find("a")->AsString(), std::string("D"));
  CHECK_EQ(v.Find("strings")->Find("b")->AsString(), std::string("B"));
}

TEST(ui_windows_message_keys) {
  std::vector<oled::WindowsItem> items(1);
  items[0].id = "screensaver";
  items[0].current_key = "windows.value.black_after";
  items[0].current_params = {{"time", "10 min"}};
  items[0].recommended_key = "windows.value.minutes";
  items[0].recommended_params = {{"n", "10"}};
  const JsonValue v = Parse(oled::WindowsMessage(items));
  const JsonValue& it = v.Find("items")->AsArray().at(0);
  CHECK(it.Find("label") == nullptr);
  CHECK_EQ(it.Find("current_key")->AsString(), std::string("windows.value.black_after"));
  CHECK_EQ(it.Find("current_params")->Find("time")->AsString(), std::string("10 min"));
  CHECK_EQ(it.Find("recommended_key")->AsString(), std::string("windows.value.minutes"));
  CHECK_EQ(it.Find("recommended_params")->Find("n")->AsString(), std::string("10"));
}

TEST(ui_parse_set_language) {
  const std::string auto_json = R"({"cmd":"setLanguage","id":"auto"})";
  auto a = oled::ParseUiCommand(auto_json);
  CHECK(a && a->type == oled::UiCommandType::SetLanguage && a->id == "auto");
  const std::string de_json = R"({"cmd":"setLanguage","id":"de"})";
  auto d = oled::ParseUiCommand(de_json);
  CHECK(d && d->id == "de");
  const std::string pt_json = R"({"cmd":"setLanguage","id":"pt"})";
  CHECK(!oled::ParseUiCommand(pt_json).has_value());
  const std::string none_json = R"({"cmd":"setLanguage"})";
  CHECK(!oled::ParseUiCommand(none_json).has_value());
}

TEST(ui_command_parses_known_commands) {
  auto ready = oled::ParseUiCommand(R"({"cmd":"ready"})");
  CHECK(ready && ready->type == UiCommandType::Ready);
  auto rule = oled::ParseUiCommand(R"({"cmd":"setAppRule","exe":"Game.exe","rule":"never"})");
  CHECK(rule && rule->rule == oled::AppRule::NeverPause && rule->exe == "Game.exe");
  auto win = oled::ParseUiCommand(R"({"cmd":"window","action":"close"})");
  CHECK(win && win->action == "close");
  auto cfg = oled::ParseUiCommand(R"({"cmd":"setConfig","patch":{"amplitude_px":3}})");
  CHECK(cfg && cfg->type == UiCommandType::SetConfig && cfg->patch.Find("amplitude_px"));
  auto pause = oled::ParseUiCommand(R"({"cmd":"setManualPause","on":true})");
  CHECK(pause && pause->on);
  auto apply = oled::ParseUiCommand(R"({"cmd":"windowsApply","id":"dark_theme"})");
  CHECK(apply && apply->id == "dark_theme");
  for (const char* simple : {"testShift", "listProcesses", "windowsApplyAll", "windowsRestoreAll", "displayOff"}) {
    const std::string json = std::string(R"({"cmd":")") + simple + "\"}";
    CHECK(oled::ParseUiCommand(json).has_value());
  }
}

TEST(ui_command_rejects_bad_input) {
  for (const char* bad : {"", "[]", "{}", R"({"cmd":"boh"})", R"({"cmd":"setManualPause"})",
                          R"({"cmd":"setManualPause","on":"si"})", R"({"cmd":"setAppRule","exe":"","rule":"never"})",
                          R"({"cmd":"setAppRule","exe":"a.exe","rule":"spesso"})", R"({"cmd":"window","action":"explode"})",
                          R"({"cmd":"setConfig","patch":5})", R"({"cmd":"windowsApply"})", R"({"cmd":5})"}) {
    CHECK(!oled::ParseUiCommand(bad).has_value());
  }
  const std::string long_exe = R"({"cmd":"addApp","exe":")" + std::string(300, 'a') + "\"}";
  CHECK(!oled::ParseUiCommand(long_exe).has_value());
}

TEST(config_patch_applies_valid_values) {
  oled::Config c;
  auto r = oled::ApplyConfigPatch(c, Parse(R"({"amplitude_px":3})"));
  CHECK(!r.parse_failed);
  CHECK(r.warnings.empty());
  CHECK_EQ(r.config.amplitude_px, 3);
  CHECK_EQ(r.config.resume_delay_ms, 2500);
}

TEST(config_patch_invalid_is_rejected) {
  oled::Config c;
  auto r = oled::ApplyConfigPatch(c, Parse(R"({"amplitude_px":9})"));
  CHECK(!r.warnings.empty());
  CHECK_EQ(r.config.amplitude_px, 2);
  auto u = oled::ApplyConfigPatch(c, Parse(R"({"pippo":1})"));
  CHECK(!u.warnings.empty());
}

// ---------------------------------------------------------------- oscuramento

TEST(ui_dim_summarize_keeps_small_grid) {
  const std::vector<float> levels = {0, 0.8f, 0.4f, 0, 0, 0, 0, 0};
  const oled::DimMapSummary s = oled::SummarizeDimLevels(levels, 4, 2, 120);
  CHECK_EQ(s.cols, 4);
  CHECK_EQ(s.rows, 2);
  CHECK_EQ(s.cells, std::string("0f800000"));
  CHECK_NEAR(s.max_level, 0.8, 1e-6);
  CHECK_NEAR(s.dimmed_pct, 25.0, 1e-9);
}

TEST(ui_dim_summarize_downsamples_with_max) {
  std::vector<float> levels(240 * 135, 0.0f);
  levels[67 * 240 + 101] = 0.4f;
  const oled::DimMapSummary s = oled::SummarizeDimLevels(levels, 240, 135, 120);
  CHECK_EQ(s.cols, 120);
  CHECK_EQ(s.rows, 68);
  CHECK_EQ(s.cells.size(), size_t{120 * 68});
  if (s.cells.size() != size_t{120 * 68}) return;
  CHECK_EQ(s.cells[33 * 120 + 50], '8');
  CHECK_EQ(s.cells[0], '0');
  CHECK_NEAR(s.dimmed_pct, 100.0 / 32400, 1e-9);
}

TEST(ui_dim_message_fields) {
  oled::DimUiState st;
  st.status = "suspended";
  st.suspended_by = "photoshop.exe";
  st.map = oled::SummarizeDimLevels({0, 0.8f}, 2, 1, 120);
  st.sdr_white_nits = 240;
  st.hdr = true;
  st.highlight_left_ms = 1200;
  const JsonValue v = Parse(oled::DimMessage(st));
  CHECK(v.type() == JsonValue::Type::Object);
  if (v.type() != JsonValue::Type::Object) return;
  CHECK_EQ(v.Find("type")->AsString(), std::string("dim"));
  CHECK_EQ(v.Find("status")->AsString(), std::string("suspended"));
  CHECK_EQ(v.Find("suspended_by")->AsString(), std::string("photoshop.exe"));
  CHECK_NEAR(v.Find("cols")->AsNumber(), 2, 1e-9);
  CHECK_NEAR(v.Find("rows")->AsNumber(), 1, 1e-9);
  CHECK_EQ(v.Find("cells")->AsString(), std::string("0f"));
  CHECK_NEAR(v.Find("dimmed_pct")->AsNumber(), 50, 1e-9);
  CHECK_NEAR(v.Find("max_level")->AsNumber(), 0.8, 1e-6);
  CHECK_NEAR(v.Find("sdr_white_nits")->AsNumber(), 240, 1e-9);
  CHECK(v.Find("hdr")->AsBool());
  CHECK_NEAR(v.Find("highlight_left_ms")->AsNumber(), 1200, 1e-9);
}

TEST(ui_parse_dim_commands) {
  const auto watch = oled::ParseUiCommand(R"({"cmd":"dimWatch","on":true})");
  CHECK(watch.has_value());
  if (watch) {
    CHECK(watch->type == UiCommandType::DimWatch);
    CHECK(watch->on);
  }
  CHECK(!oled::ParseUiCommand(R"({"cmd":"dimWatch"})").has_value());
  const auto hl = oled::ParseUiCommand(R"({"cmd":"dimHighlight"})");
  CHECK(hl.has_value() && hl->type == UiCommandType::DimHighlight);
}

TEST(ui_state_has_away_and_session) {
  oled::UiState s;
  s.away = true;
  s.session_ms = 123456;
  const JsonValue v = Parse(oled::StateMessage(s));
  CHECK(v.Find("away") && v.Find("away")->AsBool());
  CHECK(v.Find("session_ms") && std::abs(v.Find("session_ms")->AsNumber() - 123456) < 1e-9);
}

TEST(ui_stats_pauses_by_reason) {
  oled::WearStats w;
  w.RollDay(20261121);
  w.CountPause(oled::PauseReason::FullscreenWindow);
  w.CountPause(oled::PauseReason::FullscreenWindow);
  w.CountPause(oled::PauseReason::NotificationState);
  const JsonValue v = Parse(oled::StatsMessage(w, 2));
  const JsonValue* by = v.Find("pauses_by_reason");
  CHECK(by && by->type() == JsonValue::Type::Object);
  if (!by || by->type() != JsonValue::Type::Object) return;
  CHECK_EQ(by->AsObject().size(), size_t{2});
  const std::string full(oled::PauseReasonKey(oled::PauseReason::FullscreenWindow));
  const std::string notif(oled::PauseReasonKey(oled::PauseReason::NotificationState));
  CHECK(by->Find(full) && std::abs(by->Find(full)->AsNumber() - 2) < 1e-9);
  CHECK(by->Find(notif) && std::abs(by->Find(notif)->AsNumber() - 1) < 1e-9);
}

TEST(ui_apps_includes_listed_and_excluded) {
  oled::AppHistory h;
  oled::Config c;
  c.app_list = {"a.exe"};
  c.dim_exclude = {"b.exe"};
  const JsonValue v = Parse(oled::AppsMessage(h, c));
  const auto& apps = v.Find("apps")->AsArray();
  CHECK_EQ(apps.size(), size_t{2});
  bool saw_a = false, saw_b = false;
  for (const auto& a : apps) {
    const std::string exe = a.Find("exe")->AsString();
    CHECK(a.Find("dim_exclude") != nullptr);
    if (exe == "a.exe") {
      saw_a = true;
      CHECK_EQ(a.Find("rule")->AsString(), std::string("auto"));
      CHECK(!a.Find("dim_exclude")->AsBool());
    }
    if (exe == "b.exe") {
      saw_b = true;
      CHECK(a.Find("dim_exclude")->AsBool());
    }
  }
  CHECK(saw_a && saw_b);
}

TEST(ui_monitors_message_fields) {
  std::vector<oled::MonitorItem> items(2);
  items[0].device = "DISPLAY1";
  items[0].name = "Dell";
  items[0].primary = true;
  items[1].device = "DISPLAY2";
  items[1].name = "LG TV SSCR2";
  items[1].width = 3840;
  items[1].height = 2160;
  items[1].refresh_hz = 144;
  items[1].hdr = true;
  items[1].is_protected = true;
  items[1].connection = "HDMI";
  const JsonValue v = Parse(oled::MonitorsMessage(items, "auto"));
  CHECK_EQ(v.Find("type")->AsString(), std::string("monitors"));
  CHECK_EQ(v.Find("setting")->AsString(), std::string("auto"));
  const auto& list = v.Find("monitors")->AsArray();
  CHECK_EQ(list.size(), size_t{2});
  if (list.size() != 2) return;
  CHECK(list[0].Find("primary")->AsBool());
  CHECK(!list[0].Find("protected")->AsBool());
  CHECK_EQ(list[1].Find("device")->AsString(), std::string("DISPLAY2"));
  CHECK_EQ(list[1].Find("name")->AsString(), std::string("LG TV SSCR2"));
  CHECK_NEAR(list[1].Find("width")->AsNumber(), 3840, 1e-9);
  CHECK_NEAR(list[1].Find("height")->AsNumber(), 2160, 1e-9);
  CHECK_NEAR(list[1].Find("refresh_hz")->AsNumber(), 144, 1e-9);
  CHECK(list[1].Find("hdr")->AsBool());
  CHECK(list[1].Find("protected")->AsBool());
  CHECK_EQ(list[1].Find("connection")->AsString(), std::string("HDMI"));
}

TEST(ui_monitors_message_none_protected) {
  std::vector<oled::MonitorItem> items(1);
  items[0].device = "DISPLAY1";
  const JsonValue v = Parse(oled::MonitorsMessage(items, "LG TV SSCR2"));
  CHECK(v.type() == JsonValue::Type::Object);
  if (v.type() != JsonValue::Type::Object) return;
  CHECK(!v.Find("monitors")->AsArray()[0].Find("protected")->AsBool());
  const JsonValue empty = Parse(oled::MonitorsMessage({}, "auto"));
  CHECK(empty.Find("monitors") && empty.Find("monitors")->AsArray().empty());
}

TEST(ui_activity_messages) {
  oled::ActivityLog log;
  log.Add(1000, "pause", "activity.paused", {{"reason", "pause.reason.manual"}});
  log.Add(2000, "resume", "activity.resumed");
  const JsonValue all = Parse(oled::ActivityMessage(log));
  CHECK_EQ(all.Find("type")->AsString(), std::string("activity"));
  const auto& events = all.Find("events")->AsArray();
  CHECK_EQ(events.size(), size_t{2});
  if (events.size() == 2) {
    CHECK_EQ(events[0].Find("kind")->AsString(), std::string("resume"));
    CHECK_NEAR(events[1].Find("t")->AsNumber(), 1000, 1e-9);
  }
  CHECK(events.size() == 2 && events[1].Find("text") == nullptr);
  CHECK(events.size() == 2 && events[1].Find("params")->Find("reason")->AsString() == "pause.reason.manual");
  const auto& merged = log.Add(3000, "resume", "activity.resumed");
  const JsonValue add = Parse(oled::ActivityAddMessage(merged));
  CHECK_EQ(add.Find("type")->AsString(), std::string("activity"));
  CHECK(add.Find("add") != nullptr);
  if (!add.Find("add")) return;
  CHECK_EQ(add.Find("add")->Find("key")->AsString(), std::string("activity.resumed"));
  CHECK_NEAR(add.Find("add")->Find("count")->AsNumber(), 2, 1e-9);
}

TEST(ui_about_and_browsed_messages) {
  const JsonValue about = Parse(oled::AboutMessage("1.0.0", "C:/cfg \"x\"/config.json", "C:/log"));
  CHECK_EQ(about.Find("type")->AsString(), std::string("about"));
  CHECK_EQ(about.Find("version")->AsString(), std::string("1.0.0"));
  CHECK_EQ(about.Find("config_path")->AsString(), std::string("C:/cfg \"x\"/config.json"));
  CHECK_EQ(about.Find("log_dir")->AsString(), std::string("C:/log"));
  const JsonValue br = Parse(oled::BrowsedMessage("gioco.exe"));
  CHECK_EQ(br.Find("type")->AsString(), std::string("browsed"));
  CHECK_EQ(br.Find("exe")->AsString(), std::string("gioco.exe"));
}

TEST(ui_parse_new_commands) {
  for (const char* simple : {"openLogFolder", "browseExe", "clearActivity"}) {
    const std::string json = std::string(R"({"cmd":")") + simple + "\"}";
    CHECK(oled::ParseUiCommand(json).has_value());
  }
  auto log = oled::ParseUiCommand(R"({"cmd":"openLogFolder"})");
  CHECK(log && log->type == UiCommandType::OpenLogFolder);
  auto mon = oled::ParseUiCommand(R"({"cmd":"setMonitor","device":"auto"})");
  CHECK(mon && mon->type == UiCommandType::SetMonitor && mon->id == "auto");
  auto mon2 = oled::ParseUiCommand(R"({"cmd":"setMonitor","device":"DISPLAY2"})");
  CHECK(mon2 && mon2->id == "DISPLAY2");
  auto ex = oled::ParseUiCommand(R"({"cmd":"setDimExclude","exe":"x.exe","on":true})");
  CHECK(ex && ex->type == UiCommandType::SetDimExclude && ex->exe == "x.exe" && ex->on);
}

TEST(ui_parse_new_commands_reject) {
  for (const char* bad : {R"({"cmd":"setMonitor"})", R"({"cmd":"setMonitor","device":""})",
                          R"({"cmd":"setMonitor","device":5})", R"({"cmd":"setDimExclude","exe":"x.exe"})"}) {
    CHECK(!oled::ParseUiCommand(bad).has_value());
  }
  const std::string long_exe = R"({"cmd":"setDimExclude","on":true,"exe":")" + std::string(261, 'a') + "\"}";
  CHECK(!oled::ParseUiCommand(long_exe).has_value());
}

TEST(ui_signals_messages) {
  oled::SignalSample s{true, 0.24, 0.1, 0.41};
  const JsonValue v = Parse(oled::SignalsMessage(s));
  CHECK_EQ(v.Find("type")->AsString(), std::string("signals"));
  CHECK(v.Find("measured")->AsBool());
  CHECK_NEAR(v.Find("risk")->AsNumber(), 0.24, 1e-9);
  CHECK_NEAR(v.Find("motion")->AsNumber(), 0.1, 1e-9);
  CHECK_NEAR(v.Find("brightness")->AsNumber(), 0.41, 1e-9);
  CHECK_EQ(v.Find("caption_key")->AsString(), std::string("signals.caption.work"));
  oled::SignalRing ring;
  ring.Push(s);
  ring.Push(oled::SignalSample{});
  const JsonValue h = Parse(oled::SignalsHistoryMessage(ring));
  CHECK_EQ(h.Find("type")->AsString(), std::string("signals"));
  const auto& list = h.Find("history")->AsArray();
  CHECK_EQ(list.size(), size_t{2});
  if (list.size() == 2) {
    CHECK_EQ(list[0].AsArray().size(), size_t{3});
    CHECK(list[1].type() == JsonValue::Type::Null);
  }
}

TEST(ui_history_message_fields) {
  oled::HistoryPayload p;
  p.timeline.assign(oled::kTimelineBuckets, std::nullopt);
  p.timeline.back() = 0.3;
  p.timeline_end_local_s = 123456;
  p.risk_peak_24h = 0.5;
  p.wear.hottest = 3;
  p.wear.hottest_key = "wear.zone.top_left";
  p.wear.normalized[3] = 1;
  p.wear_monitor = "LG TV SSCR2";
  p.top_apps = {oled::AppView{"code.exe", 100, 20}};
  p.top_app_rules = {"never"};
  p.content = {0.5, 0.2, 0.2, 0.1, 0};
  p.sessions_total = 47;
  p.sessions_week = 3;
  p.streak = 12;
  p.streak_best = 19;
  p.avoided_total = 0.31;
  p.protected_week_s = 18720;
  const JsonValue v = Parse(oled::HistoryMessage(p));
  CHECK_EQ(v.Find("type")->AsString(), std::string("history"));
  const auto& t = v.Find("timeline")->AsArray();
  CHECK_EQ(t.size(), size_t{oled::kTimelineBuckets});
  CHECK(t.front().type() == JsonValue::Type::Null);
  CHECK_NEAR(t.back().AsNumber(), 0.3, 1e-9);
  CHECK_NEAR(v.Find("timeline_end")->AsNumber(), 123456, 1e-9);
  CHECK_NEAR(v.Find("risk_peak_24h")->AsNumber(), 0.5, 1e-9);
  const JsonValue* wear = v.Find("wear");
  CHECK(wear && wear->Find("cells") && wear->Find("cells")->AsArray().size() == size_t{oled::kZones});
  if (wear) {
    CHECK_EQ(wear->Find("monitor")->AsString(), std::string("LG TV SSCR2"));
    CHECK_EQ(wear->Find("hottest_key")->AsString(), std::string("wear.zone.top_left"));
    CHECK(wear->Find("hottest_hours") && wear->Find("concentration") && wear->Find("over_double") &&
          wear->Find("measured_hours") && wear->Find("since"));
  }
  const auto& apps = v.Find("top_apps")->AsArray();
  CHECK_EQ(apps.size(), size_t{1});
  if (!apps.empty()) {
    CHECK_EQ(apps[0].Find("exe")->AsString(), std::string("code.exe"));
    CHECK_EQ(apps[0].Find("rule")->AsString(), std::string("never"));
    CHECK_NEAR(apps[0].Find("paused_s")->AsNumber(), 20, 1e-9);
  }
  CHECK_EQ(v.Find("content")->AsArray().size(), size_t{5});
  CHECK_NEAR(v.Find("sessions_total")->AsNumber(), 47, 1e-9);
  CHECK_NEAR(v.Find("sessions_week")->AsNumber(), 3, 1e-9);
  CHECK_NEAR(v.Find("streak")->AsNumber(), 12, 1e-9);
  CHECK_NEAR(v.Find("streak_best")->AsNumber(), 19, 1e-9);
  CHECK(v.Find("avoided_today")->type() == JsonValue::Type::Null);
  CHECK_NEAR(v.Find("avoided_total")->AsNumber(), 0.31, 1e-9);
  CHECK_NEAR(v.Find("protected_week_s")->AsNumber(), 18720, 1e-9);
}

TEST(ui_parse_clear_history) {
  auto c = oled::ParseUiCommand(R"({"cmd":"clearHistory"})");
  CHECK(c && c->type == UiCommandType::ClearHistory);
}

TEST(ui_parse_setup_commands) {
  const std::string identify = R"({"cmd":"identifyMonitors"})";
  auto a = oled::ParseUiCommand(identify);
  CHECK(a && a->type == oled::UiCommandType::IdentifyMonitors);
  const std::string finish = R"({"cmd":"finishSetup","device":"\\\\.\\DISPLAY2","autostart":true})";
  auto f = oled::ParseUiCommand(finish);
  CHECK(f && f->type == oled::UiCommandType::FinishSetup && f->id == "\\\\.\\DISPLAY2" && f->on);
  const std::string finish_auto = R"({"cmd":"finishSetup","device":"auto","autostart":false})";
  auto g = oled::ParseUiCommand(finish_auto);
  CHECK(g && g->id == "auto" && !g->on);
  const std::string missing = R"({"cmd":"finishSetup","device":"auto"})";
  CHECK(!oled::ParseUiCommand(missing).has_value());
  const std::string empty = R"({"cmd":"finishSetup","device":"","autostart":true})";
  CHECK(!oled::ParseUiCommand(empty).has_value());
}

TEST(ui_state_setup_pending) {
  oled::UiState s;
  s.setup_pending = true;
  const JsonValue v = Parse(oled::StateMessage(s));
  CHECK(v.Find("setup_pending") && v.Find("setup_pending")->AsBool());
}

TEST(ui_navigate_message) {
  const JsonValue v = Parse(oled::NavigateMessage("monitor"));
  CHECK_EQ(v.Find("type")->AsString(), std::string("navigate"));
  CHECK_EQ(v.Find("page")->AsString(), std::string("monitor"));
}
