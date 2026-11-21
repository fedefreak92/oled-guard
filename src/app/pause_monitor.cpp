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

}  // namespace

std::vector<std::string> ListWindowedProcesses() {
  std::vector<DWORD> pids;
  EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) == 0 || GetWindow(hwnd, GW_OWNER)) return TRUE;
        if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
        BOOL cloaked = FALSE;  // finestre UWP sospese o su altri desktop virtuali
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) reinterpret_cast<std::vector<DWORD>*>(lp)->push_back(pid);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&pids));
  std::vector<std::string> names;
  for (DWORD pid : pids) {
    std::string name = ProcessName(pid);
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
    in.foreground_process = ProcessName(pid);
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
  return in;
}

}  // namespace oled
