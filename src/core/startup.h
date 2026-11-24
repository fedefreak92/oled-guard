// Decisioni dell'avvio: procedura di primo avvio e avviso per lo schermo scelto che manca.
#pragma once

namespace oled {

struct StartupDecision {
  bool setup_pending = false;       // si apre la procedura di primo avvio, protezione ferma
  bool start_protection = false;    // la protezione parte subito
  bool notify_auto_choice = false;  // avvisare che lo schermo e' stato scelto in automatico
};

// Primo avvio = config.json assente. Senza interfaccia (WebView2 mancante) la procedura
// non si puo' mostrare: si protegge lo schermo scelto in automatico e lo si dice.
StartupDecision DecideStartup(bool config_exists, bool ui_available);

// La finestra non e' partita anche se WebView2 risultava installato: durante la procedura
// di primo avvio si passa alla scelta automatica (come senza interfaccia), altrimenti
// non cambia nulla.
StartupDecision AfterUiFailure(const StartupDecision& current);

// Avviso "lo schermo scelto non c'e'": una volta sola quando manca da almeno 30 s di
// fila e l'app vive da almeno 60 s (all'avvio di Windows la TV puo' accendersi dopo).
// Mai se lo schermo si e' visto in questa sessione: una TV spenta la sera col PC acceso
// non e' un errore di configurazione.
class MissingMonitorNotifier {
 public:
  static constexpr double kMissingMs = 30000;
  static constexpr double kUptimeMs = 60000;

  bool Update(bool missing, double uptime_ms, double now_ms);

 private:
  bool missing_ = false;
  double since_ms_ = 0;
  bool notified_ = false;
  bool seen_ = false;
};

}  // namespace oled
