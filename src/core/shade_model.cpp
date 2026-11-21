#include "shade_model.h"

#include <algorithm>

namespace oled {
namespace {

// 1 sul bordo, 0 da `spread` in poi, con raccordo smoothstep; spread 0 = lato spento.
float Edge(float distance, float spread) {
  if (spread <= 0) return 0;
  const float t = std::clamp(distance / spread, 0.0f, 1.0f);
  return 1.0f - t * t * (3.0f - 2.0f * t);
}

}  // namespace

ShadeParams ToShadeParams(const Config& config, double away_factor) {
  ShadeParams p;
  p.brightness = static_cast<float>(config.brightness_pct / 100.0 * away_factor);
  if (config.vignette_enabled) {
    p.vignette_strength = static_cast<float>(config.vignette_strength_pct / 100.0);
    p.spread[0] = static_cast<float>(config.vignette_left_pct / 100.0);
    p.spread[1] = static_cast<float>(config.vignette_right_pct / 100.0);
    p.spread[2] = static_cast<float>(config.vignette_top_pct / 100.0);
    p.spread[3] = static_cast<float>(config.vignette_bottom_pct / 100.0);
  }
  return p;
}

bool ShadeIsNeutral(const ShadeParams& p) { return p.brightness == 1.0f && p.vignette_strength == 0.0f; }

float ShadeFactor(const ShadeParams& p, float u, float v) {
  const float vignette = std::max({Edge(u, p.spread[0]), Edge(1.0f - u, p.spread[1]), Edge(v, p.spread[2]),
                                   Edge(1.0f - v, p.spread[3])});
  return p.brightness * (1.0f - p.vignette_strength * vignette);
}

}  // namespace oled
