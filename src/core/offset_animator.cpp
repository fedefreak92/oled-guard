#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "offset.h"

namespace oled {
namespace {

double Smoothstep(double u) { return u * u * (3.0 - 2.0 * u); }

}  // namespace

OffsetAnimator::OffsetAnimator(Offset from, Offset to, double duration_ms)
    : from_(from),
      to_(to),
      duration_ms_(std::max(0.0, duration_ms)),
      steps_(std::max(std::abs(to.x - from.x), std::abs(to.y - from.y))) {}

int OffsetAnimator::IndexAt(double t_ms) const {
  if (steps_ == 0) return 0;
  if (duration_ms_ <= 0.0) return steps_;
  const double u = std::clamp(t_ms / duration_ms_, 0.0, 1.0);
  const int index = static_cast<int>(std::floor(Smoothstep(u) * steps_ + 0.5));
  return std::min(index, steps_);
}

Offset OffsetAnimator::PointAt(int index) const {
  if (steps_ == 0) return to_;
  // Linea discreta: ogni passo cambia ciascun asse di al massimo 1 px.
  const double k = static_cast<double>(index) / steps_;
  return Offset{from_.x + static_cast<int>(std::lround(k * (to_.x - from_.x))),
                from_.y + static_cast<int>(std::lround(k * (to_.y - from_.y)))};
}

Offset OffsetAnimator::At(double t_ms) const { return PointAt(IndexAt(t_ms)); }

std::optional<double> OffsetAnimator::NextStepAt(double t_ms) const {
  const int index = IndexAt(t_ms);
  if (index >= steps_) return std::nullopt;

  // Il passo index+1 scatta quando smoothstep(u) * n + 0.5 >= index + 1.
  const double threshold = (index + 0.5) / steps_;
  double lo = 0.0;
  double hi = 1.0;
  for (int i = 0; i < 60; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (Smoothstep(mid) >= threshold) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  double candidate = std::ceil(hi * duration_ms_);
  // Correzione per gli arrotondamenti in virgola mobile.
  while (IndexAt(candidate) <= index) candidate += 1.0;
  while (candidate - 1.0 > t_ms && IndexAt(candidate - 1.0) > index) candidate -= 1.0;
  return candidate;
}

}  // namespace oled
