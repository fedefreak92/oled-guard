#include "dim_model.h"

#include <algorithm>

namespace oled {

std::vector<double> WindowMean(const std::vector<double>& grid, int cols, int rows, int radius) {
  // Passo orizzontale: somma e numero di celle presenti; poi verticale sulle due.
  std::vector<double> hsum(grid.size()), hcount(grid.size());
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < cols; ++x) {
      const int x0 = std::max(0, x - radius), x1 = std::min(cols - 1, x + radius);
      double s = 0;
      for (int i = x0; i <= x1; ++i) s += grid[static_cast<size_t>(y) * cols + i];
      hsum[static_cast<size_t>(y) * cols + x] = s;
      hcount[static_cast<size_t>(y) * cols + x] = x1 - x0 + 1;
    }
  }
  std::vector<double> out(grid.size());
  for (int y = 0; y < rows; ++y) {
    const int y0 = std::max(0, y - radius), y1 = std::min(rows - 1, y + radius);
    for (int x = 0; x < cols; ++x) {
      double s = 0, n = 0;
      for (int j = y0; j <= y1; ++j) {
        s += hsum[static_cast<size_t>(j) * cols + x];
        n += hcount[static_cast<size_t>(j) * cols + x];
      }
      out[static_cast<size_t>(y) * cols + x] = s / n;
    }
  }
  return out;
}

std::vector<double> WindowMax(const std::vector<double>& grid, int cols, int rows, int radius) {
  std::vector<double> h(grid.size());
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < cols; ++x) {
      const int x0 = std::max(0, x - radius), x1 = std::min(cols - 1, x + radius);
      double m = grid[static_cast<size_t>(y) * cols + x0];
      for (int i = x0 + 1; i <= x1; ++i) m = std::max(m, grid[static_cast<size_t>(y) * cols + i]);
      h[static_cast<size_t>(y) * cols + x] = m;
    }
  }
  std::vector<double> out(grid.size());
  for (int y = 0; y < rows; ++y) {
    const int y0 = std::max(0, y - radius), y1 = std::min(rows - 1, y + radius);
    for (int x = 0; x < cols; ++x) {
      double m = h[static_cast<size_t>(y0) * cols + x];
      for (int j = y0 + 1; j <= y1; ++j) m = std::max(m, h[static_cast<size_t>(j) * cols + x]);
      out[static_cast<size_t>(y) * cols + x] = m;
    }
  }
  return out;
}

DimModel::DimModel(int width, int height, double now_s)
    : width_(width),
      height_(height),
      cols_((width + kDimCellPx - 1) / kDimCellPx),
      rows_((height + kDimCellPx - 1) / kDimCellPx) {
  const size_t n = static_cast<size_t>(cols_) * rows_;
  last_change_.assign(n, now_s);
  bright_.assign(n, 0);
  content_.assign(n, 0);
  large_.assign(n, 0);
  class_.assign(n, 0);
  targets_.assign(n, 0.0);
  levels_.assign(n, 0.0f);
}

void DimModel::OnFrame(const LumaImage& frame, double now_s) {
  if (frame.width != width_ || frame.height != height_) return;
  const bool first = previous_.empty();
  for (int cy = 0; cy < rows_; ++cy) {
    for (int cx = 0; cx < cols_; ++cx) {
      const int x0 = cx * kDimCellPx, x1 = std::min(width_, x0 + kDimCellPx);
      const int y0 = cy * kDimCellPx, y1 = std::min(height_, y0 + kDimCellPx);
      bool changed = first;
      int bright_pixels = 0;
      float lo = frame.luma[static_cast<size_t>(y0) * width_ + x0];
      float hi = lo;
      for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
          const size_t i = static_cast<size_t>(y) * width_ + x;
          const float v = frame.luma[i];
          if (!first && v != previous_[i]) changed = true;
          if (v >= bright_threshold_) ++bright_pixels;
          lo = std::min(lo, v);
          hi = std::max(hi, v);
        }
      }
      const int pixels = (x1 - x0) * (y1 - y0);
      const size_t c = Index(cx, cy);
      if (changed) last_change_[c] = now_s;
      bright_[c] = bright_pixels >= kDimBrightPixelFraction * pixels ? 1 : 0;
      content_[c] = bright_[c] || hi - lo >= kDimDetailRange ? 1 : 0;
    }
  }
  previous_ = frame.luma;
}

void DimModel::Update(double now_s, double dt_s, const DimParams& p, bool suspended) {
  bright_threshold_ = p.bright_threshold;
  const size_t n = levels_.size();
  std::vector<double> c_mask(n);
  for (size_t i = 0; i < n; ++i) c_mask[i] = content_[i] && now_s - last_change_[i] >= p.delay_s ? 1.0 : 0.0;
  const auto large_max = WindowMax(WindowMean(c_mask, cols_, rows_, kDimWindowRadius), cols_, rows_, kDimWindowRadius);
  const double dt = std::max(0.0, dt_s);

  for (size_t i = 0; i < n; ++i) {
    const double age = now_s - last_change_[i];
    const bool m = bright_[i] && age >= p.delay_s;
    const bool large = large_max[i] >= p.large_fraction;
    large_[i] = large ? 1 : 0;

    double cap = 0;
    if (p.enabled && !suspended && m) {
      if (!large) cap = p.strength;
      else if (p.large_enabled) cap = p.large_strength;
    }
    const double start = large ? p.large_delay_s : p.delay_s;
    const double target = cap * std::clamp((age - start) / p.ramp_s, 0.0, 1.0);
    targets_[i] = target;

    // Se cambia solo la classe il ritorno e' lento (invisibile); se la classe resta la
    // stessa e il livello supera il massimo consentito e' un comando (ritorno in 1 s).
    const uint8_t cls = !m ? 0 : (large ? 2 : 1);
    double level = levels_[i];
    bool slow = (class_[i] & 4) != 0;
    if (cls != (class_[i] & 3) && level > target) slow = true;
    if (level < target) {
      level = std::min(target, level + cap / p.ramp_s * dt);
    } else if (level > target) {
      double rate = p.strength / kDimLargeRecoverS;  // da strength a 0 in 30 s
      if (age < p.delay_s || !bright_[i]) rate = std::max(rate, kDimMaxLevel / p.recover_s);
      if (suspended || !p.enabled || (cls != 0 && !slow && level > cap)) {
        rate = std::max(rate, kDimMaxLevel / kDimCommandRecoverS);
      }
      level = std::max(target, level - rate * dt);
    }
    if (level <= target) slow = false;
    class_[i] = static_cast<uint8_t>(cls | (slow ? 4 : 0));
    levels_[i] = static_cast<float>(level);
  }
}

}  // namespace oled
