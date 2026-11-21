#include "watchdog.h"

namespace oled {

WatchdogVerdict CheckWatchdog(int64_t now_ms, int64_t last_check_ms, int64_t main_beat_ms, int64_t render_beat_ms,
                              bool overlay_active, int64_t stall_limit_ms, int64_t check_interval_ms) {
  // Se lo stesso watchdog e' rimasto fermo molto piu' del previsto, il sistema era
  // sospeso: nessun thread poteva battere, non e' un blocco.
  if (now_ms - last_check_ms > 4 * check_interval_ms) return WatchdogVerdict::ClockJump;
  if (!overlay_active) return WatchdogVerdict::Ok;
  if (now_ms - main_beat_ms > stall_limit_ms || now_ms - render_beat_ms > stall_limit_ms) {
    return WatchdogVerdict::Stalled;
  }
  return WatchdogVerdict::Ok;
}

}  // namespace oled
