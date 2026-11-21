#include "dim_model.h"

#include <algorithm>
#include <cstring>

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

std::vector<double> ZoneWindowMean(const std::vector<double>& grid, const std::vector<uint8_t>& zones, int cols,
                                   int rows, int radius) {
  const auto at = [cols](int x, int y) { return static_cast<size_t>(y) * cols + x; };
  std::vector<double> hsum(grid.size()), hcount(grid.size());
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < cols; ++x) {
      const uint8_t z = zones[at(x, y)];
      double s = 0, n = 0;
      for (int i = std::max(0, x - radius); i <= std::min(cols - 1, x + radius); ++i) {
        if (zones[at(i, y)] != z) continue;
        s += grid[at(i, y)];
        n += 1;
      }
      hsum[at(x, y)] = s;
      hcount[at(x, y)] = n;
    }
  }
  std::vector<double> out(grid.size());
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < cols; ++x) {
      const uint8_t z = zones[at(x, y)];
      double s = 0, n = 0;
      for (int j = std::max(0, y - radius); j <= std::min(rows - 1, y + radius); ++j) {
        if (zones[at(x, j)] != z) continue;
        s += hsum[at(x, j)];
        n += hcount[at(x, j)];
      }
      out[at(x, y)] = s / n;  // n >= 1: la cella centrale conta sempre
    }
  }
  return out;
}

DimModel::DimModel(int width, int height, double now_s, const DimSavedState* restore)
    : width_(width),
      height_(height),
      cols_((width + kDimCellPx - 1) / kDimCellPx),
      rows_((height + kDimCellPx - 1) / kDimCellPx) {
  const size_t n = static_cast<size_t>(cols_) * rows_;
  last_change_.assign(n, now_s);
  content_change_.assign(n, now_s);
  mean_luma_.assign(n, 0.0f);
  hashes_.assign(n, 0);
  anchor_hash_.assign(n, 0);
  anchor_time_.assign(n, now_s);
  excursion_.assign(n, -1.0);
  zones_.assign(n, kDimZoneApp);
  bright_.assign(n, 0);
  large_.assign(n, 0);
  class_.assign(n, 0);
  targets_.assign(n, 0.0);
  levels_.assign(n, 0.0f);
  if (restore && restore->width == width && restore->height == height && restore->ages.size() == n &&
      restore->hashes.size() == n) {
    restore_ = *restore;
  }
}

void DimModel::SetZones(const std::vector<uint8_t>& zones) {
  if (zones.size() == zones_.size()) zones_ = zones;
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
      double luma_sum = 0;
      uint32_t hash = 2166136261u;  // FNV-1a sui bit dei valori
      for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
          const size_t i = static_cast<size_t>(y) * width_ + x;
          const float v = frame.luma[i];
          if (!first && v != previous_[i]) changed = true;
          if (v >= bright_threshold_) ++bright_pixels;
          luma_sum += v;
          uint32_t bits = 0;
          std::memcpy(&bits, &v, sizeof(bits));
          hash = (hash ^ bits) * 16777619u;
        }
      }
      const int pixels = (x1 - x0) * (y1 - y0);
      const size_t c = Index(cx, cy);
      if (first) {
        // Ripresa dopo una pausa: stessa firma, stessa eta' di prima.
        const bool same = !restore_.ages.empty() && restore_.hashes[c] == hash;
        last_change_[c] = same ? now_s - restore_.ages[c] : now_s;
        anchor_hash_[c] = hash;
        anchor_time_[c] = last_change_[c];
        excursion_[c] = -1;
      } else if (hash == anchor_hash_[c]) {
        if (excursion_[c] >= 0) {
          // Tornata com'era: dopo una deviazione breve riprende l'eta', altrimenti e' nuova.
          const bool brief = now_s - excursion_[c] <= kDimBlipS;
          last_change_[c] = brief ? anchor_time_[c] : now_s;
          if (!brief) anchor_time_[c] = now_s;
          excursion_[c] = -1;
        }
      } else if (changed) {
        last_change_[c] = now_s;
        if (excursion_[c] < 0) {
          excursion_[c] = now_s;
        } else if (now_s - excursion_[c] > kDimBlipS) {
          anchor_hash_[c] = hash;  // contenuto nuovo da troppo tempo: diventa il riferimento
          anchor_time_[c] = now_s;
          excursion_[c] = -1;
        }
      }
      // Cambio reale: al primo frame come l'eta' (anche dopo una ripresa), poi a ogni
      // pixel diverso, senza la regola delle deviazioni brevi.
      if (first) content_change_[c] = last_change_[c];
      else if (changed) content_change_[c] = now_s;
      mean_luma_[c] = static_cast<float>(luma_sum / pixels);
      hashes_[c] = hash;
      bright_[c] = bright_pixels >= kDimBrightPixelFraction * pixels ? 1 : 0;
    }
  }
  restore_ = DimSavedState{};
  previous_ = frame.luma;
}

DimSavedState DimModel::Save(double now_s) const {
  // Nessun frame ancora: lo stato da riprendere e' ancora quello ricevuto.
  if (!restore_.ages.empty()) return restore_;
  DimSavedState s;
  s.width = width_;
  s.height = height_;
  s.hashes.resize(hashes_.size());
  s.ages.resize(last_change_.size());
  for (size_t i = 0; i < last_change_.size(); ++i) {
    // Durante una deviazione breve vale il contenuto di riferimento (es. icona sotto il mouse).
    const bool brief = excursion_[i] >= 0 && now_s - excursion_[i] <= kDimBlipS;
    s.hashes[i] = brief ? anchor_hash_[i] : hashes_[i];
    s.ages[i] = static_cast<float>(now_s - (brief ? anchor_time_[i] : last_change_[i]));
  }
  return s;
}

void DimModel::Update(double now_s, double dt_s, const DimParams& p, bool suspended) {
  bright_threshold_ = p.bright_threshold;
  const size_t n = levels_.size();
  // Finestre attive: "cambiate adesso" (ritorno rapido, conteggio che riparte).
  for (size_t i = 0; i < n; ++i) {
    const uint8_t z = zones_[i];
    if (z == kDimZoneShell || std::find(active_.begin(), active_.end(), z) == active_.end()) continue;
    last_change_[i] = now_s;
    anchor_time_[i] = now_s;
    excursion_[i] = -1;
  }
  // Movimento intorno: quota di celle cambiate da poco nella finestra 33x33.
  std::vector<double> motion(n);
  for (size_t i = 0; i < n; ++i) motion[i] = now_s - last_change_[i] < kDimMotionWindowS ? 1.0 : 0.0;
  const auto motion_density = ZoneWindowMean(motion, zones_, cols_, rows_, kDimWindowRadius);
  const double dt = std::max(0.0, dt_s);

  for (size_t i = 0; i < n; ++i) {
    const double age = now_s - last_change_[i];
    const bool m = bright_[i] && age >= p.delay_s;
    // Desktop e barra: ogni elemento fermo e luminoso. Dentro un'app: solo con movimento intorno.
    const bool large =
        zones_[i] != kDimZoneShell && !p.windows_like_desktop && motion_density[i] < p.motion_fraction;
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
