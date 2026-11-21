// Decisione del watchdog: thread bloccato o semplice salto dell'orologio (sospensione).
#pragma once

#include <cstdint>

namespace oled {

enum class WatchdogVerdict {
  Ok,
  Stalled,    // un thread non batte da troppo: chiudere l'app
  ClockJump,  // anche il watchdog era fermo (sospensione/ibernazione): azzerare i battiti
};

WatchdogVerdict CheckWatchdog(int64_t now_ms, int64_t last_check_ms, int64_t main_beat_ms, int64_t render_beat_ms,
                              bool overlay_active, int64_t stall_limit_ms, int64_t check_interval_ms);

}  // namespace oled
