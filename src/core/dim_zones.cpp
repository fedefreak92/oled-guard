#include "dim_zones.h"

#include "ascii.h"

namespace oled {
namespace {

bool Contains(const ZoneRect& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

bool AnyOf(std::string_view cls, std::initializer_list<std::string_view> names) {
  const std::string lower = ToLowerAscii(cls);
  for (std::string_view n : names) {
    if (lower == ToLowerAscii(n)) return true;
  }
  return false;
}

}  // namespace

std::vector<uint8_t> ComputeDimZones(int cols, int rows, ZoneRect monitor, const std::vector<ZoneRect>& apps,
                                     const std::vector<ZoneRect>& taskbars) {
  std::vector<uint8_t> zones(static_cast<size_t>(cols) * rows, kDimZoneShell);
  for (int cy = 0; cy < rows; ++cy) {
    for (int cx = 0; cx < cols; ++cx) {
      const int x = monitor.left + cx * kDimCellPx + kDimCellPx / 2;
      const int y = monitor.top + cy * kDimCellPx + kDimCellPx / 2;
      bool app = false;
      for (const ZoneRect& r : apps) app = app || Contains(r, x, y);
      for (const ZoneRect& r : taskbars) app = app && !Contains(r, x, y);
      zones[static_cast<size_t>(cy) * cols + cx] = app ? kDimZoneApp : kDimZoneShell;
    }
  }
  return zones;
}

bool IsShellWindowClass(std::string_view cls) {
  return AnyOf(cls, {"Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd"});
}

bool IsInvisibleOverlay(uint32_t ex_style, bool has_layered_attributes, uint32_t lwa_flags, uint8_t alpha) {
  constexpr uint32_t kExLayered = 0x00080000;  // WS_EX_LAYERED
  constexpr uint32_t kLwaAlpha = 0x2;          // LWA_ALPHA
  return (ex_style & kExLayered) != 0 && has_layered_attributes && (lwa_flags & kLwaAlpha) != 0 && alpha == 0;
}

bool IsTaskSwitcherClass(std::string_view cls) {
  return AnyOf(cls, {"XamlExplorerHostIslandWindow", "MultitaskingViewFrame", "TaskSwitcherWnd", "ForegroundStaging"});
}

}  // namespace oled
