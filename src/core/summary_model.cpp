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

FrameSummary DecodeSummary(const std::vector<uint32_t>& words, double total_px) {
  FrameSummary s;
  if (words.size() != static_cast<size_t>(kSummaryWords) || total_px <= 0) return s;
  std::array<double, kSummaryTotals> t{};
  for (int slot = 0; slot < kSummarySlots; ++slot) {
    for (int f = 0; f < kSummaryTotals; ++f) t[f] += words[static_cast<size_t>(slot) * kSummaryTotals + f];
  }
  s.risk = t[kSumRiskPx] / total_px;
  s.risk_unprotected = t[kSumUnprotectedPx] / total_px;
  s.motion = t[kSumMotionPx] / total_px;
  s.brightness = t[kSumLuma] / kSummaryScale / total_px;
  s.light_still_before = t[kSumStillBefore] / kSummaryScale / total_px;
  s.light_still_removed = t[kSumStillRemoved] / kSummaryScale / total_px;
  for (int z = 0; z < kZones; ++z) {
    const double light = words[static_cast<size_t>(kSummaryZoneBase) + z * 2];
    const double px = words[static_cast<size_t>(kSummaryZoneBase) + z * 2 + 1];
    s.zone_light[z] = px > 0 ? light / kSummaryScale / px : 0;
  }
  return s;
}

std::string_view MotionCaptionKey(double motion) {
  if (motion < 0.02) return "signals.caption.still";
  if (motion <= 0.15) return "signals.caption.work";
  return "signals.caption.video";
}

}  // namespace oled
