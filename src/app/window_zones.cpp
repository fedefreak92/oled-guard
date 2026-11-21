#include "window_zones.h"

#include <dwmapi.h>

#include "dim_zones.h"
#include "text.h"

namespace oled {
namespace {

struct Collected {
  std::vector<ZoneRect> apps;
  std::vector<ZoneRect> taskbars;
  std::vector<std::string>* described = nullptr;
};

void Describe(Collected& c, const char* kind, const std::string& cls, const RECT& r) {
  if (!c.described) return;
  c.described->push_back(std::string(kind) + " " + cls + " [" + std::to_string(r.left) + "," + std::to_string(r.top) +
                         "," + std::to_string(r.right) + "," + std::to_string(r.bottom) + "]");
}

std::string ClassOf(HWND hwnd) {
  wchar_t cls[128] = {};
  GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
  return Narrow(cls);
}

BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM lp) {
  auto& out = *reinterpret_cast<Collected*>(lp);
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
  BOOL cloaked = FALSE;  // UWP sospese, altri desktop virtuali, menu Start chiuso
  DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
  if (cloaked) return TRUE;
  const std::string cls = ClassOf(hwnd);
  if (cls == "Shell_TrayWnd" || cls == "Shell_SecondaryTrayWnd") {
    RECT r{};
    if (GetWindowRect(hwnd, &r)) {
      out.taskbars.push_back({r.left, r.top, r.right, r.bottom});
      Describe(out, "barra", cls, r);
    }
    return TRUE;
  }
  if (IsShellWindowClass(cls) || IsTaskSwitcherClass(cls)) return TRUE;
  // Finestre trasparenti ai clic (overlay di altri programmi, compreso il nostro): non sono app.
  const auto ex_style = static_cast<uint32_t>(GetWindowLongW(hwnd, GWL_EXSTYLE));
  if (ex_style & WS_EX_TRANSPARENT) return TRUE;
  BYTE alpha = 255;
  DWORD lwa_flags = 0;
  const bool has_attr = (ex_style & WS_EX_LAYERED) && GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &lwa_flags);
  if (IsInvisibleOverlay(ex_style, has_attr, lwa_flags, alpha)) return TRUE;
  RECT r{};
  if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) {
    if (!GetWindowRect(hwnd, &r)) return TRUE;
  }
  if (r.right > r.left && r.bottom > r.top) {
    out.apps.push_back({r.left, r.top, r.right, r.bottom});
    Describe(out, "app", cls, r);
  }
  return TRUE;
}

}  // namespace

std::vector<uint8_t> CollectDimZones(const MonitorInfo& monitor, int cols, int rows,
                                     std::vector<std::string>* described) {
  Collected c;
  c.described = described;
  EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&c));
  const RECT& m = monitor.rect;
  return ComputeDimZones(cols, rows, {m.left, m.top, m.right, m.bottom}, c.apps, c.taskbars);
}

}  // namespace oled
