// Regole di pausa automatica e isteresi. Logica pura: i segnali sono raccolti altrove.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace oled {

// Rispecchia QUERY_USER_NOTIFICATION_STATE senza dipendere dagli header di Windows.
enum class NotificationState {
  AcceptsNotifications,
  NotPresent,
  Busy,
  RunningD3DFullScreen,
  PresentationMode,
  QuietTime,
  App,
};

struct PauseInputs {
  bool manual = false;
  bool session_locked = false;
  bool session_inactive = false;
  bool secure_desktop = false;
  bool capture_failed = false;
  bool display_rotated = false;
  bool monitor_missing = false;
  NotificationState notification_state = NotificationState::AcceptsNotifications;
  bool foreground_covers_monitor = false;
  std::string foreground_process;  // nome .exe, UTF-8
  // Una finestra esclusa dalla cattura (DRM, gestori di password...) e' visibile sullo
  // schermo protetto: l'overlay ci disegnerebbe sopra e l'utente non la vedrebbe.
  bool protected_window = false;
  std::string protected_process;   // nome .exe della prima trovata, UTF-8
};

enum class PauseReason {
  None,
  Manual,
  SessionLocked,
  SessionInactive,
  SecureDesktop,
  CaptureFailed,
  DisplayRotated,
  MonitorMissing,
  Denylisted,
  NotificationState,
  FullscreenWindow,
  ProtectedWindow,  // ultimo: i numeri dei motivi finiscono in stats.json
};

inline constexpr int kPauseReasonCount = static_cast<int>(PauseReason::ProtectedWindow) + 1;

struct PauseRulesConfig {
  bool pause_on_notification_state = true;
  bool pause_on_fullscreen_window = true;
  bool pause_on_protected_window = true;
  std::vector<std::string> denylist;
  std::vector<std::string> allowlist;
};

PauseReason EvaluatePause(const PauseInputs& inputs, const PauseRulesConfig& config);

struct ScreenRect {
  long left = 0;
  long top = 0;
  long right = 0;
  long bottom = 0;
};

// La finestra in primo piano conta come fullscreen/borderless se copre l'intero
// monitor e non e' una normale finestra massimizzata con barra del titolo (caso della
// barra delle applicazioni a scomparsa).
bool IsFullscreenWindow(ScreenRect window, ScreenRect monitor, bool maximized, bool has_caption);

// La finestra conta se la sua parte sullo schermo e' almeno min_side x min_side.
bool OverlapsEnough(ScreenRect window, ScreenRect monitor, long min_side = 64);

// Motivi per cui la pausa scatta subito, senza ritardo di entrata.
bool IsImmediate(PauseReason reason);

// Pause dovute all'app in primo piano (o decise dall'utente mentre la usava): sono le
// sole che finiscono nello storico "Giochi e app" e nel conteggio delle pause.
bool CausedByApp(PauseReason reason);

// Testo italiano (UTF-8) per il log.
std::string_view PauseReasonText(PauseReason reason);

// Chiave di traduzione per l'utente ("pause.reason.manual", ...).
std::string_view PauseReasonKey(PauseReason reason);

class PauseHysteresis {
 public:
  PauseHysteresis(double enter_delay_ms, double resume_delay_ms);

  // Ritorna true se si e' in pausa dopo aver considerato il motivo grezzo.
  bool Update(PauseReason raw, double now_ms);
  bool Paused() const { return paused_; }
  PauseReason Reason() const { return reason_; }
  void SetDelays(double enter_delay_ms, double resume_delay_ms);

 private:
  double enter_delay_ms_;
  double resume_delay_ms_;
  bool initialized_ = false;
  bool paused_ = false;
  PauseReason reason_ = PauseReason::None;
  bool pending_ = false;          // condizione opposta in corso
  double pending_since_ms_ = 0;
};

}  // namespace oled
