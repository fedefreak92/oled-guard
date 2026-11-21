#include <string>

#include "app_history.h"
#include "json.h"
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
  CHECK_EQ(v.Find("reason")->AsString(), std::string("Finestra a tutto schermo"));
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
    CHECK(oled::ParseUiCommand(std::string(R"({"cmd":")") + simple + "\"}").has_value());
  }
}

TEST(ui_command_rejects_bad_input) {
  for (const char* bad : {"", "[]", "{}", R"({"cmd":"boh"})", R"({"cmd":"setManualPause"})",
                          R"({"cmd":"setManualPause","on":"si"})", R"({"cmd":"setAppRule","exe":"","rule":"never"})",
                          R"({"cmd":"setAppRule","exe":"a.exe","rule":"spesso"})", R"({"cmd":"window","action":"explode"})",
                          R"({"cmd":"setConfig","patch":5})", R"({"cmd":"windowsApply"})", R"({"cmd":5})"}) {
    CHECK(!oled::ParseUiCommand(bad).has_value());
  }
  CHECK(!oled::ParseUiCommand(R"({"cmd":"addApp","exe":")" + std::string(300, 'a') + "\"}").has_value());
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
