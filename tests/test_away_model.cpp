#include "away_model.h"
#include "test_framework.h"

using oled::AwayFactorAt;
using oled::AwayInputs;
using oled::AwayModel;
using oled::MotionWindow;

namespace {
AwayInputs Idle(double now_ms, double idle_ms) {
  AwayInputs in;
  in.now_ms = now_ms;
  in.idle_ms = idle_ms;
  in.overlay_shown = true;
  in.enabled = true;
  in.after_min = 10;
  return in;
}
}  // namespace

TEST(motion_window_video_is_continuous) {
  MotionWindow w;
  for (int i = 0; i < 60; ++i) w.AddSecond(24);
  CHECK(w.Continuous());
}

TEST(motion_window_caret_is_not) {
  MotionWindow w;
  for (int i = 0; i < 60; ++i) w.AddSecond(2);
  CHECK(!w.Continuous());
  MotionWindow a;
  for (int i = 0; i < 19; ++i) a.AddSecond(30);
  for (int i = 0; i < 41; ++i) a.AddSecond(0);
  CHECK(!a.Continuous());
  MotionWindow b;
  for (int i = 0; i < 20; ++i) b.AddSecond(30);
  CHECK(b.Continuous());
  b.Reset();
  CHECK(!b.Continuous());
}

TEST(motion_window_forgets_old_seconds) {
  MotionWindow w;
  for (int i = 0; i < 60; ++i) w.AddSecond(30);
  for (int i = 0; i < 41; ++i) w.AddSecond(0);
  CHECK(!w.Continuous());
}

TEST(away_never_with_continuous_motion) {
  AwayModel m;
  for (int s = 0; s <= 30 * 60; ++s) {
    AwayInputs in = Idle(s * 1000.0, s * 1000.0);
    in.continuous_motion = true;
    m.Update(in);
    CHECK(!m.Away());
  }
}

TEST(away_enters_after_idle) {
  AwayModel m;
  CHECK(!m.Update(Idle(1000, 599999)));
  CHECK(!m.Away());
  CHECK(m.Update(Idle(2000, 600000)));
  CHECK(m.Away());
  CHECK_NEAR(m.SinceMs(), 2000.0, 1e-9);
  CHECK(!m.Update(Idle(3000, 601000)));  // nessun cambio
  CHECK_NEAR(m.SinceMs(), 2000.0, 1e-9);
}

TEST(away_input_restores_immediately) {
  AwayModel m;
  m.Update(Idle(1000, 700000));
  CHECK(m.Away());
  CHECK(m.Update(Idle(5000, 500)));
  CHECK(!m.Away());
}

TEST(away_off_when_disabled_or_hidden) {
  AwayModel m;
  AwayInputs off = Idle(1000, 900000);
  off.enabled = false;
  m.Update(off);
  CHECK(!m.Away());
  AwayInputs hidden = Idle(1000, 900000);
  hidden.overlay_shown = false;
  m.Update(hidden);
  CHECK(!m.Away());
  m.Update(Idle(2000, 900000));
  CHECK(m.Away());
  CHECK(m.Update(hidden));
  CHECK(!m.Away());
}

TEST(away_factor_ramp) {
  const double since = 50000;
  CHECK_NEAR(AwayFactorAt(since, since, 0.3), 1.0, 1e-9);
  CHECK_NEAR(AwayFactorAt(since + 5000, since, 0.3), 0.65, 1e-9);
  CHECK_NEAR(AwayFactorAt(since + 10000, since, 0.3), 0.3, 1e-9);
  CHECK_NEAR(AwayFactorAt(since + 99999, since, 0.3), 0.3, 1e-9);
  CHECK_NEAR(AwayFactorAt(since + 5000, -1, 0.3), 1.0, 1e-9);
}
