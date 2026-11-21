#include "pause_rules.h"

#include <algorithm>

#include "ascii.h"

namespace oled {
namespace {

bool InList(const std::vector<std::string>& list, std::string_view process) {
  if (process.empty()) return false;
  return std::any_of(list.begin(), list.end(),
                     [&](const std::string& item) { return EqualsIgnoreCase(item, process); });
}

// QUNS_BUSY non e' fra questi: l'overlay stesso (topmost, grande quanto il monitor)
// lo fa riportare a Windows. Le app fullscreen/borderless che lo causano sono gia'
// coperte dalla regola "finestra in primo piano a tutto schermo".
bool NotificationStatePauses(NotificationState state) {
  switch (state) {
    case NotificationState::NotPresent:
    case NotificationState::RunningD3DFullScreen:
    case NotificationState::PresentationMode:
      return true;
    default:
      return false;
  }
}

}  // namespace

PauseReason EvaluatePause(const PauseInputs& in, const PauseRulesConfig& config) {
  // L'ordine conta: le prime regole non possono essere scavalcate dall'allowlist.
  if (in.manual) return PauseReason::Manual;
  if (in.session_locked) return PauseReason::SessionLocked;
  if (in.session_inactive) return PauseReason::SessionInactive;
  if (in.secure_desktop) return PauseReason::SecureDesktop;
  if (in.capture_failed) return PauseReason::CaptureFailed;
  if (in.display_rotated) return PauseReason::DisplayRotated;
  if (in.monitor_missing) return PauseReason::MonitorMissing;

  if (InList(config.allowlist, in.foreground_process)) return PauseReason::None;
  if (InList(config.denylist, in.foreground_process)) return PauseReason::Denylisted;
  if (config.pause_on_notification_state && NotificationStatePauses(in.notification_state)) {
    return PauseReason::NotificationState;
  }
  if (config.pause_on_fullscreen_window && in.foreground_covers_monitor) return PauseReason::FullscreenWindow;
  return PauseReason::None;
}

bool IsFullscreenWindow(ScreenRect window, ScreenRect monitor, bool maximized, bool has_caption) {
  const bool covers = window.left <= monitor.left && window.top <= monitor.top && window.right >= monitor.right &&
                      window.bottom >= monitor.bottom;
  return covers && !(maximized && has_caption);
}

bool IsImmediate(PauseReason reason) {
  switch (reason) {
    case PauseReason::Manual:
    case PauseReason::SessionLocked:
    case PauseReason::SessionInactive:
    case PauseReason::SecureDesktop:
    case PauseReason::CaptureFailed:
    case PauseReason::DisplayRotated:
    case PauseReason::MonitorMissing:
      return true;
    default:
      return false;
  }
}

bool CausedByApp(PauseReason reason) {
  switch (reason) {
    case PauseReason::Manual:
    case PauseReason::Denylisted:
    case PauseReason::NotificationState:
    case PauseReason::FullscreenWindow:
      return true;
    default:
      return false;
  }
}

std::string_view PauseReasonText(PauseReason reason) {
  switch (reason) {
    case PauseReason::None: return "Attivo";
    case PauseReason::Manual: return "Pausa manuale";
    case PauseReason::SessionLocked: return "Schermo bloccato";
    case PauseReason::SessionInactive: return "Sessione non attiva";
    case PauseReason::SecureDesktop: return "Desktop sicuro (UAC)";
    case PauseReason::CaptureFailed: return "Errore di cattura";
    case PauseReason::DisplayRotated: return "Display ruotato";
    case PauseReason::MonitorMissing: return "Monitor non trovato";
    case PauseReason::Denylisted: return "Processo in denylist";
    case PauseReason::NotificationState: return "Gioco o app a schermo intero";
    case PauseReason::FullscreenWindow: return "Finestra a tutto schermo";
  }
  return "?";
}

PauseHysteresis::PauseHysteresis(double enter_delay_ms, double resume_delay_ms)
    : enter_delay_ms_(enter_delay_ms), resume_delay_ms_(resume_delay_ms) {}

void PauseHysteresis::SetDelays(double enter_delay_ms, double resume_delay_ms) {
  enter_delay_ms_ = enter_delay_ms;
  resume_delay_ms_ = resume_delay_ms;
}

bool PauseHysteresis::Update(PauseReason raw, double now_ms) {
  const bool want_pause = raw != PauseReason::None;

  if (!initialized_) {
    // All'avvio si adotta subito lo stato reale: niente attesa inutile.
    initialized_ = true;
    paused_ = want_pause;
    reason_ = raw;
    pending_ = false;
    return paused_;
  }

  if (paused_) {
    if (want_pause) {
      reason_ = raw;
      pending_ = false;
      return true;
    }
    if (!pending_) {
      pending_ = true;
      pending_since_ms_ = now_ms;
    }
    if (now_ms - pending_since_ms_ >= resume_delay_ms_) {
      paused_ = false;
      reason_ = PauseReason::None;
      pending_ = false;
    }
    return paused_;
  }

  if (!want_pause) {
    pending_ = false;
    return false;
  }
  if (IsImmediate(raw)) {
    paused_ = true;
    reason_ = raw;
    pending_ = false;
    return true;
  }
  if (!pending_) {
    pending_ = true;
    pending_since_ms_ = now_ms;
  }
  if (now_ms - pending_since_ms_ >= enter_delay_ms_) {
    paused_ = true;
    reason_ = raw;
    pending_ = false;
  }
  return paused_;
}

}  // namespace oled
