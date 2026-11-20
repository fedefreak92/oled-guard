#include "monitor_select.h"

#include "ascii.h"

namespace oled {

std::optional<size_t> SelectMonitor(std::span<const MonitorDesc> monitors, std::string_view setting) {
  if (monitors.empty()) return std::nullopt;

  if (EqualsIgnoreCase(TrimAscii(setting), "auto")) {
    for (size_t i = 0; i < monitors.size(); ++i)
      if (ContainsIgnoreCase(monitors[i].friendly_name, "LG TV")) return i;
    for (size_t i = 0; i < monitors.size(); ++i)
      if (monitors[i].primary) return i;
    return 0;
  }

  for (size_t i = 0; i < monitors.size(); ++i) {
    if (EqualsIgnoreCase(monitors[i].friendly_name, setting) || EqualsIgnoreCase(monitors[i].device_name, setting)) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace oled
