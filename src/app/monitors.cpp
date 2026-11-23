#include "monitors.h"

#include <map>

#include "logger.h"
#include "monitor_select.h"
#include "text.h"

using Microsoft::WRL::ComPtr;

namespace oled {
namespace {

// Device name GDI (\\.\DISPLAYn) -> nome amichevole del monitor collegato.
std::map<std::wstring, std::wstring> FriendlyNames() {
  std::map<std::wstring, std::wstring> names;
  UINT32 path_count = 0;
  UINT32 mode_count = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) return names;
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) !=
      ERROR_SUCCESS) {
    return names;
  }
  paths.resize(path_count);
  for (const auto& path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;

    DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
    target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size = sizeof(target);
    target.header.adapterId = path.targetInfo.adapterId;
    target.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;

    names[source.viewGdiDeviceName] = target.monitorFriendlyDeviceName;
  }
  return names;
}

struct PathExtras {
  int refresh_hz = 0;
  std::string connection;
};

std::string ConnectionName(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY tech) {
  switch (tech) {
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI: return "HDMI";
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: return "DP";
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: return "DVI";
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED: return "internal";
    default: return "";
  }
}

// Device name GDI -> frequenza e collegamento. Errori: mappa vuota.
std::map<std::wstring, PathExtras> PathDetails() {
  std::map<std::wstring, PathExtras> out;
  UINT32 path_count = 0;
  UINT32 mode_count = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) return out;
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) !=
      ERROR_SUCCESS) {
    return out;
  }
  paths.resize(path_count);
  for (const auto& path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
    PathExtras extras;
    const DISPLAYCONFIG_RATIONAL rate = path.targetInfo.refreshRate;
    if (rate.Denominator != 0) {
      extras.refresh_hz = static_cast<int>(static_cast<double>(rate.Numerator) / rate.Denominator + 0.5);
    }
    extras.connection = ConnectionName(path.targetInfo.outputTechnology);
    out[source.viewGdiDeviceName] = extras;
  }
  return out;
}

}  // namespace

std::vector<MonitorDetails> ListMonitorDetails() {
  const auto extras = PathDetails();
  std::vector<MonitorDetails> out;
  for (auto& m : EnumerateMonitors()) {
    MonitorDetails d;
    const auto it = extras.find(m.device_name);
    if (it != extras.end()) {
      d.refresh_hz = it->second.refresh_hz;
      d.connection = it->second.connection;
    }
    d.info = std::move(m);
    out.push_back(std::move(d));
  }
  return out;
}

std::vector<MonitorInfo> EnumerateMonitors() {
  std::vector<MonitorInfo> result;
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return result;
  const auto names = FriendlyNames();

  ComPtr<IDXGIAdapter1> adapter;
  for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    DXGI_ADAPTER_DESC1 adapter_desc{};
    adapter->GetDesc1(&adapter_desc);
    ComPtr<IDXGIOutput> output;
    for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
      DXGI_OUTPUT_DESC desc{};
      if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) continue;
      MonitorInfo info;
      info.device_name = desc.DeviceName;
      const auto it = names.find(info.device_name);
      info.friendly_name = it != names.end() && !it->second.empty() ? it->second : info.device_name;
      info.rect = desc.DesktopCoordinates;
      info.adapter_luid = adapter_desc.AdapterLuid;
      info.rotation = desc.Rotation;
      ComPtr<IDXGIOutput6> output6;
      if (SUCCEEDED(output.As(&output6))) {
        DXGI_OUTPUT_DESC1 desc1{};
        if (SUCCEEDED(output6->GetDesc1(&desc1))) info.hdr = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
      }
      MONITORINFO mi{sizeof(mi)};
      if (GetMonitorInfoW(desc.Monitor, &mi)) info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
      result.push_back(std::move(info));
    }
  }
  return result;
}

std::optional<MonitorInfo> FindMonitor(std::string_view setting) {
  auto monitors = EnumerateMonitors();
  const auto extras = PathDetails();
  std::vector<MonitorDesc> descs;
  for (const auto& m : monitors) {
    MonitorDesc d{Narrow(m.device_name), Narrow(m.friendly_name), m.primary};
    d.hdr = m.hdr;
    const auto it = extras.find(m.device_name);
    if (it != extras.end()) d.connection = it->second.connection;
    descs.push_back(std::move(d));
  }
  const auto index = SelectMonitor(descs, setting);
  if (!index) return std::nullopt;
  return monitors[*index];
}

std::optional<float> QuerySdrWhiteLevel(const MonitorInfo& monitor) {
  UINT32 path_count = 0;
  UINT32 mode_count = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) return std::nullopt;
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) !=
      ERROR_SUCCESS) {
    return std::nullopt;
  }
  paths.resize(path_count);
  for (const auto& path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
    if (monitor.device_name != source.viewGdiDeviceName) continue;

    DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
    white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
    white.header.size = sizeof(white);
    white.header.adapterId = path.targetInfo.adapterId;
    white.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS || white.SDRWhiteLevel == 0) return std::nullopt;
    return static_cast<float>(white.SDRWhiteLevel) / 1000.0f;
  }
  return std::nullopt;
}

HRESULT OpenOutput(const MonitorInfo& monitor, IDXGIAdapter1** adapter_out, IDXGIOutput5** output_out) {
  ComPtr<IDXGIFactory1> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) return hr;
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    ComPtr<IDXGIOutput> output;
    for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
      DXGI_OUTPUT_DESC desc{};
      if (FAILED(output->GetDesc(&desc)) || monitor.device_name != desc.DeviceName) continue;
      // Il device va creato sull'adapter che possiede l'output (sistemi ibridi).
      ComPtr<IDXGIOutput5> output5;
      hr = output.As(&output5);
      if (FAILED(hr)) return hr;
      *adapter_out = adapter.Detach();
      *output_out = output5.Detach();
      return S_OK;
    }
  }
  return DXGI_ERROR_NOT_FOUND;
}

}  // namespace oled
