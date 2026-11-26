#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.h"
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

TEST(only_app_reasons_go_to_history) {
  // Lo storico "Giochi e app" conta solo le pause causate dall'app in primo piano.
  for (auto r : {PauseReason::Manual, PauseReason::Denylisted, PauseReason::NotificationState,
                 PauseReason::FullscreenWindow})
    CHECK(CausedByApp(r));
  for (auto r : {PauseReason::None, PauseReason::SessionLocked, PauseReason::SessionInactive, PauseReason::SecureDesktop,
                 PauseReason::CaptureFailed, PauseReason::DisplayRotated, PauseReason::MonitorMissing})
    CHECK(!CausedByApp(r));
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

TEST(pause_reason_keys_exist) {
  std::ifstream in(std::filesystem::path(OLED_SOURCE_DIR) / "src" / "ui" / "i18n" / "en.json", std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  const auto en = ParseJson(ss.str());
  CHECK(en.value.has_value());
  if (!en.value) return;
  for (PauseReason r : {PauseReason::None, PauseReason::Manual, PauseReason::SessionLocked, PauseReason::SessionInactive,
                        PauseReason::SecureDesktop, PauseReason::CaptureFailed, PauseReason::DisplayRotated,
                        PauseReason::MonitorMissing, PauseReason::Denylisted, PauseReason::NotificationState,
                        PauseReason::FullscreenWindow, PauseReason::ProtectedWindow}) {
    const std::string key(PauseReasonKey(r));
    CHECK(key.rfind("pause.reason.", 0) == 0);
    if (!en.value->Find(key)) ::oled::test::ReportFailure(__FILE__, __LINE__, "chiave mancante in en.json: " + key);
  }
}

TEST(pause_protected_window) {
  PauseRulesConfig cfg;
  PauseInputs in;
  in.protected_window = true;
  in.protected_process = "keepassxc.exe";
  CHECK(EvaluatePause(in, cfg) == PauseReason::ProtectedWindow);
  in.foreground_process = "keepassxc.exe";
  cfg.allowlist = {"keepassxc.exe"};
  CHECK(EvaluatePause(in, cfg) == PauseReason::ProtectedWindow);  // l'allowlist non la scavalca
  cfg.pause_on_protected_window = false;
  CHECK(EvaluatePause(in, cfg) == PauseReason::None);
  in.manual = true;
  cfg.pause_on_protected_window = true;
  CHECK(EvaluatePause(in, cfg) == PauseReason::Manual);  // le regole prima restano prima
}

TEST(pause_protected_window_is_immediate_not_app) {
  CHECK(IsImmediate(PauseReason::ProtectedWindow));
  CHECK(!CausedByApp(PauseReason::ProtectedWindow));
  CHECK_EQ(kPauseReasonCount, static_cast<int>(PauseReason::ProtectedWindow) + 1);
}

TEST(pause_protected_window_needs_overlap) {
  const ScreenRect monitor{0, 0, 3840, 2160};
  CHECK(!OverlapsEnough({-500, 100, -10, 600}, monitor));        // fuori dallo schermo
  CHECK(!OverlapsEnough({3777, 100, 3900, 300}, monitor));       // 63 x 200 dentro
  CHECK(OverlapsEnough({3776, 2096, 3900, 2300}, monitor));      // 64 x 64 dentro
  CHECK(OverlapsEnough({100, 100, 900, 700}, monitor));
  CHECK(!OverlapsEnough({100, 100, 100, 700}, monitor));         // larghezza zero
}

TEST(protected_window_visible_part) {
  const ScreenRect monitor{0, 0, 3840, 2160};
  const ScreenRect keepass{1000, 500, 1800, 1100};
  // Nessuna finestra sopra: conta come prima.
  CHECK(VisibleEnough(keepass, monitor, {}));
  // Coperta del tutto da un browser ingrandito.
  CHECK(!VisibleEnough(keepass, monitor, {{0, 0, 3840, 2100}}));
  // Spunta un angolo di 100x100 in basso a destra.
  CHECK(VisibleEnough(keepass, monitor, {{0, 0, 1700, 2160}, {1700, 0, 3840, 1000}}));
  // Spunta solo una striscia di 63 px: non basta.
  CHECK(!VisibleEnough(keepass, monitor, {{0, 0, 1737, 2160}}));
  // Due finestre ai lati lasciano libero il centro (200 px).
  CHECK(VisibleEnough(keepass, monitor, {{0, 0, 1300, 2160}, {1500, 0, 3840, 2160}}));
  // Una finestra fuori dallo schermo non copre nulla.
  CHECK(VisibleEnough(keepass, monitor, {{-3000, 0, -10, 2160}}));
  // La parte fuori dallo schermo protetto non conta come visibile.
  CHECK(!VisibleEnough({3800, 500, 4600, 1100}, monitor, {}));
}

TEST(protected_window_visible_part_across_cut) {
  const ScreenRect monitor{0, 0, 3840, 2160};
  const ScreenRect win{0, 0, 600, 600};
  // Una finestra copre la meta' destra in basso e una la meta' destra in alto lasciando
  // scoperta solo una colonna di 100 px a sinistra, alta 600: il pezzo visibile 100x600
  // attraversa i bordi orizzontali delle due finestre e non va spezzato in strisce.
  CHECK(VisibleEnough(win, monitor, {{100, 300, 600, 600}, {100, 0, 600, 300}}));
  // Scoperto solo un quadrato 100x100 a cavallo del bordo superiore della finestra in basso.
  CHECK(VisibleEnough(win, monitor, {{100, 250, 600, 600}, {0, 0, 600, 200}, {0, 300, 100, 600}}));
}

// Regole della 1.1.0 per le app dello Store: allora il nome era sempre quello del processo
// ospite (applicationframehost.exe); dalla 1.2.0 si vede l'app vera. Il vecchio nome vale
// ancora per tutte le app dello Store, ma una regola sull'app precisa vince.
TEST(store_host_rule_still_applies) {
  PauseInputs in;
  in.foreground_process = "netflix.exe";
  in.foreground_host = std::string(oled::kStoreAppHost);
  PauseRulesConfig c;
  c.denylist = {"ApplicationFrameHost.exe"};
  CHECK(EvaluatePause(in, c) == PauseReason::Denylisted);
  in.foreground_covers_monitor = true;
  PauseRulesConfig never;
  never.allowlist = {"applicationframehost.exe"};
  CHECK(EvaluatePause(in, never) == PauseReason::None);
}

TEST(store_precise_rule_wins_over_host_rule) {
  PauseInputs in;
  in.foreground_process = "netflix.exe";
  in.foreground_host = std::string(oled::kStoreAppHost);
  PauseRulesConfig deny_app;
  deny_app.allowlist = {"applicationframehost.exe"};
  deny_app.denylist = {"netflix.exe"};
  CHECK(EvaluatePause(in, deny_app) == PauseReason::Denylisted);
  PauseRulesConfig allow_app;
  allow_app.allowlist = {"netflix.exe"};
  allow_app.denylist = {"applicationframehost.exe"};
  CHECK(EvaluatePause(in, allow_app) == PauseReason::None);
}

TEST(store_host_rule_ignores_normal_apps) {
  PauseInputs in;
  in.foreground_process = "chrome.exe";
  PauseRulesConfig c;
  c.denylist = {"applicationframehost.exe"};
  CHECK(EvaluatePause(in, c) == PauseReason::None);
  const std::vector<std::string> list = {"applicationframehost.exe"};
  CHECK(oled::AppInList(list, "netflix.exe", "applicationframehost.exe"));
  CHECK(!oled::AppInList(list, "chrome.exe", ""));
  CHECK(oled::AppInList({"chrome.exe"}, "Chrome.exe", ""));
  CHECK(!oled::AppInList({}, "", ""));
}
