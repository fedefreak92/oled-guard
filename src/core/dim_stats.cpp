#include "dim_stats.h"

#include <algorithm>
#include <cstdio>

#include "dim_params.h"

namespace oled {

DimGridStats SummarizeDimGrids(const std::vector<float>& levels, const std::vector<uint32_t>& flags,
                               const std::vector<uint32_t>& classes, const std::vector<float>& last_change,
                               double now_s, double delay_s) {
  DimGridStats s;
  const size_t n = levels.size();
  if (flags.size() != n || classes.size() != n || last_change.size() != n) return s;
  s.cells = static_cast<int>(n);
  for (size_t i = 0; i < n; ++i) {
    if (flags[i] & 1u) ++s.bright;
    if (now_s - last_change[i] < kDimMotionWindowS) ++s.moving;
    if (now_s - last_change[i] >= delay_s) ++s.static_cells;
    const uint32_t cls = classes[i] & 3u;
    if (cls == 1) ++s.small_cells;
    if (cls == 2) ++s.large_cells;
    if (levels[i] > 0.01f) ++s.dimmed;
    if (now_s - last_change[i] < 2.0) ++s.recent_changes;
    s.max_level = std::max(s.max_level, static_cast<double>(levels[i]));
  }
  return s;
}

std::string FormatDimGridStats(const DimGridStats& s) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "celle %d: luminose %d, in movimento %d, ferme %d, elementi %d, da non toccare %d, scurite %d, "
                "max %.0f%%, cambiate da 2 s %d",
                s.cells, s.bright, s.moving, s.static_cells, s.small_cells, s.large_cells, s.dimmed, s.max_level * 100,
                s.recent_changes);
  return buf;
}

}  // namespace oled
