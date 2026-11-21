#include "json.h"
#include "test_framework.h"
#include "wear_stats.h"

using oled::Offset;
using oled::PauseReason;
using oled::WearStats;

TEST(wear_stats_accumulates_cells_and_totals) {
  WearStats s;
  s.RollDay(20261121);
  s.AddProtectedTime({1, -1}, 1000);
  s.AddProtectedTime({1, -1}, 500);
  s.AddProtectedTime({4, 0}, 999);  // fuori da +-3: solo nel totale
  CHECK_NEAR(s.CellMs({1, -1}), 1500, 1e-9);
  CHECK_NEAR(s.CellMs({4, 0}), 0, 1e-9);
  CHECK_NEAR(s.TodayProtectedMs(), 2499, 1e-9);
  CHECK_NEAR(s.TotalProtectedMs(), 2499, 1e-9);
}

TEST(wear_stats_day_rollover) {
  WearStats s;
  s.RollDay(20261121);
  s.AddProtectedTime({0, 0}, 5000);
  s.CountPause(PauseReason::FullscreenWindow);
  s.RollDay(20261121);
  CHECK_NEAR(s.TodayProtectedMs(), 5000, 1e-9);
  CHECK_EQ(s.TodayPauses(), 1);
  s.RollDay(20261122);
  CHECK_NEAR(s.TodayProtectedMs(), 0, 1e-9);
  CHECK_NEAR(s.TotalProtectedMs(), 5000, 1e-9);
  CHECK_EQ(s.TodayPauses(), 0);
  CHECK_NEAR(s.CellMs({0, 0}), 5000, 1e-9);
}

TEST(wear_stats_counts_pauses_by_reason) {
  WearStats s;
  s.RollDay(1);
  s.CountPause(PauseReason::FullscreenWindow);
  s.CountPause(PauseReason::FullscreenWindow);
  s.CountPause(PauseReason::Manual);
  CHECK_EQ(s.TodayPauses(), 3);
  CHECK_EQ(s.TodayPauses(PauseReason::FullscreenWindow), 2);
  CHECK_EQ(s.TodayPauses(PauseReason::Denylisted), 0);
}

TEST(wear_stats_uniformity) {
  WearStats s;
  s.RollDay(1);
  CHECK(!s.Uniformity(1).has_value());
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) s.AddProtectedTime({x, y}, 100000);
  CHECK(s.Uniformity(1).has_value());
  if (s.Uniformity(1)) CHECK_NEAR(*s.Uniformity(1), 100.0, 1e-9);
  s.AddProtectedTime({0, 0}, 800000);  // una cella molto piu' usata
  if (s.Uniformity(1)) CHECK(*s.Uniformity(1) < 50.0);
}

TEST(wear_stats_json_roundtrip) {
  WearStats s;
  s.RollDay(20261121);
  s.AddProtectedTime({-3, 3}, 1234.5);
  s.AddProtectedTime({2, 0}, 42);
  s.CountPause(PauseReason::NotificationState);
  const WearStats t = WearStats::FromJson(s.ToJson());
  CHECK_NEAR(t.CellMs({-3, 3}), 1234.5, 1e-9);
  CHECK_NEAR(t.CellMs({2, 0}), 42, 1e-9);
  CHECK_NEAR(t.TodayProtectedMs(), 1276.5, 1e-9);
  CHECK_NEAR(t.TotalProtectedMs(), 1276.5, 1e-9);
  CHECK_EQ(t.TodayPauses(PauseReason::NotificationState), 1);
  WearStats u = t;
  u.RollDay(20261121);  // stesso giorno salvato: "oggi" non si azzera
  CHECK_NEAR(u.TodayProtectedMs(), 1276.5, 1e-9);
}

TEST(wear_stats_from_garbage_is_empty) {
  const WearStats s = WearStats::FromJson(oled::JsonValue("boh"));
  CHECK_NEAR(s.TotalProtectedMs(), 0, 1e-9);
  const auto parsed = oled::ParseJson(R"({"cells":[1,"x"],"total_ms":-5,"today_ms":"a"})");
  CHECK(parsed.value.has_value());
  if (parsed.value) {
    const WearStats t = WearStats::FromJson(*parsed.value);
    CHECK_NEAR(t.TotalProtectedMs(), 0, 1e-9);
    CHECK_NEAR(t.TodayProtectedMs(), 0, 1e-9);
    CHECK_NEAR(t.CellMs({-3, -3}), 0, 1e-9);
  }
}
