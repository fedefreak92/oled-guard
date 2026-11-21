#include "test_framework.h"
#include "watchdog.h"

using oled::CheckWatchdog;
using oled::WatchdogVerdict;

namespace {
constexpr int64_t kLimit = 3000;
constexpr int64_t kInterval = 500;
}  // namespace

TEST(watchdog_ok_with_fresh_beats) {
  CHECK(CheckWatchdog(10000, 9500, 9900, 9950, true, kLimit, kInterval) == WatchdogVerdict::Ok);
}

TEST(watchdog_detects_stalled_main_or_render) {
  CHECK(CheckWatchdog(10000, 9500, 6000, 9950, true, kLimit, kInterval) == WatchdogVerdict::Stalled);
  CHECK(CheckWatchdog(10000, 9500, 9950, 6000, true, kLimit, kInterval) == WatchdogVerdict::Stalled);
}

TEST(watchdog_ignores_stalls_when_overlay_inactive) {
  CHECK(CheckWatchdog(10000, 9500, 0, 0, false, kLimit, kInterval) == WatchdogVerdict::Ok);
}

TEST(watchdog_sleep_resume_is_clock_jump_not_stall) {
  // Il PC si e' sospeso per 10 minuti: anche il watchdog non girava, quindi i battiti
  // vecchi non indicano un blocco.
  CHECK(CheckWatchdog(610000, 10000, 9900, 9950, true, kLimit, kInterval) == WatchdogVerdict::ClockJump);
}

TEST(watchdog_short_delay_is_not_a_jump) {
  CHECK(CheckWatchdog(10000, 9000, 9900, 9950, true, kLimit, kInterval) == WatchdogVerdict::Ok);
}
