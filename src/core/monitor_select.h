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
  bool hdr = false;
  std::string connection;     // "HDMI" | "DP" | "DVI" | "internal" | ""
};

// "auto": primo in HDR collegato via HDMI (tipico delle TV), poi il primario, poi il primo.
// Altrimenti nome amichevole o device name (senza distinzione maiuscole).
std::optional<size_t> SelectMonitor(std::span<const MonitorDesc> monitors, std::string_view setting);

// Valore da salvare in config per scegliere il monitor `index`: il nome amichevole se
// e' unico, altrimenti il device name.
std::string MonitorSettingFor(std::span<const MonitorDesc> monitors, size_t index);

// Come MonitorSettingFor, partendo dal device name scelto nella pagina Monitor.
// "auto" e device sconosciuti restano invariati.
std::string MonitorSettingForDevice(std::span<const MonitorDesc> monitors, std::string_view device);

}  // namespace oled
