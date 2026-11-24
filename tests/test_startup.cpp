#include "startup.h"
#include "test_framework.h"

using oled::DecideStartup;
using oled::MissingMonitorNotifier;

TEST(startup_first_run_waits_for_setup) {
  const auto d = DecideStartup(/*config_exists=*/false, /*ui_available=*/true);
  CHECK(d.setup_pending);
  CHECK(!d.start_protection);
  CHECK(!d.notify_auto_choice);
}

TEST(startup_without_ui_starts_protection) {
  const auto d = DecideStartup(false, false);
  CHECK(!d.setup_pending);
  CHECK(d.start_protection);
  CHECK(d.notify_auto_choice);
}

TEST(startup_existing_config) {
  for (bool ui : {true, false}) {
    const auto d = DecideStartup(true, ui);
    CHECK(!d.setup_pending);
    CHECK(d.start_protection);
    CHECK(!d.notify_auto_choice);
  }
}

TEST(missing_monitor_notifies_once) {
  MissingMonitorNotifier n;
  const double up = 120000;  // app avviata da 2 minuti
  CHECK(!n.Update(true, up, 0));
  CHECK(!n.Update(true, up + 29000, 29000));
  CHECK(n.Update(true, up + 30000, 30000));
  CHECK(!n.Update(true, up + 90000, 90000));  // una volta sola
}

TEST(missing_monitor_waits_for_uptime) {
  MissingMonitorNotifier n;
  // Mancante fin dall'avvio (TV che si accende con Windows): niente avviso nei primi 60 s.
  CHECK(!n.Update(true, 0, 0));
  CHECK(!n.Update(true, 50000, 50000));
  CHECK(n.Update(true, 60000, 60000));
}

TEST(startup_ui_failure_during_setup_falls_back_to_auto) {
  const auto pending = DecideStartup(false, true);
  const auto d = oled::AfterUiFailure(pending);
  CHECK(!d.setup_pending);
  CHECK(d.start_protection);
  CHECK(d.notify_auto_choice);
  // Fuori dalla procedura un errore della finestra non cambia nulla.
  const auto normal = DecideStartup(true, true);
  const auto n = oled::AfterUiFailure(normal);
  CHECK(!n.setup_pending && n.start_protection && !n.notify_auto_choice);
}

TEST(missing_monitor_not_after_seen) {
  MissingMonitorNotifier n;
  const double up = 120000;
  CHECK(!n.Update(false, up, 0));               // lo schermo c'era
  CHECK(!n.Update(true, up + 1000, 1000));      // la TV si spegne la sera
  CHECK(!n.Update(true, up + 90000, 90000));    // nessun avviso: era gia' stato visto
}
