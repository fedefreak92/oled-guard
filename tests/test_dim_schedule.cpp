#include <limits>

#include "dim_schedule.h"
#include "test_framework.h"

using oled::DimParams;
using oled::DimRedrawSchedule;

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
}

TEST(dim_schedule_no_activity_no_redraw) {
  DimRedrawSchedule s;
  CHECK(s.NextRedrawAt(1000, DimParams{}, 7) == kInf);
}

TEST(dim_schedule_fast_after_frame) {
  DimRedrawSchedule s;
  s.OnNewFrame(1000);
  s.OnRedraw(1000);
  CHECK_NEAR(s.NextRedrawAt(1000, DimParams{}, 7), 1007, 1e-9);
}

TEST(dim_schedule_slow_after_fast_window) {
  DimRedrawSchedule s;
  s.OnNewFrame(1000);
  s.OnRedraw(1700);
  CHECK_NEAR(s.NextRedrawAt(1700, DimParams{}, 7), 2200, 1e-9);
}

TEST(dim_schedule_stops_after_bound) {
  DimRedrawSchedule s;
  s.OnNewFrame(0);
  s.OnRedraw(512000);
  CHECK_NEAR(s.NextRedrawAt(512000, DimParams{}, 7), 512500, 1e-9);
  s.OnRedraw(513000);
  CHECK(s.NextRedrawAt(513001, DimParams{}, 7) == kInf);
}

TEST(dim_schedule_large_extends_bound) {
  DimRedrawSchedule s;
  s.OnNewFrame(0);
  s.OnRedraw(600000);
  CHECK(s.NextRedrawAt(600000, DimParams{}, 7) == kInf);
  DimParams p;
  p.large_enabled = true;
  CHECK_NEAR(s.NextRedrawAt(600000, p, 7), 600500, 1e-9);
  CHECK(s.NextRedrawAt(1533001, p, 7) == kInf);
}

TEST(dim_schedule_poke_opens_fast_window) {
  DimRedrawSchedule s;
  s.Poke(5000);
  s.OnRedraw(6000);
  CHECK_NEAR(s.NextRedrawAt(6000, DimParams{}, 7), 6007, 1e-9);
  s.OnRedraw(6200);
  CHECK_NEAR(s.NextRedrawAt(6200, DimParams{}, 7), 6700, 1e-9);
}

TEST(dim_schedule_disabled_only_poke_window) {
  DimParams off;
  off.enabled = false;
  DimRedrawSchedule s;
  s.OnNewFrame(0);
  CHECK(s.NextRedrawAt(10, off, 7) == kInf);
  s.Poke(100);
  s.OnRedraw(100);
  CHECK_NEAR(s.NextRedrawAt(100, off, 7), 107, 1e-9);
  s.OnRedraw(1300);
  CHECK(s.NextRedrawAt(1300, off, 7) == kInf);
}

TEST(dim_schedule_never_in_the_past) {
  DimRedrawSchedule s;
  s.OnNewFrame(0);
  s.OnRedraw(0);
  CHECK_NEAR(s.NextRedrawAt(5000, DimParams{}, 7), 5000, 1e-9);
}
