#include "monitor_select.h"

#include "ascii.h"

namespace oled {

std::optional<size_t> SelectMonitor(std::span<const MonitorDesc> monitors, std::string_view setting) {
  if (monitors.empty()) return std::nullopt;

  if (EqualsIgnoreCase(TrimAscii(setting), "auto")) {
    for (size_t i = 0; i < monitors.size(); ++i)
      if (monitors[i].hdr && monitors[i].connection == "HDMI") return i;
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

std::optional<int> DisplayNumber(std::string_view setting) {
  std::string_view s = TrimAscii(setting);
  constexpr std::string_view kPrefix = R"(\\.\)";
  if (s.substr(0, kPrefix.size()) == kPrefix) s.remove_prefix(kPrefix.size());
  constexpr std::string_view kDisplay = "DISPLAY";
  if (s.size() <= kDisplay.size() || !EqualsIgnoreCase(s.substr(0, kDisplay.size()), kDisplay)) return std::nullopt;
  s.remove_prefix(kDisplay.size());
  if (s.size() > 4) return std::nullopt;
  int n = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    n = n * 10 + (c - '0');
  }
  if (n == 0) return std::nullopt;
  return n;
}

std::string MonitorSettingFor(std::span<const MonitorDesc> monitors, size_t index) {
  if (index >= monitors.size()) return "auto";
  const auto& chosen = monitors[index];
  for (size_t i = 0; i < monitors.size(); ++i) {
    if (i != index && EqualsIgnoreCase(monitors[i].friendly_name, chosen.friendly_name)) return chosen.device_name;
  }
  return chosen.friendly_name;
}

std::string MonitorSettingForDevice(std::span<const MonitorDesc> monitors, std::string_view device) {
  for (size_t i = 0; i < monitors.size(); ++i) {
    if (EqualsIgnoreCase(monitors[i].device_name, device)) return MonitorSettingFor(monitors, i);
  }
  return std::string(device);
}

}  // namespace oled
