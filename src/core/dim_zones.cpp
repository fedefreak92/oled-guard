#include "dim_zones.h"

#include <algorithm>

#include "ascii.h"

namespace oled {
namespace {

ZoneRect Intersect(const ZoneRect& a, const ZoneRect& b) {
  return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}
bool Empty(const ZoneRect& r) { return r.left >= r.right || r.top >= r.bottom; }
bool Covers(const ZoneRect& outer, const ZoneRect& inner) {
  return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right && inner.bottom <= outer.bottom;
}

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
      const ZoneRect cell = Intersect({monitor.left + cx * kDimCellPx, monitor.top + cy * kDimCellPx,
                                       monitor.left + (cx + 1) * kDimCellPx, monitor.top + (cy + 1) * kDimCellPx},
                                      monitor);
      // La prima finestra (la piu' in alto) con almeno un pixel visibile nella cella, cioe'
      // non coperto dalla barra delle applicazioni.
      for (size_t i = 0; i < apps.size(); ++i) {
        const ZoneRect part = Intersect(cell, apps[i]);
        if (Empty(part)) continue;
        bool under_taskbar = false;
        for (const ZoneRect& t : taskbars) under_taskbar = under_taskbar || Covers(t, part);
        if (under_taskbar) continue;
        zones[static_cast<size_t>(cy) * cols + cx] = static_cast<uint8_t>(1 + i % 255);
        break;
      }
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
