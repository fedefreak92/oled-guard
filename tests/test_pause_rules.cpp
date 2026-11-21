#include "pause_rules.h"
#include "test_framework.h"

using namespace oled;

TEST(nothing_means_active) { CHECK(EvaluatePause({}, {}) == PauseReason::None); }

TEST(manual_wins_over_allowlist) {
  PauseInputs in;
  in.manual = true;
  in.foreground_process = "code.exe";
  PauseRulesConfig c;
  c.allowlist = {"code.exe"};
  CHECK(EvaluatePause(in, c) == PauseReason::Manual);
}

TEST(lock_wins_over_allowlist) {
  PauseInputs in;
  in.session_locked = true;
  in.foreground_process = "code.exe";
  PauseRulesConfig c;
  c.allowlist = {"code.exe"};
  CHECK(EvaluatePause(in, c) == PauseReason::SessionLocked);
}

TEST(session_and_secure_desktop_pause) {
  PauseInputs a;
  a.session_inactive = true;
  CHECK(EvaluatePause(a, {}) == PauseReason::SessionInactive);
  PauseInputs b;
  b.secure_desktop = true;
  CHECK(EvaluatePause(b, {}) == PauseReason::SecureDesktop);
}

TEST(capture_problems_pause) {
  PauseInputs a;
  a.capture_failed = true;
  CHECK(EvaluatePause(a, {}) == PauseReason::CaptureFailed);
  PauseInputs b;
  b.display_rotated = true;
  CHECK(EvaluatePause(b, {}) == PauseReason::DisplayRotated);
  PauseInputs c;
  c.monitor_missing = true;
  CHECK(EvaluatePause(c, {}) == PauseReason::MonitorMissing);
}

TEST(allowlist_overrides_fullscreen_and_d3d) {
  PauseInputs in;
  in.foreground_process = "mpv.exe";
  in.foreground_covers_monitor = true;
  in.notification_state = NotificationState::RunningD3DFullScreen;
  PauseRulesConfig c;
  c.allowlist = {"mpv.exe"};
  CHECK(EvaluatePause(in, c) == PauseReason::None);
}

TEST(deny_is_case_insensitive) {
  PauseInputs in;
  in.foreground_process = "Game.EXE";
  PauseRulesConfig c;
  c.denylist = {"game.exe"};
  CHECK(EvaluatePause(in, c) == PauseReason::Denylisted);
}

TEST(allow_is_case_insensitive) {
  PauseInputs in;
  in.foreground_process = "mpv.exe";
  in.foreground_covers_monitor = true;
  PauseRulesConfig c;
  c.allowlist = {"MPV.exe"};
  CHECK(EvaluatePause(in, c) == PauseReason::None);
}

TEST(notification_states_that_pause) {
  for (auto s : {NotificationState::NotPresent, NotificationState::RunningD3DFullScreen,
                 NotificationState::PresentationMode}) {
    PauseInputs in;
    in.notification_state = s;
    CHECK(EvaluatePause(in, {}) == PauseReason::NotificationState);
  }
  for (auto s : {NotificationState::QuietTime, NotificationState::App,
                 NotificationState::AcceptsNotifications}) {
    PauseInputs in;
    in.notification_state = s;
    CHECK(EvaluatePause(in, {}) == PauseReason::None);
  }
}

TEST(busy_alone_does_not_pause) {
  // L'overlay stesso (topmost, grande quanto il monitor) fa riportare QUNS_BUSY a
  // Windows: da solo non deve mettere in pausa, altrimenti l'app si spegne da sola.
  PauseInputs in;
  in.notification_state = NotificationState::Busy;
  in.foreground_process = "claude.exe";
  CHECK(EvaluatePause(in, {}) == PauseReason::None);
}

TEST(busy_with_fullscreen_window_pauses_via_fullscreen_rule) {
  PauseInputs in;
  in.notification_state = NotificationState::Busy;
  in.foreground_covers_monitor = true;
  CHECK(EvaluatePause(in, {}) == PauseReason::FullscreenWindow);
  PauseRulesConfig c;
  c.pause_on_fullscreen_window = false;  // l'utente vuole lo shift anche in borderless
  CHECK(EvaluatePause(in, c) == PauseReason::None);
}

TEST(notification_rule_can_be_disabled) {
  PauseInputs in;
  in.notification_state = NotificationState::RunningD3DFullScreen;
  PauseRulesConfig c;
  c.pause_on_notification_state = false;
  CHECK(EvaluatePause(in, c) == PauseReason::None);
}

