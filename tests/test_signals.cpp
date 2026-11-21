#include "signals.h"
#include "test_framework.h"

using oled::SignalRing;
using oled::SignalSample;

TEST(signals_ring_keeps_last_60) {
  SignalRing r;
  for (int i = 1; i <= 70; ++i) r.Push(SignalSample{true, i / 100.0, 0, 0});
  const auto s = r.Samples();
  CHECK_EQ(s.size(), size_t{60});
  CHECK_NEAR(s.front().risk, 0.11, 1e-9);
  CHECK_NEAR(r.Last().risk, 0.70, 1e-9);
}

TEST(signals_stats_skip_unmeasured) {
  SignalRing r;
  r.Push(SignalSample{true, 0.2, 0.1, 0.5});
  r.Push(SignalSample{});
  r.Push(SignalSample{true, 0.4, 0.3, 0.7});
  r.Push(SignalSample{});
  r.Push(SignalSample{});
  const auto risk = r.Risk();
  CHECK_NEAR(risk.peak, 0.4, 1e-9);
  CHECK_NEAR(risk.mean, 0.3, 1e-9);
  CHECK_EQ(risk.samples, 2);
  CHECK_NEAR(r.Motion().mean, 0.2, 1e-9);
  CHECK_NEAR(r.Brightness().peak, 0.7, 1e-9);
  CHECK(!r.Last().measured);
}

TEST(signals_empty_ring) {
  SignalRing r;
  CHECK_EQ(r.Risk().samples, 0);
  CHECK_NEAR(r.Risk().peak, 0.0, 1e-9);
  CHECK(!r.Last().measured);
  CHECK(r.Samples().empty());
}
