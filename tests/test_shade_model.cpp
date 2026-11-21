#include "shade_model.h"
#include "test_framework.h"

using oled::Config;
using oled::ShadeFactor;
using oled::ShadeIsNeutral;
using oled::ShadeParams;
using oled::ToShadeParams;

TEST(shade_neutral_is_one) {
  const ShadeParams p = ToShadeParams(Config{}, 1.0);
  CHECK(ShadeIsNeutral(p));
  CHECK_NEAR(ShadeFactor(p, 0, 0), 1.0, 1e-5);
  CHECK_NEAR(ShadeFactor(p, 0.5f, 0.5f), 1.0, 1e-5);
  CHECK_NEAR(ShadeFactor(p, 1, 1), 1.0, 1e-5);
}

TEST(shade_brightness_and_away_multiply) {
  Config c;
  c.brightness_pct = 60;
  const ShadeParams p = ToShadeParams(c, 0.5);
  CHECK_NEAR(p.brightness, 0.3, 1e-5);
  CHECK(!ShadeIsNeutral(p));
  CHECK_NEAR(ShadeFactor(p, 0.5f, 0.5f), 0.3, 1e-5);
}

TEST(shade_vignette_disabled_ignores_sides) {
  Config c;
  c.vignette_enabled = false;
  c.vignette_left_pct = c.vignette_right_pct = c.vignette_top_pct = c.vignette_bottom_pct = 40;
  const ShadeParams p = ToShadeParams(c, 1.0);
  CHECK_NEAR(p.vignette_strength, 0.0, 1e-9);
  CHECK(ShadeIsNeutral(p));
}

TEST(shade_vignette_edges) {
  Config c;
  c.vignette_enabled = true;
  c.vignette_strength_pct = 50;
  c.vignette_left_pct = 20;
  c.vignette_right_pct = c.vignette_top_pct = c.vignette_bottom_pct = 0;
  const ShadeParams p = ToShadeParams(c, 1.0);
  CHECK_NEAR(p.spread[0], 0.2, 1e-6);
  CHECK_NEAR(ShadeFactor(p, 0, 0.5f), 0.5, 1e-5);
  CHECK_NEAR(ShadeFactor(p, 0.2f, 0.5f), 1.0, 1e-5);
  CHECK_NEAR(ShadeFactor(p, 0.6f, 0.5f), 1.0, 1e-5);
  const float mid = ShadeFactor(p, 0.1f, 0.5f);
  CHECK(mid > 0.5f && mid < 1.0f);
  CHECK_NEAR(ShadeFactor(p, 1, 0.5f), 1.0, 1e-5);  // lato destro spento
}

TEST(shade_vignette_corner_uses_max) {
  Config c;
  c.vignette_enabled = true;
  c.vignette_strength_pct = 50;
  c.vignette_left_pct = c.vignette_top_pct = 20;
  c.vignette_right_pct = c.vignette_bottom_pct = 0;
  const ShadeParams p = ToShadeParams(c, 1.0);
  CHECK_NEAR(ShadeFactor(p, 0, 0), 0.5, 1e-5);
}
