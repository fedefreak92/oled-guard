#include "diagnostics.h"

#include <wrl/client.h>

#include "capture.h"
#include "d3d_device.h"
#include "logger.h"
#include "monitors.h"
#include "text.h"

using Microsoft::WRL::ComPtr;

namespace oled {

int RunListMonitors(std::string_view monitor_setting) {
  const auto monitors = EnumerateMonitors();
  LogInfo("Monitor trovati: {}", monitors.size());
  for (const auto& m : monitors) {
    LogInfo("  {} \"{}\" {}x{} @({},{}) primario={} rotazione={}", Narrow(m.device_name), Narrow(m.friendly_name),
            m.rect.right - m.rect.left, m.rect.bottom - m.rect.top, m.rect.left, m.rect.top, m.primary,
            static_cast<int>(m.rotation));
  }
  const auto chosen = FindMonitor(monitor_setting);
  if (!chosen) {
    LogError("Nessun monitor corrisponde a \"{}\"", monitor_setting);
    return 2;
  }
  LogInfo("Monitor scelto (\"{}\"): {}", monitor_setting, Narrow(chosen->friendly_name));

  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIOutput5> output;
  HRESULT hr = OpenOutput(*chosen, &adapter, &output);
  if (FAILED(hr)) {
    LogError("OpenOutput fallito: {}", Hr(hr));
    return 3;
  }
  DXGI_ADAPTER_DESC1 ad{};
  adapter->GetDesc1(&ad);
  LogInfo("Adapter: {}", Narrow(ad.Description));
  ComPtr<IDXGIOutput6> output6;
  if (SUCCEEDED(output.As(&output6))) {
    DXGI_OUTPUT_DESC1 d1{};
    output6->GetDesc1(&d1);
    LogInfo("Colorspace output: {} ({}), bit per colore {}, luminanza max {} nit", static_cast<int>(d1.ColorSpace),
            d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "HDR attivo" : "SDR", d1.BitsPerColor,
            d1.MaxLuminance);
  }

  ComPtr<ID3D11Device> device;
  hr = CreateDeviceForAdapter(adapter.Get(), &device);
  if (FAILED(hr)) {
    LogError("D3D11CreateDevice fallito: {}", Hr(hr));
    return 4;
  }
  DesktopCapture capture;
  hr = capture.Start(device.Get(), output.Get());
  if (FAILED(hr)) {
    LogError("DuplicateOutput1 fallito: {}", Hr(hr));
    return 5;
  }
  LogInfo("Cattura: {}x{} formato {} rotazione {}", capture.Width(), capture.Height(), FormatName(capture.Format()),
          static_cast<int>(capture.Rotation()));
  ComPtr<ID3D11Texture2D> cache;
  hr = CreateCacheTexture(device.Get(), capture.Width(), capture.Height(), capture.Format(), &cache);
  if (FAILED(hr)) {
    LogError("Texture di appoggio non creata: {}", Hr(hr));
    return 6;
  }
  auto status = DesktopCapture::Status::NoChange;
  for (int i = 0; i < 5 && status == DesktopCapture::Status::NoChange; ++i) {
    status = capture.Acquire(500, cache.Get());
    LogInfo("Acquire #{}: stato {}", i, static_cast<int>(status));
  }
  LogInfo("Primo frame: {}", status == DesktopCapture::Status::NewFrame ? "ricevuto" : "non ricevuto");
  return status == DesktopCapture::Status::NewFrame ? 0 : 7;
}

}  // namespace oled
