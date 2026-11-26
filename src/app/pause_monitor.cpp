#include "pause_monitor.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "ascii.h"
#include "logger.h"
#include "dim_zones.h"
#include "text.h"

namespace oled {
namespace {

NotificationState QueryNotificationState() {
  QUERY_USER_NOTIFICATION_STATE state{};
  if (FAILED(SHQueryUserNotificationState(&state))) return NotificationState::AcceptsNotifications;
  switch (state) {
    case QUNS_NOT_PRESENT: return NotificationState::NotPresent;
    case QUNS_BUSY: return NotificationState::Busy;
    case QUNS_RUNNING_D3D_FULL_SCREEN: return NotificationState::RunningD3DFullScreen;
    case QUNS_PRESENTATION_MODE: return NotificationState::PresentationMode;
    case QUNS_QUIET_TIME: return NotificationState::QuietTime;
    case QUNS_APP: return NotificationState::App;
    default: return NotificationState::AcceptsNotifications;
  }
}

// UAC e Ctrl+Alt+Canc girano su un desktop diverso da "Default".
bool OnSecureDesktop() {
  HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!desk) return true;
  wchar_t name[64] = {};
  DWORD needed = 0;
  const bool ok = GetUserObjectInformationW(desk, UOI_NAME, name, sizeof(name), &needed) != FALSE;
  CloseDesktop(desk);
  return !ok || _wcsicmp(name, L"Default") != 0;
}

// Desktop, barra e selettore di Windows (Alt+Tab, Visualizzazione attivita'): non sono
// app in primo piano e non mettono in pausa anche se coprono tutto lo schermo.
bool IsShellWindow(HWND hwnd) {
  wchar_t cls[128] = {};
  GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
  const std::string name = Narrow(cls);
  return IsShellWindowClass(name) || IsTaskSwitcherClass(name);
}

std::string ProcessName(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  wchar_t path[MAX_PATH * 2] = {};
  DWORD size = ARRAYSIZE(path);
  std::string name;
  if (QueryFullProcessImageNameW(process, 0, path, &size)) {
    name = LowerUnicode(Narrow(std::filesystem::path(path).filename().wstring()));
  }
  CloseHandle(process);
  return name;
}

bool IsCloaked(HWND hwnd) {
  BOOL cloaked = FALSE;  // finestre UWP sospese o su altri desktop virtuali
  DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
  return cloaked != FALSE;
}

}  // namespace

