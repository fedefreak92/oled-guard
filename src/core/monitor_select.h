// Scelta del monitor da proteggere in base all'impostazione "monitor" della config.
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace oled {

struct MonitorDesc {
  std::string device_name;    // es. \\.\DISPLAY1 (UTF-8)
  std::string friendly_name;  // es. LG TV SSCR2 (UTF-8)
  bool primary = false;
};

// "auto": primo con "LG TV" nel nome, poi il primario, poi il primo.
// Altrimenti nome amichevole o device name (senza distinzione maiuscole).
std::optional<size_t> SelectMonitor(std::span<const MonitorDesc> monitors, std::string_view setting);

}  // namespace oled
