#include "startup.h"

namespace oled {

StartupDecision DecideStartup(bool config_exists, bool ui_available) {
  StartupDecision d;
  if (config_exists) {
    d.start_protection = true;
  } else if (ui_available) {
    d.setup_pending = true;
  } else {
    d.start_protection = true;
    d.notify_auto_choice = true;
  }
  return d;
}

StartupDecision AfterUiFailure(const StartupDecision& current) {
  if (!current.setup_pending) return current;
  return DecideStartup(/*config_exists=*/false, /*ui_available=*/false);
}

bool WarnSetupUnfinished(bool setup_pending, bool quitting) { return setup_pending && !quitting; }

SecondInstanceAction OnSecondInstance(bool setup_pending) {
  return setup_pending ? SecondInstanceAction::OpenSetup : SecondInstanceAction::ShowRunningNotice;
}

bool MissingMonitorNotifier::Update(bool missing, double uptime_ms, double now_ms) {
  if (!missing) {
    missing_ = false;
    seen_ = true;
    return false;
  }
  if (seen_) return false;
  if (!missing_) {
    missing_ = true;
    since_ms_ = now_ms;
  }
  if (notified_ || now_ms - since_ms_ < kMissingMs || uptime_ms < kUptimeMs) return false;
  notified_ = true;
  return true;
}

}  // namespace oled
