#include <array>

#include "json.h"
#include "test_framework.h"
#include "wear_stats.h"

using oled::JsonValue;
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

// Statistiche con le 49 celle date per indice (riga per riga, da (-3,-3)), senza
// costruire posizioni dentro un ciclo: MSVC 14.52 (/O2) compila male {x, y} con x
// contatore di ciclo negativo (vedi WearStats::CellMs(int, int)).
WearStats WithCells(const std::array<double, 49>& cells) {
  JsonValue o = JsonValue::MakeObject();
  JsonValue arr = JsonValue::MakeArray();
  for (double c : cells) arr.Push(JsonValue(c));
  o.Set("cells", arr);
  return WearStats::FromJson(o);
}

std::array<double, 49> DistinctCells() {
  std::array<double, 49> cells{};
  for (size_t i = 0; i < cells.size(); ++i) cells[i] = 1000.0 + static_cast<double>(i);
  return cells;
}

TEST(wear_stats_cell_layout) {
  const WearStats s = WithCells(DistinctCells());
  for (int y = -3; y <= 3; ++y)
    for (int x = -3; x <= 3; ++x) CHECK_NEAR(s.CellMs(x, y), 1000.0 + (y + 3) * 7 + (x + 3), 1e-9);
  CHECK_NEAR(s.CellMs({-1, 1}), 1000.0 + 4 * 7 + 2, 1e-9);
  CHECK_NEAR(s.CellMs(4, 0), 0.0, 1e-9);
}

TEST(wear_stats_add_goes_to_its_own_cell) {
  WearStats s;
  s.AddProtectedTime({-1, -1}, 1);
  s.AddProtectedTime({-1, 1}, 2);
  s.AddProtectedTime({-3, 2}, 4);
  s.AddProtectedTime({2, -3}, 8);
  const JsonValue json = s.ToJson();
  const auto& cells = json.Find("cells")->AsArray();
  CHECK_NEAR(cells[2 * 7 + 2].AsNumber(), 1, 1e-9);
  CHECK_NEAR(cells[4 * 7 + 2].AsNumber(), 2, 1e-9);
  CHECK_NEAR(cells[5 * 7 + 0].AsNumber(), 4, 1e-9);
  CHECK_NEAR(cells[0 * 7 + 5].AsNumber(), 8, 1e-9);
}

TEST(wear_stats_uniformity) {
  CHECK(!WearStats{}.Uniformity(1).has_value());
  std::array<double, 49> cells{};
  for (int y = 2; y <= 4; ++y)
    for (int x = 2; x <= 4; ++x) cells[static_cast<size_t>(y * 7 + x)] = 100000;  // il riquadro +-1
  WearStats s = WithCells(cells);
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