std::string WindowProcessName(HWND hwnd, std::string* host) {
  if (host) host->clear();
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  std::string name = ProcessName(pid);
  if (name != kStoreAppHost) return name;
  struct Search {
    DWORD host_pid;
    DWORD app_pid = 0;
  } search{pid};
  EnumChildWindows(
      hwnd,
      [](HWND child, LPARAM lp) -> BOOL {
        auto* s = reinterpret_cast<Search*>(lp);
        wchar_t cls[64] = {};
        GetClassNameW(child, cls, ARRAYSIZE(cls));
        DWORD child_pid = 0;
        GetWindowThreadProcessId(child, &child_pid);
        if (wcscmp(cls, L"Windows.UI.Core.CoreWindow") == 0 && child_pid != s->host_pid) {
          s->app_pid = child_pid;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  if (search.app_pid == 0) return name;
  std::string app = ProcessName(search.app_pid);
  if (app.empty()) return name;
  if (host) *host = name;
  return app;
}

// Finestra che copre di sicuro cio' che sta sotto: niente trasparenze, niente finestre che
// lasciano passare i clic, niente overlay o menu (strumenti, non attivabili). Nel dubbio
// non conta come copertura: meglio una pausa in piu' che una finestra nascosta dall'overlay.
bool IsOpaqueCover(HWND hwnd) {
  const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (ex & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) return false;
  if (ex & WS_EX_LAYERED) {
    COLORREF key = 0;
    BYTE alpha = 0;
    DWORD flags = 0;
    // Fallisce con UpdateLayeredWindow (alfa per pixel): trasparenza ignota.
    if (!GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags)) return false;
    if ((flags & LWA_COLORKEY) || ((flags & LWA_ALPHA) && alpha < 255)) return false;
  }
  return true;
}

std::optional<std::string> FindProtectedWindow(const RECT& monitor) {
  struct Search {
    RECT monitor;
    HWND found = nullptr;
    std::vector<ScreenRect> above;  // finestre opache gia' viste, piu' in alto nell'ordine z
  } search{monitor};
  EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
        auto* s = reinterpret_cast<Search*>(lp);
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || IsCloaked(hwnd)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        // Le finestre di OLED Guard non sono mai "protette" da mettere in pausa (l'overlay si
        // esclude dalla cattura apposta), ma l'interfaccia copre come qualsiasi finestra.
        const bool own = pid == GetCurrentProcessId();
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(hwnd, &r);
        const ScreenRect rect{r.left, r.top, r.right, r.bottom};
        const RECT& m = s->monitor;
        DWORD affinity = WDA_NONE;
        // EnumWindows va dall'alto verso il basso: conta solo la parte non coperta dalle
        // finestre opache sopra. Una finestra protetta coperta del tutto non va in pausa.
        if (!own && GetWindowDisplayAffinity(hwnd, &affinity) && affinity != WDA_NONE &&
            VisibleEnough(rect, {m.left, m.top, m.right, m.bottom}, s->above)) {
          s->found = hwnd;
          return FALSE;
        }
        if (IsOpaqueCover(hwnd)) s->above.push_back(rect);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  static HWND last_logged = nullptr;  // una riga quando cambia, non 4 volte al secondo
  if (!search.found) {
    last_logged = nullptr;
    return std::nullopt;
  }
  if (search.found != last_logged && LogEnabled(LogLevel::Debug)) {
    last_logged = search.found;
    wchar_t cls[128] = {};
    GetClassNameW(search.found, cls, ARRAYSIZE(cls));
    RECT r{};
    GetWindowRect(search.found, &r);
    LogDebug("Finestra protetta visibile: classe {} ({},{})-({},{})", Narrow(cls), r.left, r.top, r.right, r.bottom);
  }
  return WindowProcessName(search.found);
}

std::vector<std::string> ListWindowedProcesses() {
  std::vector<HWND> windows;
  EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) == 0 || GetWindow(hwnd, GW_OWNER)) return TRUE;
        if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
        if (IsCloaked(hwnd)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) reinterpret_cast<std::vector<HWND>*>(lp)->push_back(hwnd);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&windows));
  std::vector<std::string> names;
  for (HWND hwnd : windows) {
    std::string name = WindowProcessName(hwnd);
    if (!name.empty()) names.push_back(std::move(name));
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

PauseMonitor::PauseMonitor() { inactive_ = GetSystemMetrics(SM_REMOTESESSION) != 0; }

void PauseMonitor::OnSessionChange(WPARAM wts_event) {
  switch (wts_event) {
    case WTS_SESSION_LOCK: locked_ = true; break;
    case WTS_SESSION_UNLOCK: locked_ = false; break;
    case WTS_CONSOLE_DISCONNECT:
    case WTS_REMOTE_DISCONNECT:
    case WTS_REMOTE_CONNECT:  // sessione remota: la TV non mostra questa sessione
      inactive_ = true;
      break;
    case WTS_CONSOLE_CONNECT: inactive_ = false; break;
    default: break;
  }
}

PauseInputs PauseMonitor::Collect(const MonitorInfo* monitor) const {
  PauseInputs in;
  in.session_locked = locked_;
  in.session_inactive = inactive_;
  in.secure_desktop = OnSecureDesktop();
  in.notification_state = QueryNotificationState();
  in.monitor_missing = monitor == nullptr;

  HWND fg = GetForegroundWindow();
  DWORD pid = 0;
  if (fg) GetWindowThreadProcessId(fg, &pid);
  if (fg && pid != GetCurrentProcessId() && !IsShellWindow(fg)) {
    in.foreground_process = WindowProcessName(fg, &in.foreground_host);
    RECT bounds{};
    if (monitor && SUCCEEDED(DwmGetWindowAttribute(fg, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
      const RECT& m = monitor->rect;
      const bool has_caption = (GetWindowLongW(fg, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
      in.foreground_covers_monitor =
          IsFullscreenWindow({bounds.left, bounds.top, bounds.right, bounds.bottom}, {m.left, m.top, m.right, m.bottom},
                             IsZoomed(fg) != FALSE, has_caption);
      if (in.foreground_covers_monitor) {
        // Solo quando cambia: in un gioco si resterebbe qui 4 volte al secondo per ore.
        static HWND last_logged = nullptr;
        if (fg != last_logged) {
          wchar_t cls[128] = {};
          GetClassNameW(fg, cls, ARRAYSIZE(cls));
          LogDebug("Finestra a tutto schermo: classe {} ({})", Narrow(cls), in.foreground_process);
          last_logged = fg;
        }
      }
    }
  }
  if (monitor) {
    if (auto process = FindProtectedWindow(monitor->rect)) {
      in.protected_window = true;
      in.protected_process = std::move(*process);
    }
  }
  return in;
}

}  // namespace oled