TEST(fullscreen_rule_and_option) {
  PauseInputs in;
  in.foreground_covers_monitor = true;
  CHECK(EvaluatePause(in, {}) == PauseReason::FullscreenWindow);
  PauseRulesConfig c;
  c.pause_on_fullscreen_window = false;
  CHECK(EvaluatePause(in, c) == PauseReason::None);
}

namespace {
const ScreenRect kMonitor{0, 0, 3840, 2160};
}

TEST(fullscreen_borderless_window_counts) {
  CHECK(IsFullscreenWindow({0, 0, 3840, 2160}, kMonitor, false, false));
  CHECK(IsFullscreenWindow({-8, -8, 3848, 2168}, kMonitor, false, false));
}

TEST(maximized_window_with_caption_is_not_fullscreen) {
  // Barra delle applicazioni a scomparsa: una finestra massimizzata copre tutto il
  // monitor ma resta una finestra normale.
  CHECK(!IsFullscreenWindow({0, 0, 3840, 2160}, kMonitor, true, true));
}

TEST(maximized_window_without_caption_counts) {
  CHECK(IsFullscreenWindow({0, 0, 3840, 2160}, kMonitor, true, false));
}

TEST(window_not_covering_monitor_is_not_fullscreen) {
  CHECK(!IsFullscreenWindow({0, 0, 3840, 2100}, kMonitor, false, false));
  CHECK(!IsFullscreenWindow({100, 0, 3840, 2160}, kMonitor, false, false));
}

TEST(immediate_reasons) {
  for (auto r : {PauseReason::Manual, PauseReason::SessionLocked, PauseReason::SessionInactive,
                 PauseReason::SecureDesktop, PauseReason::CaptureFailed, PauseReason::DisplayRotated,
                 PauseReason::MonitorMissing})
    CHECK(IsImmediate(r));
  for (auto r : {PauseReason::None, PauseReason::Denylisted, PauseReason::NotificationState,
                 PauseReason::FullscreenWindow})
    CHECK(!IsImmediate(r));
}

TEST(reason_texts_are_italian) {
  CHECK(PauseReasonText(PauseReason::Manual) == "Pausa manuale");
  CHECK(PauseReasonText(PauseReason::None) == "Attivo");
  CHECK(PauseReasonText(PauseReason::FullscreenWindow) == "Finestra a tutto schermo");
}

TEST(hysteresis_first_update_is_immediate) {
  PauseHysteresis h(300, 2500);
  CHECK(!h.Update(PauseReason::None, 0));
  PauseHysteresis h2(300, 2500);
  CHECK(h2.Update(PauseReason::FullscreenWindow, 0));
  CHECK(h2.Reason() == PauseReason::FullscreenWindow);
}

TEST(hysteresis_immediate_reasons) {
  PauseHysteresis h(300, 2500);
  h.Update(PauseReason::None, 0);
  CHECK(h.Update(PauseReason::SessionLocked, 10));
  CHECK(h.Reason() == PauseReason::SessionLocked);
}

TEST(hysteresis_enter_delay) {
  PauseHysteresis h(300, 2500);
  h.Update(PauseReason::None, 0);
  CHECK(!h.Update(PauseReason::FullscreenWindow, 100));
  CHECK(!h.Update(PauseReason::FullscreenWindow, 399));
  CHECK(h.Update(PauseReason::FullscreenWindow, 400));
}

TEST(hysteresis_flash_does_not_pause) {
  PauseHysteresis h(300, 2500);
  h.Update(PauseReason::None, 0);
  h.Update(PauseReason::FullscreenWindow, 100);
  CHECK(!h.Update(PauseReason::None, 200));
  CHECK(!h.Update(PauseReason::FullscreenWindow, 450));
}

TEST(hysteresis_resume_delay_and_no_flapping) {
  PauseHysteresis h(300, 2500);
  h.Update(PauseReason::Manual, 0);
  CHECK(h.Update(PauseReason::None, 1000));
  CHECK(h.Update(PauseReason::Manual, 2000));  // il conteggio riparte
  CHECK(h.Update(PauseReason::None, 3000));
  CHECK(h.Update(PauseReason::None, 5499));
  CHECK(!h.Update(PauseReason::None, 5500));
  CHECK(h.Reason() == PauseReason::None);
}

TEST(hysteresis_reason_follows_raw_while_paused) {
  PauseHysteresis h(300, 2500);
  h.Update(PauseReason::Manual, 0);
  h.Update(PauseReason::SessionLocked, 100);
  CHECK(h.Reason() == PauseReason::SessionLocked);
  h.Update(PauseReason::None, 200);
  CHECK(h.Reason() == PauseReason::SessionLocked);  // resta l'ultimo motivo noto
}
