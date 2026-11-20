#include "d3d_device.h"

namespace oled {

HRESULT CreateDeviceForAdapter(IDXGIAdapter1* adapter, ID3D11Device** device) {
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  // Con un adapter esplicito il driver type deve essere UNKNOWN.
  return D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                           ARRAYSIZE(levels), D3D11_SDK_VERSION, device, nullptr, nullptr);
}

HRESULT CreateCacheTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
                           ID3D11Texture2D** texture) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  return device->CreateTexture2D(&desc, nullptr, texture);
}

}  // namespace oled
