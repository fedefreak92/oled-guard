#include "alert_model.h"

namespace oled {

double AlertThreshold(std::string_view level) {
  if (level == "low") return 0.10;
  if (level == "high") return 0.35;
  return 0.20;
}

bool AlertModel::Update(double now_ms, std::optional<double> unprotected_risk, const AlertParams& p) {
  if (!p.enabled) {
    above_since_ms_ = -1;
    below_since_ms_ = -1;
    return false;
  }
  const bool above = unprotected_risk && *unprotected_risk >= p.threshold;
  if (above) {
    below_since_ms_ = -1;
    if (above_since_ms_ < 0) above_since_ms_ = now_ms;
  } else if (above_since_ms_ >= 0) {
    // Cali brevi non azzerano il conteggio; 2 minuti di fila si'.
    if (below_since_ms_ < 0) below_since_ms_ = now_ms;
    if (now_ms - below_since_ms_ >= kAlertResetBelowMs) {
      above_since_ms_ = -1;
      below_since_ms_ = -1;
    }
    return false;
  }
  if (!above) return false;
  const bool long_enough = now_ms - above_since_ms_ >= p.after_min * 60000.0;
  const bool rate_ok = last_fired_ms_ < 0 || now_ms - last_fired_ms_ >= kAlertMinGapMs;
  if (!long_enough || !rate_ok) return false;
  last_fired_ms_ = now_ms;
  return true;
}

double AlertModel::AboveForMs(double now_ms) const { return above_since_ms_ < 0 ? 0 : now_ms - above_since_ms_; }

}  // namespace oled
