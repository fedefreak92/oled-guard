#include <string>

#include "config.h"
#include "hotkey_parse.h"
#include "test_framework.h"

using oled::Config;
using oled::ConfigFromJson;
using oled::ConfigToJson;

namespace {
bool AnyContains(const std::vector<std::string>& warnings, const std::string& needle) {
  for (const auto& w : warnings)
    if (w.find(needle) != std::string::npos) return true;
  return false;
}
}  // namespace

TEST(config_defaults_from_empty_object) {
  auto r = ConfigFromJson("{}");
  CHECK(!r.parse_failed);
  CHECK(r.warnings.empty());
  CHECK_EQ(r.config.amplitude_px, 2);
  CHECK_EQ(r.config.resume_delay_ms, 2500);
  CHECK_EQ(r.config.pause_enter_delay_ms, 300);
  CHECK_EQ(r.config.hotkey_emergency_exit, std::string("Ctrl+Alt+Shift+F12"));
  CHECK_EQ(r.config.hotkey_toggle, std::string("Ctrl+Alt+Shift+P"));
  CHECK_EQ(r.config.monitor, std::string("auto"));
  CHECK(r.config.allow_tearing == false);
  CHECK(r.config == Config{});
}

TEST(config_reads_valid_values) {
  auto r = ConfigFromJson(R"({"amplitude_px": 3, "dwell_min_s": 10, "dwell_max_s": 30,
    "transition_min_s": 1.5, "transition_max_s": 4, "allow_tearing": true, "monitor": "LG TV SSCR2",
    "resume_delay_ms": 3000, "log_level": "debug", "allowlist": ["mpv.exe"]})");
  CHECK(r.warnings.empty());
  CHECK_EQ(r.config.amplitude_px, 3);
  CHECK_NEAR(r.config.transition_min_s, 1.5, 1e-9);
  CHECK(r.config.allow_tearing);
  CHECK_EQ(r.config.monitor, std::string("LG TV SSCR2"));
  CHECK_EQ(r.config.resume_delay_ms, 3000);
  CHECK_EQ(r.config.log_level, std::string("debug"));
  CHECK_EQ(r.config.allowlist.size(), size_t{1});
}

TEST(config_out_of_range_uses_default) {
  auto r = ConfigFromJson(R"({"amplitude_px": 7})");
  CHECK_EQ(r.config.amplitude_px, 2);
  CHECK_EQ(r.warnings.size(), size_t{1});
  CHECK(AnyContains(r.warnings, "amplitude_px"));
}

TEST(config_non_integer_uses_default) {
  auto r = ConfigFromJson(R"({"amplitude_px": 2.5})");
  CHECK_EQ(r.config.amplitude_px, 2);
  CHECK_EQ(r.warnings.size(), size_t{1});
}

TEST(config_wrong_type_uses_default) {
  auto r = ConfigFromJson(R"({"allow_tearing": "si"})");
  CHECK(r.config.allow_tearing == false);
  CHECK_EQ(r.warnings.size(), size_t{1});
}

TEST(config_min_greater_than_max) {
  auto r = ConfigFromJson(R"({"dwell_min_s": 90, "dwell_max_s": 30})");
  CHECK_NEAR(r.config.dwell_min_s, 20, 1e-9);
  CHECK_NEAR(r.config.dwell_max_s, 60, 1e-9);
  CHECK_EQ(r.warnings.size(), size_t{1});
}

TEST(config_invalid_log_level) {
  auto r = ConfigFromJson(R"({"log_level": "verbose"})");
  CHECK_EQ(r.config.log_level, std::string("info"));
  CHECK_EQ(r.warnings.size(), size_t{1});
}

TEST(config_lists_are_lowercased) {
  auto r = ConfigFromJson(R"({"denylist": ["Game.EXE", 3]})");
  CHECK_EQ(r.config.denylist.size(), size_t{1});
  if (!r.config.denylist.empty()) CHECK_EQ(r.config.denylist[0], std::string("game.exe"));
  CHECK_EQ(r.warnings.size(), size_t{1});
}

