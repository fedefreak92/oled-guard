#include "shared_signals.h"

namespace oled {

void SharedSignals::Publish(const FrameSummary& s, double measured_ms) {
  std::lock_guard lock(mutex_);
  latest_ = s;
  measured_ms_ = measured_ms;
}

std::optional<FrameSummary> SharedSignals::Latest(double now_ms, double max_age_ms) {
  std::lock_guard lock(mutex_);
  if (!latest_ || now_ms - measured_ms_ > max_age_ms) return std::nullopt;
  return latest_;
}

}  // namespace oled
