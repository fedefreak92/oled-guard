#include "pause_monitor.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <filesystem>
#include <string>

#include "ascii.h"
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

bool IsShellWindow(HWND hwnd) {
  wchar_t cls[64] = {};
  GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
  for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
    if (_wcsicmp(cls, shell) == 0) return true;
  }
  return false;
}

std::string ProcessName(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  wchar_t path[MAX_PATH * 2] = {};
  DWORD size = ARRAYSIZE(path);
  std::string name;
  if (QueryFullProcessImageNameW(process, 0, path, &size)) {
    name = ToLowerAscii(Narrow(std::filesystem::path(path).filename().wstring()));
  }
  CloseHandle(process);
  return name;
}

}  // namespace

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
    }
  }
  return in;
}

}  // namespace oled
