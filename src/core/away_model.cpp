#include "away_model.h"

#include <algorithm>

namespace oled {

void MotionWindow::AddSecond(int new_frames) {
  frames_[next_] = new_frames;
  next_ = (next_ + 1) % kMotionWindowS;
}

bool MotionWindow::Continuous() const {
  const auto moving = std::count_if(frames_.begin(), frames_.end(), [](int f) { return f >= kMovingSecondFrames; });
  return moving >= kContinuousSeconds;
}

void MotionWindow::Reset() {
  frames_.fill(0);
  next_ = 0;
}

bool AwayModel::Update(const AwayInputs& in) {
  const bool away = in.enabled && in.overlay_shown && !in.continuous_motion &&
                    in.idle_ms >= in.after_min * 60000.0;
  if (away == away_) return false;
  away_ = away;
  since_ms_ = away ? in.now_ms : -1;
  return true;
}

double AwayFactorAt(double now_ms, double since_ms, double target) {
  if (since_ms < 0) return 1.0;
  const double t = std::clamp((now_ms - since_ms) / kAwayRampMs, 0.0, 1.0);
  return 1.0 + (target - 1.0) * t;
}

}  // namespace oled