TEST(config_rejects_equal_hotkeys) {
  auto r = ConfigFromJson(R"({"hotkey_toggle": "Ctrl+Alt+X", "hotkey_emergency_exit": "ctrl+alt+x"})");
  CHECK_EQ(r.config.hotkey_toggle, std::string("Ctrl+Alt+Shift+P"));
  CHECK_EQ(r.config.hotkey_emergency_exit, std::string("Ctrl+Alt+Shift+F12"));
  CHECK(!r.warnings.empty());
}

TEST(config_invalid_hotkey_default_does_not_collide) {
  // Toggle impostata uguale al default di emergenza + emergenza scritta male:
  // il default dell'emergenza non deve finire uguale alla toggle.
  auto r = ConfigFromJson(R"({"hotkey_toggle": "Ctrl+Alt+Shift+F12", "hotkey_emergency_exit": "Ctrl+Pippo"})");
  CHECK(!(oled::ParseHotkey(r.config.hotkey_toggle) == oled::ParseHotkey(r.config.hotkey_emergency_exit)));
  CHECK_EQ(r.config.hotkey_toggle, std::string("Ctrl+Alt+Shift+P"));
  CHECK_EQ(r.config.hotkey_emergency_exit, std::string("Ctrl+Alt+Shift+F12"));
  auto r2 = ConfigFromJson(R"({"hotkey_toggle": "boh", "hotkey_emergency_exit": "Ctrl+Alt+Shift+P"})");
  CHECK(!(oled::ParseHotkey(r2.config.hotkey_toggle) == oled::ParseHotkey(r2.config.hotkey_emergency_exit)));
}

TEST(config_rejects_unparsable_hotkey) {
  auto r = ConfigFromJson(R"({"hotkey_toggle": "F12"})");
  CHECK_EQ(r.config.hotkey_toggle, std::string("Ctrl+Alt+Shift+P"));
  CHECK(!r.warnings.empty());
}

TEST(config_unknown_key_warns) {
  auto r = ConfigFromJson(R"({"foo": 1})");
  CHECK_EQ(r.warnings.size(), size_t{1});
  CHECK(AnyContains(r.warnings, "foo"));
}

TEST(config_malformed_json) {
  auto r = ConfigFromJson("{");
  CHECK(r.parse_failed);
  CHECK(!r.warnings.empty());
  auto top = ConfigFromJson("[1]");
  CHECK(top.parse_failed);
}

TEST(config_roundtrip) {
  Config c;
  c.monitor = "\\\\.\\DISPLAY2";
  c.amplitude_px = 1;
  c.dwell_min_s = 30;
  c.dwell_max_s = 90;
  c.transition_min_s = 2.5;
  c.transition_max_s = 6;
  c.pause_on_notification_state = false;
  c.pause_on_fullscreen_window = false;
  c.pause_enter_delay_ms = 500;
  c.resume_delay_ms = 3000;
  c.denylist = {"game.exe"};
  c.allowlist = {"mpv.exe", "vlc.exe"};
  c.hotkey_toggle = "Ctrl+Alt+O";
  c.hotkey_emergency_exit = "Ctrl+Alt+Shift+F11";
  c.allow_tearing = true;
  c.autostart = true;
  c.log_level = "warn";
  auto r = ConfigFromJson(ConfigToJson(c));
  CHECK(r.warnings.empty());
  CHECK(r.config == c);
}

TEST(config_conversions) {
  Config c;
  c.denylist = {"a.exe"};
  const auto rules = oled::ToPauseRules(c);
  CHECK(rules.pause_on_fullscreen_window);
  CHECK_EQ(rules.denylist.size(), size_t{1});
  const auto timing = oled::ToShiftTiming(c);
  CHECK_NEAR(timing.dwell_min_ms, 20000, 1e-9);
  CHECK_NEAR(timing.transition_max_ms, 5000, 1e-9);
}
