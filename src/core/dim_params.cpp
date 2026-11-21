#include "dim_params.h"

namespace oled {

DimParams ToDimParams(const Config& c) {
  DimParams p;
  p.enabled = c.dim_enabled;
  p.strength = c.dim_strength_pct / 100.0;
  p.delay_s = c.dim_delay_s;
  p.ramp_s = c.dim_ramp_s;
  p.recover_s = c.dim_recover_ms / 1000.0;
  p.large_enabled = c.dim_large_enabled;
  p.large_delay_s = c.dim_large_delay_s;
  p.large_strength = c.dim_large_strength_pct / 100.0;
  p.bright_threshold = c.dim_bright_threshold_pct / 100.0;
  p.motion_fraction = c.dim_motion_fraction_pct / 100.0;
  p.windows_like_desktop = c.dim_window_mode == "desktop";
  return p;
}

}  // namespace oled
