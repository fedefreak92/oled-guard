#include "dim_diagnostics.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

#include "logger.h"
#include "text.h"

namespace oled {
namespace {

uint8_t ToSrgb8(float linear) {
  linear = std::clamp(linear, 0.0f, 1.0f);
  const float c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
  return static_cast<uint8_t>(std::lround(c * 255.0f));
}

void WriteBmp(const std::filesystem::path& file, int w, int h, const std::vector<uint8_t>& bgr) {
  const uint32_t row = static_cast<uint32_t>((w * 3 + 3) & ~3);
  const uint32_t data_size = row * h;
  uint8_t header[54] = {'B', 'M'};
  const auto put32 = [&](int at, uint32_t v) {
    for (int i = 0; i < 4; ++i) header[at + i] = static_cast<uint8_t>(v >> (8 * i));
  };
  put32(2, 54 + data_size);
  put32(10, 54);
  put32(14, 40);
  put32(18, static_cast<uint32_t>(w));
  put32(22, static_cast<uint32_t>(h));
  header[26] = 1;
  header[28] = 24;
  put32(34, data_size);
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<const char*>(header), sizeof(header));
  std::vector<uint8_t> line(row, 0);
  for (int y = h - 1; y >= 0; --y) {  // BMP dal basso verso l'alto
    std::copy_n(bgr.begin() + static_cast<size_t>(y) * w * 3, w * 3, line.begin());
    out.write(reinterpret_cast<const char*>(line.data()), row);
  }
}

}  // namespace

void FrameSnapshot::Request(ID3D11Device* device, ID3D11Texture2D* source) {
  if (pending_) return;
  D3D11_TEXTURE2D_DESC desc{};
  source->GetDesc(&desc);
  if (!staging_ || device_.Get() != device) {
    device_ = device;
    device_->GetImmediateContext(&context_);
    staging_.Reset();
  }
  if (staging_) {
    D3D11_TEXTURE2D_DESC have{};
    staging_->GetDesc(&have);
    if (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format) staging_.Reset();
  }
  if (!staging_) {
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging_))) return;
  }
  context_->CopyResource(staging_.Get(), source);
  context_->Flush();
  pending_ = true;
}

bool FrameSnapshot::TryWrite(const std::filesystem::path& file, float white) {
  if (!pending_) return false;
  D3D11_MAPPED_SUBRESOURCE map{};
  if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map))) return false;
  D3D11_TEXTURE2D_DESC desc{};
  staging_->GetDesc(&desc);
  const int w = static_cast<int>(desc.Width), h = static_cast<int>(desc.Height);
  const bool hdr = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
  // Copia grezza subito, conversione e scrittura su un altro thread.
  const size_t bpp = hdr ? 8 : 4;
  std::vector<uint8_t> raw(static_cast<size_t>(w) * h * bpp);
  for (int y = 0; y < h; ++y) {
    std::memcpy(raw.data() + static_cast<size_t>(y) * w * bpp,
                static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch, w * bpp);
  }
  context_->Unmap(staging_.Get(), 0);
  pending_ = false;
  std::thread([file, w, h, hdr, white, raw = std::move(raw)] {
    std::vector<uint8_t> bgr(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
      if (hdr) {
        const auto* px = reinterpret_cast<const DirectX::PackedVector::HALF*>(raw.data()) + i * 4;
        for (int c = 0; c < 3; ++c) {
          const float v = DirectX::PackedVector::XMConvertHalfToFloat(px[c]) / (white > 0 ? white : 1.0f);
          bgr[i * 3 + (2 - c)] = ToSrgb8(v);  // RGB -> BGR
        }
      } else {
        for (int c = 0; c < 3; ++c) bgr[i * 3 + c] = raw[i * 4 + c];  // BGRA gia' in ordine BGR
      }
    }
    WriteBmp(file, w, h, bgr);
    LogInfo("Diagnostica: istantanea {}", Narrow(file.filename().wstring()));
  }).detach();
  return true;
}

}  // namespace oled
