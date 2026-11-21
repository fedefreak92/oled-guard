#include <optional>

#include "alert_model.h"
#include "test_framework.h"

using oled::AlertModel;
using oled::AlertParams;

namespace {
AlertParams Params(int after_min) {
  AlertParams p;
  p.threshold = 0.20;
  p.after_min = after_min;
  return p;
}
}  // namespace

TEST(alert_thresholds) {
  CHECK_NEAR(oled::AlertThreshold("low"), 0.10, 1e-12);
  CHECK_NEAR(oled::AlertThreshold("medium"), 0.20, 1e-12);
  CHECK_NEAR(oled::AlertThreshold("high"), 0.35, 1e-12);
  CHECK_NEAR(oled::AlertThreshold("boh"), 0.20, 1e-12);
}

TEST(alert_fires_after_sustained_risk) {
  AlertModel a;
  int fired_at = -1;
  for (int s = 0; s <= 3600 && fired_at < 0; ++s) {
    if (a.Update(s * 1000.0, 0.25, Params(60))) fired_at = s;
  }
  CHECK_EQ(fired_at, 3600);
}

TEST(alert_short_dips_do_not_reset) {
  AlertModel a;
  int fired_at = -1;
  for (int s = 0; s <= 4000 && fired_at < 0; ++s) {
    const bool dip = s % 600 >= 540;  // 60 s sotto soglia ogni 10 minuti
    if (a.Update(s * 1000.0, dip ? 0.05 : 0.25, Params(60))) fired_at = s;
  }
  CHECK_EQ(fired_at, 3600);
  AlertModel b;
  for (int s = 0; s < 1800; ++s) CHECK(!b.Update(s * 1000.0, 0.25, Params(60)));
  for (int s = 1800; s < 1921; ++s) b.Update(s * 1000.0, 0.05, Params(60));  // 121 s sotto
  fired_at = -1;
  for (int s = 1921; s <= 6000 && fired_at < 0; ++s) {
    if (b.Update(s * 1000.0, 0.25, Params(60))) fired_at = s;
  }
  CHECK_EQ(fired_at, 1921 + 3600);
}

TEST(alert_ignores_when_below_and_limits_rate) {
  AlertModel a;
  for (int s = 0; s < 5 * 3600; ++s) CHECK(!a.Update(s * 1000.0, 0.05, Params(60)));
  AlertModel b;
  int fires = 0, first = -1, second = -1;
  for (int s = 0; s <= 7300; ++s) {
    if (b.Update(s * 1000.0, 0.5, Params(5))) {
      ++fires;
      (first < 0 ? first : second) = s;
    }
  }
  CHECK_EQ(first, 300);
  CHECK_EQ(second, 300 + 3600);
  CHECK_EQ(fires, 2);
}

TEST(alert_disabled_and_unmeasured) {
  AlertModel a;
  AlertParams off = Params(5);
  off.enabled = false;
  for (int s = 0; s < 1000; ++s) CHECK(!a.Update(s * 1000.0, 0.9, off));
  AlertModel b;
  for (int s = 0; s < 200; ++s) b.Update(s * 1000.0, 0.9, Params(5));
  for (int s = 200; s < 380; ++s) b.Update(s * 1000.0, std::nullopt, Params(5));  // 3 minuti in pausa
  CHECK_NEAR(b.AboveForMs(380000), 0.0, 1e-9);
}
