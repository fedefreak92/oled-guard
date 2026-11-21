// Utilita' per i test degli shader su device WARP (rasterizzatore software di Windows).
#pragma once

#include <DirectXPackedVector.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "dim_model.h"

namespace oled::gpu_test {

using Microsoft::WRL::ComPtr;

inline ComPtr<ID3D11Device> WarpDevice() {
  ComPtr<ID3D11Device> device;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                    ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, nullptr);
  return device;
}

inline float SrgbEncode(float l) {
  l = std::fmax(0.0f, std::fmin(1.0f, l));
  return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}
inline float SrgbDecode(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// Luminanza che la GPU vedra' dopo la codifica in BGRA8 (per alimentare il modello).
inline LumaImage QuantizeSdr(const LumaImage& img) {
  LumaImage out = img;
  for (float& v : out.luma) v = SrgbDecode(std::round(SrgbEncode(v) * 255.0f) / 255.0f);
  return out;
}

// Pixel grigi con luminanza = luma (in HDR moltiplicata per il bianco SDR).
inline std::vector<uint8_t> Pixels(const LumaImage& img, DXGI_FORMAT format, float white) {
  const size_t n = img.luma.size();
  if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
    std::vector<uint8_t> data(n * 8);
    auto* p = reinterpret_cast<DirectX::PackedVector::HALF*>(data.data());
    for (size_t i = 0; i < n; ++i) {
      const auto h = DirectX::PackedVector::XMConvertFloatToHalf(img.luma[i] * white);
      p[4 * i] = p[4 * i + 1] = p[4 * i + 2] = h;
      p[4 * i + 3] = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
    }
    return data;
  }
  std::vector<uint8_t> data(n * 4);
  for (size_t i = 0; i < n; ++i) {
    const auto b = static_cast<uint8_t>(std::lround(SrgbEncode(img.luma[i]) * 255.0f));
    data[4 * i] = data[4 * i + 1] = data[4 * i + 2] = b;
    data[4 * i + 3] = 255;
  }
  return data;
}

inline UINT BytesPerPixel(DXGI_FORMAT format) { return format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4; }

inline ComPtr<ID3D11Texture2D> Texture(ID3D11Device* device, int w, int h, DXGI_FORMAT format, UINT bind) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = w;
  desc.Height = h;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = bind;
  ComPtr<ID3D11Texture2D> tex;
  device->CreateTexture2D(&desc, nullptr, &tex);
  return tex;
}

inline void Upload(ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, const LumaImage& img, DXGI_FORMAT format,
                   float white) {
  const auto data = Pixels(img, format, white);
  ctx->UpdateSubresource(tex, 0, nullptr, data.data(), img.width * BytesPerPixel(format), 0);
}

// Copia bloccante (solo nei test) di una texture in memoria: righe compatte.
inline std::vector<uint8_t> ReadBack(ID3D11Device* device, ID3D11Texture2D* tex) {
  ComPtr<ID3D11DeviceContext> ctx;
  device->GetImmediateContext(&ctx);
  D3D11_TEXTURE2D_DESC desc{};
  tex->GetDesc(&desc);
  const UINT bpp = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, &staging);
  ctx->CopyResource(staging.Get(), tex);
  std::vector<uint8_t> out(static_cast<size_t>(desc.Width) * desc.Height * bpp);
  D3D11_MAPPED_SUBRESOURCE map{};
  if (SUCCEEDED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) {
    for (UINT y = 0; y < desc.Height; ++y)
      std::memcpy(out.data() + static_cast<size_t>(y) * desc.Width * bpp,
                  static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch, desc.Width * bpp);
    ctx->Unmap(staging.Get(), 0);
  }
  return out;
}

inline std::vector<float> ReadFloats(ID3D11Device* device, ID3D11ShaderResourceView* srv) {
  ComPtr<ID3D11Resource> res;
  srv->GetResource(&res);
  ComPtr<ID3D11Texture2D> tex;
  res.As(&tex);
  const auto bytes = ReadBack(device, tex.Get());
  std::vector<float> out(bytes.size() / 4);
  std::memcpy(out.data(), bytes.data(), bytes.size());
  return out;
}

}  // namespace oled::gpu_test
