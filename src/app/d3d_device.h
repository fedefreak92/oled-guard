// Creazione del device D3D11 sull'adapter che possiede l'output catturato.
#pragma once

#include <d3d11.h>
#include <dxgi1_6.h>

namespace oled {

HRESULT CreateDeviceForAdapter(IDXGIAdapter1* adapter, ID3D11Device** device);

// Texture di appoggio con lo stesso formato/dimensioni del desktop, leggibile dallo shader.
HRESULT CreateCacheTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
                           ID3D11Texture2D** texture);

}  // namespace oled
