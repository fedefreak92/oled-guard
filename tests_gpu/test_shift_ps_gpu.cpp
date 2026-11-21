// Pixel shader dell'overlay: spostamento + oscuramento, eseguito su WARP.
#include <cmath>
#include <cstring>
#include <random>

#include "gpu_test_util.h"  // prima degli header degli shader (tipo BYTE)
#include "fullscreen_vs.h"
#include "shift_constants.h"
#include "shift_ps.h"
#include "test_framework.h"

using namespace oled::gpu_test;
using oled::ShiftConstants;

namespace {

constexpr int kW = 64;
constexpr int kH = 48;  // 4x3 celle
constexpr DXGI_FORMAT kHdr = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kSdr = DXGI_FORMAT_B8G8R8A8_UNORM;

std::vector<uint8_t> RandomPixels(DXGI_FORMAT format) {
  std::mt19937 rng(42);
  std::vector<uint8_t> data(static_cast<size_t>(kW) * kH * BytesPerPixel(format));
  if (format == kHdr) {
    auto* h = reinterpret_cast<DirectX::PackedVector::HALF*>(data.data());
    std::uniform_real_distribution<float> d(0.0f, 4.0f);
    for (size_t i = 0; i < data.size() / 2; ++i) h[i] = DirectX::PackedVector::XMConvertFloatToHalf(d(rng));
  } else {
    for (auto& b : data) b = static_cast<uint8_t>(rng() & 0xff);
  }
  return data;
}

// Disegna `source` con lo shader dell'overlay; livelli uniformi `level` (o nessun dimming).
std::vector<uint8_t> Draw(DXGI_FORMAT format, const std::vector<uint8_t>& pixels, float level, unsigned flags) {
  auto device = WarpDevice();
  ComPtr<ID3D11DeviceContext> ctx;
  device->GetImmediateContext(&ctx);
  auto src = Texture(device.Get(), kW, kH, format, D3D11_BIND_SHADER_RESOURCE);
  ctx->UpdateSubresource(src.Get(), 0, nullptr, pixels.data(), kW * BytesPerPixel(format), 0);
  auto rt = Texture(device.Get(), kW, kH, format, D3D11_BIND_RENDER_TARGET);

  D3D11_TEXTURE2D_DESC ld{};
  ld.Width = 4;
  ld.Height = 3;
  ld.MipLevels = 1;
  ld.ArraySize = 1;
  ld.Format = DXGI_FORMAT_R32_FLOAT;
  ld.SampleDesc.Count = 1;
  ld.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  const std::vector<float> lv(12, level);
  D3D11_SUBRESOURCE_DATA init{lv.data(), 4 * 4, 0};
  ComPtr<ID3D11Texture2D> levels;
  device->CreateTexture2D(&ld, &init, &levels);

  ComPtr<ID3D11ShaderResourceView> src_srv, lv_srv;
  device->CreateShaderResourceView(src.Get(), nullptr, &src_srv);
  device->CreateShaderResourceView(levels.Get(), nullptr, &lv_srv);
  ComPtr<ID3D11RenderTargetView> rtv;
  device->CreateRenderTargetView(rt.Get(), nullptr, &rtv);
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  device->CreateVertexShader(g_fullscreen_vs, sizeof(g_fullscreen_vs), nullptr, &vs);
  device->CreatePixelShader(g_shift_ps, sizeof(g_shift_ps), nullptr, &ps);
  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sd, &sampler);

  const ShiftConstants c{{0, 0}, {kW - 1, kH - 1}, {64.0f, 48.0f}, format == kHdr ? 2.5f : 1.0f, flags};
  D3D11_BUFFER_DESC cbd{};
  cbd.ByteWidth = sizeof(c);
  cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  D3D11_SUBRESOURCE_DATA cbi{&c, 0, 0};
  ComPtr<ID3D11Buffer> cb;
  device->CreateBuffer(&cbd, &cbi, &cb);

  const D3D11_VIEWPORT vp{0, 0, kW, kH, 0, 1};
  ID3D11ShaderResourceView* srvs[2] = {src_srv.Get(), lv_srv.Get()};
  ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
  ID3D11Buffer* cbs[1] = {cb.Get()};
  ID3D11SamplerState* samplers[1] = {sampler.Get()};
  ctx->OMSetRenderTargets(1, rtvs, nullptr);
  ctx->RSSetViewports(1, &vp);
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(vs.Get(), nullptr, 0);
  ctx->PSSetShader(ps.Get(), nullptr, 0);
  ctx->PSSetShaderResources(0, 2, srvs);
  ctx->PSSetConstantBuffers(0, 1, cbs);
  ctx->PSSetSamplers(0, 1, samplers);
  ctx->Draw(3, 0);
  return ReadBack(device.Get(), rt.Get());
}

constexpr unsigned kSdrFlag = 1;
constexpr unsigned kDimFlag = 4;

}  // namespace

TEST(shift_ps_level_zero_is_bit_exact) {
  for (const DXGI_FORMAT f : {kHdr, kSdr}) {
    const auto pixels = RandomPixels(f);
    const unsigned sdr = f == kSdr ? kSdrFlag : 0;
    CHECK(Draw(f, pixels, 0.0f, sdr | kDimFlag) == pixels);
    CHECK(Draw(f, pixels, 0.5f, sdr) == pixels);  // oscuramento non attivo
  }
}

TEST(shift_ps_halves_linear_hdr) {
  const auto pixels = RandomPixels(kHdr);
  const auto out = Draw(kHdr, pixels, 0.5f, kDimFlag);
  const auto* in_h = reinterpret_cast<const DirectX::PackedVector::HALF*>(pixels.data());
  const auto* out_h = reinterpret_cast<const DirectX::PackedVector::HALF*>(out.data());
  int bad = 0;
  for (size_t i = 0; i < pixels.size() / 2; ++i) {
    const float a = DirectX::PackedVector::XMConvertHalfToFloat(in_h[i]);
    const float b = DirectX::PackedVector::XMConvertHalfToFloat(out_h[i]);
    const float expected = i % 4 == 3 ? a : a * 0.5f;  // alfa invariata
    if (std::fabs(b - expected) > 1e-3f * a + 1e-3f) ++bad;
  }
  CHECK_EQ(bad, 0);
}

TEST(shift_ps_sdr_dims_in_linear) {
  std::vector<uint8_t> white(static_cast<size_t>(kW) * kH * 4, 255);
  const auto out = Draw(kSdr, white, 0.5f, kSdrFlag | kDimFlag);
  CHECK(std::abs(static_cast<int>(out[0]) - 188) <= 1);  // sRGB(0,5) = 0,7354
  CHECK(std::abs(static_cast<int>(out[1]) - 188) <= 1);
  CHECK(std::abs(static_cast<int>(out[2]) - 188) <= 1);
  CHECK_EQ(static_cast<int>(out[3]), 255);
}
