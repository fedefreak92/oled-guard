#include "dim_schedule.h"

#include <algorithm>

namespace oled {

double DimRedrawSchedule::NextRedrawAt(double now_ms, const DimParams& p, double frame_period_ms) const {
  double next = std::numeric_limits<double>::infinity();
  // Finestra veloce: un ridisegno per refresh durante i ritorni rapidi.
  const bool fast_after_frame = p.enabled && now_ms <= last_frame_ms_ + p.recover_s * 1000 + 100;
  const bool fast_after_poke = now_ms <= last_poke_ms_ + kDimCommandRecoverS * 1000 + 100;
  if (fast_after_frame || fast_after_poke) next = std::min(next, last_redraw_ms_ + frame_period_ms);
  // Finestra lenta: finche' una discesa (o un ritorno lento) puo' essere in corso.
  if (p.enabled) {
    const double start = std::max(p.delay_s, p.large_enabled ? p.large_delay_s : 0.0);
    const double bound = std::max(last_frame_ms_, last_poke_ms_) + (start + p.ramp_s + kDimLargeRecoverS + 3) * 1000;
    if (now_ms <= bound) next = std::min(next, last_redraw_ms_ + kDimSlowRedrawMs);
  }
  return next == std::numeric_limits<double>::infinity() ? next : std::max(next, now_ms);
}

}  // namespace oled
