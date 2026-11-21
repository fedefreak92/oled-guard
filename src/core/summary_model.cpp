#include "summary_model.h"

#include <algorithm>

namespace oled {

int ZoneOf(int cx, int cy, int cols, int rows) {
  const int zx = std::min(kZoneCols - 1, cx * kZoneCols / std::max(1, cols));
  const int zy = std::min(kZoneRows - 1, cy * kZoneRows / std::max(1, rows));
  return zy * kZoneCols + zx;
}

FrameSummary ComputeSummary(const DimModel& m, double now_s, const ShadeParams& shade) {
  FrameSummary s;
  const int w = m.Width(), h = m.Height();
  const double total = static_cast<double>(w) * h;
  if (total <= 0) return s;
  std::array<double, kZones> zone_px{};
  for (int cy = 0; cy < m.Rows(); ++cy) {
    for (int cx = 0; cx < m.Cols(); ++cx) {
      const int x0 = cx * kDimCellPx, x1 = std::min(w, x0 + kDimCellPx);
      const int y0 = cy * kDimCellPx, y1 = std::min(h, y0 + kDimCellPx);
      const double px = static_cast<double>(x1 - x0) * (y1 - y0);
      const double area = px / total;
      const double luma = m.MeanLuma(cx, cy);
      const double age = now_s - m.ContentChange(cx, cy);
      const bool still = age >= kStillS;
      const double level = m.Level(cx, cy);
      const float f = ShadeFactor(shade, static_cast<float>((x0 + x1) * 0.5 / w), static_cast<float>((y0 + y1) * 0.5 / h));
      const double arrived = luma * (1.0 - level) * f;
      if (still && m.Bright(cx, cy)) {
        s.risk += area;
        if (level <= kProtectedLevel) s.risk_unprotected += area;
      }
      if (age < kMovingS) s.motion += area;
      s.brightness += luma * area;
      if (still) {
        s.light_still_before += luma * area;
        s.light_still_removed += (luma - arrived) * area;
      }
      const int z = ZoneOf(cx, cy, m.Cols(), m.Rows());
      s.zone_light[z] += arrived * px;
      zone_px[z] += px;
    }
  }
  for (int z = 0; z < kZones; ++z) s.zone_light[z] = zone_px[z] > 0 ? s.zone_light[z] / zone_px[z] : 0;
  return s;
}

std::string_view MotionCaption(double motion) {
  if (motion < 0.02) return "interfaccia ferma";
  if (motion <= 0.15) return "lavoro";
  return "video o animazioni";
}

}  // namespace oled
