// Enumerazione dei monitor (DXGI + nomi amichevoli da DisplayConfig).
#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oled {

struct MonitorInfo {
  std::wstring device_name;    // \\.\DISPLAY1
  std::wstring friendly_name;  // LG TV SSCR2
  RECT rect{};                 // coordinate desktop (pixel fisici, DPI PerMonitorV2)
  LUID adapter_luid{};
  bool primary = false;
  DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
  bool hdr = false;  // desktop in HDR (colorspace ST.2084)
};

std::vector<MonitorInfo> EnumerateMonitors();
std::optional<MonitorInfo> FindMonitor(std::string_view setting);

// Bianco SDR del monitor in scRGB (1 = 80 nit), dal cursore "luminosita' contenuto SDR".
std::optional<float> QuerySdrWhiteLevel(const MonitorInfo& monitor);

HRESULT OpenOutput(const MonitorInfo& monitor, IDXGIAdapter1** adapter, IDXGIOutput5** output);

}  // namespace oled
