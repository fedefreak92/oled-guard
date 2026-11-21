#include <string>

#include "app_history.h"
#include "test_framework.h"

using oled::AppHistory;
using oled::AppRule;
using oled::PauseReason;

TEST(app_history_records_most_recent_first) {
  AppHistory h;
  h.Record("Game.exe", PauseReason::FullscreenWindow, 100);
  h.Record("vlc.exe", PauseReason::FullscreenWindow, 200);
  h.Record("GAME.EXE", PauseReason::NotificationState, 300);
  CHECK_EQ(h.Entries().size(), size_t{2});
  if (h.Entries().size() != 2) return;
  CHECK_EQ(h.Entries()[0].exe, std::string("game.exe"));
  CHECK_EQ(h.Entries()[0].count, 2);
  CHECK(h.Entries()[0].reason == PauseReason::NotificationState);
  CHECK_EQ(h.Entries()[0].last_seen_s, int64_t{300});
  CHECK_EQ(h.Entries()[1].exe, std::string("vlc.exe"));
}

TEST(app_history_is_capped) {
  AppHistory h;
  for (int i = 0; i < 60; ++i) h.Record("exe" + std::to_string(i) + ".exe", PauseReason::Manual, i);
  CHECK_EQ(h.Entries().size(), AppHistory::kMax);
  bool has_oldest = false;
  for (const auto& e : h.Entries()) has_oldest = has_oldest || e.exe == "exe0.exe";
  CHECK(!has_oldest);
  CHECK_EQ(h.Entries()[0].exe, std::string("exe59.exe"));
}

TEST(app_history_ignores_empty_exe) {
  AppHistory h;
  h.Record("", PauseReason::Manual, 1);
  CHECK(h.Entries().empty());
}

TEST(app_history_remove) {
  AppHistory h;
  h.Record("a.exe", PauseReason::Manual, 1);
  h.Record("b.exe", PauseReason::Manual, 2);
  h.Remove("A.EXE");
  CHECK_EQ(h.Entries().size(), size_t{1});
  CHECK_EQ(h.Entries()[0].exe, std::string("b.exe"));
}

TEST(app_rule_moves_between_lists) {
  oled::Config c;
  c.denylist = {"game.exe"};
  CHECK(oled::RuleFor(c, "GAME.exe") == AppRule::AlwaysPause);
  c = oled::WithRule(c, "Game.EXE", AppRule::NeverPause);
  CHECK(c.denylist.empty());
  CHECK_EQ(c.allowlist.size(), size_t{1});
  if (!c.allowlist.empty()) CHECK_EQ(c.allowlist[0], std::string("game.exe"));
  CHECK(oled::RuleFor(c, "game.exe") == AppRule::NeverPause);
  c = oled::WithRule(c, "game.exe", AppRule::NeverPause);
  CHECK_EQ(c.allowlist.size(), size_t{1});
  c = oled::WithRule(c, "game.exe", AppRule::Auto);
  CHECK(c.allowlist.empty() && c.denylist.empty());
  CHECK(oled::RuleFor(c, "game.exe") == AppRule::Auto);
}

TEST(app_history_json_roundtrip) {
  AppHistory h;
  h.Record("vlc.exe", PauseReason::FullscreenWindow, 1000);
  h.Record("game.exe", PauseReason::NotificationState, 2000);
  h.Record("game.exe", PauseReason::NotificationState, 3000);
  const AppHistory t = AppHistory::FromJson(h.ToJson());
  CHECK_EQ(t.Entries().size(), size_t{2});
  if (t.Entries().size() == 2) {
    CHECK_EQ(t.Entries()[0].exe, std::string("game.exe"));
    CHECK_EQ(t.Entries()[0].count, 2);
    CHECK_EQ(t.Entries()[0].last_seen_s, int64_t{3000});
    CHECK(t.Entries()[0].reason == PauseReason::NotificationState);
    CHECK_EQ(t.Entries()[1].exe, std::string("vlc.exe"));
  }
  CHECK(AppHistory::FromJson(oled::JsonValue(3)).Entries().empty());
}

TEST(app_rules_listed_and_removed) {
  oled::Config c;
  c = oled::WithListedApp(c, "Foo.exe");
  c = oled::WithListedApp(c, "foo.exe");
  CHECK_EQ(c.app_list.size(), size_t{1});
  CHECK_EQ(c.app_list[0], std::string("foo.exe"));

  oled::Config all;
  all.app_list = {"foo.exe"};
  all.denylist = {"foo.exe", "bar.exe"};
  all.allowlist = {"foo.exe"};
  all.dim_exclude = {"foo.exe"};
  const oled::Config cleaned = oled::WithoutApp(all, "FOO.exe");
  CHECK(cleaned.app_list.empty());
  CHECK_EQ(cleaned.denylist.size(), size_t{1});
  CHECK(cleaned.allowlist.empty());
  CHECK(cleaned.dim_exclude.empty());

  const oled::Config start;
  const oled::Config on = oled::WithDimExclude(start, "Foo.exe", true);
  CHECK_EQ(on.dim_exclude.size(), size_t{1});
  CHECK_EQ(on.dim_exclude[0], std::string("foo.exe"));
  CHECK(oled::WithDimExclude(on, "foo.exe", true) == on);
  CHECK(oled::WithDimExclude(on, "foo.exe", false) == start);
}
