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

bool AppInList(const std::vector<std::string>& list, std::string_view process, std::string_view host) {
  return InList(list, process) || InList(list, host);
}

PauseReason EvaluatePause(const PauseInputs& in, const PauseRulesConfig& config) {
  // L'ordine conta: le prime regole non possono essere scavalcate dall'allowlist.
  if (in.manual) return PauseReason::Manual;
  if (in.session_locked) return PauseReason::SessionLocked;
  if (in.session_inactive) return PauseReason::SessionInactive;
  if (in.secure_desktop) return PauseReason::SecureDesktop;
  if (in.capture_failed) return PauseReason::CaptureFailed;
  if (in.display_rotated) return PauseReason::DisplayRotated;
  if (in.monitor_missing) return PauseReason::MonitorMissing;
  // Prima delle regole per app: riguarda cio' che si vede, non l'app in primo piano.
  if (config.pause_on_protected_window && in.protected_window) return PauseReason::ProtectedWindow;

  if (InList(config.allowlist, in.foreground_process)) return PauseReason::None;
  if (InList(config.denylist, in.foreground_process)) return PauseReason::Denylisted;
  if (InList(config.allowlist, in.foreground_host)) return PauseReason::None;
  if (InList(config.denylist, in.foreground_host)) return PauseReason::Denylisted;
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

bool OverlapsEnough(ScreenRect window, ScreenRect monitor, long min_side) {
  const long w = std::min(window.right, monitor.right) - std::max(window.left, monitor.left);
  const long h = std::min(window.bottom, monitor.bottom) - std::max(window.top, monitor.top);
  return w >= min_side && h >= min_side;
}

bool VisibleEnough(ScreenRect window, ScreenRect monitor, const std::vector<ScreenRect>& above, long min_side) {
  const ScreenRect clip{std::max(window.left, monitor.left), std::max(window.top, monitor.top),
                        std::min(window.right, monitor.right), std::min(window.bottom, monitor.bottom)};
  if (clip.right - clip.left < min_side || clip.bottom - clip.top < min_side) return false;
  // Griglia lungo i bordi di tutte le finestre sopra (ritagliati alla zona): ogni cella e'
  // tutta coperta o tutta scoperta. Si cerca un rettangolo di celle scoperte largo e alto
  // almeno min_side, cosi' il risultato non dipende da come si spezzano i pezzi.
  std::vector<long> xs{clip.left, clip.right}, ys{clip.top, clip.bottom};
  std::vector<ScreenRect> cover;
  for (const ScreenRect& o : above) {
    const ScreenRect c{std::max(o.left, clip.left), std::max(o.top, clip.top), std::min(o.right, clip.right),
                       std::min(o.bottom, clip.bottom)};
    if (c.right <= c.left || c.bottom <= c.top) continue;
    cover.push_back(c);
    xs.insert(xs.end(), {c.left, c.right});
    ys.insert(ys.end(), {c.top, c.bottom});
  }
  std::sort(xs.begin(), xs.end());
  xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  std::sort(ys.begin(), ys.end());
  ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
  const size_t cols = xs.size() - 1, rows = ys.size() - 1;
  std::vector<char> open(cols * rows, 1);
  for (size_t r = 0; r < rows; ++r) {
    for (size_t c = 0; c < cols; ++c) {
      for (const ScreenRect& o : cover) {
        if (o.left <= xs[c] && o.right >= xs[c + 1] && o.top <= ys[r] && o.bottom >= ys[r + 1]) {
          open[r * cols + c] = 0;
          break;
        }
      }
    }
  }
  // Per ogni intervallo di colonne [i, j] abbastanza largo, righe consecutive tutte
  // scoperte in quell'intervallo, abbastanza alte.
  std::vector<char> row_ok(rows);
  for (size_t i = 0; i < cols; ++i) {
    std::fill(row_ok.begin(), row_ok.end(), char{1});
    for (size_t j = i; j < cols; ++j) {
      for (size_t r = 0; r < rows; ++r) row_ok[r] = static_cast<char>(row_ok[r] && open[r * cols + j]);
      if (xs[j + 1] - xs[i] < min_side) continue;
      long run = 0;
      for (size_t r = 0; r < rows; ++r) {
        run = row_ok[r] ? run + (ys[r + 1] - ys[r]) : 0;
        if (run >= min_side) return true;
      }
    }
  }
  return false;
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
    case PauseReason::ProtectedWindow:
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

std::string_view PauseReasonKey(PauseReason reason) {
  switch (reason) {
    case PauseReason::None: return "pause.reason.none";
    case PauseReason::Manual: return "pause.reason.manual";
    case PauseReason::SessionLocked: return "pause.reason.session_locked";
    case PauseReason::SessionInactive: return "pause.reason.session_inactive";
    case PauseReason::SecureDesktop: return "pause.reason.secure_desktop";
    case PauseReason::CaptureFailed: return "pause.reason.capture_failed";
    case PauseReason::DisplayRotated: return "pause.reason.display_rotated";
    case PauseReason::MonitorMissing: return "pause.reason.monitor_missing";
    case PauseReason::Denylisted: return "pause.reason.denylisted";
    case PauseReason::NotificationState: return "pause.reason.notification_state";
    case PauseReason::FullscreenWindow: return "pause.reason.fullscreen_window";
    case PauseReason::ProtectedWindow: return "pause.reason.protected_window";
  }
  return "pause.reason.none";
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
    case PauseReason::ProtectedWindow: return "Finestra protetta dalla cattura";
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
