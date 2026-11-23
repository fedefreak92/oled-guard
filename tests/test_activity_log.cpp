#include <string>

#include "activity_log.h"
#include "test_framework.h"

using oled::ActivityLog;

TEST(activity_newest_first) {
  ActivityLog log;
  log.Add(1000, "pause", "In pausa");
  log.Add(2000, "resume", "Attivo");
  CHECK_EQ(log.Events().size(), size_t{2});
  CHECK_EQ(log.Events().front().kind, std::string("resume"));
}

TEST(activity_merges_same_within_5s) {
  ActivityLog log;
  log.Add(0, "pause", "In pausa");
  const auto& merged = log.Add(4999, "pause", "In pausa");
  CHECK_EQ(log.Events().size(), size_t{1});
  CHECK_EQ(merged.count, 2);
  CHECK_EQ(log.Events().front().t_ms, int64_t{4999});
  log.Add(10100, "pause", "In pausa");
  CHECK_EQ(log.Events().size(), size_t{2});
  CHECK_EQ(log.Events().front().count, 1);
}

TEST(activity_caps_at_100) {
  ActivityLog log;
  for (int i = 1; i <= 150; ++i) log.Add(i * 10000, "config", "evento " + std::to_string(i));
  CHECK_EQ(log.Events().size(), size_t{100});
  CHECK_EQ(log.Events().back().key, std::string("evento 51"));
}

TEST(activity_clear) {
  ActivityLog log;
  log.Add(1, "test", "Prova");
  log.Clear();
  CHECK(log.Events().empty());
}

TEST(activity_stores_key_and_params) {
  ActivityLog log;
  log.Add(0, "pause", "activity.paused_app", {{"reason", "pause.reason.fullscreen"}, {"app", "game.exe"}});
  const auto& merged = log.Add(4000, "pause", "activity.paused_app", {{"reason", "pause.reason.fullscreen"}, {"app", "game.exe"}});
  CHECK_EQ(log.Events().size(), size_t{1});
  CHECK_EQ(merged.count, 2);
  CHECK_EQ(merged.key, std::string("activity.paused_app"));
  CHECK_EQ(merged.params.size(), size_t{2});
  log.Add(4500, "pause", "activity.paused_app", {{"reason", "pause.reason.fullscreen"}, {"app", "other.exe"}});
  CHECK_EQ(log.Events().size(), size_t{2});
}
