#include "presenter.h"

#include "fullscreen_vs.h"
#include "logger.h"
#include "shift_constants.h"
#include "shift_ps.h"

namespace oled {

HRESULT Presenter::Init(ID3D11Device* device, HWND hwnd, UINT width, UINT height, DXGI_FORMAT format,
                        bool allow_tearing) {
  Shutdown();
  device_ = device;
  device_->GetImmediateContext(&context_);
  width_ = width;
  height_ = height;
  format_ = format;

  ComPtr<IDXGIDevice> dxgi_device;
  HRESULT hr = device_.As(&dxgi_device);
  if (FAILED(hr)) return hr;
  ComPtr<IDXGIAdapter> adapter;
  dxgi_device->GetAdapter(&adapter);
  ComPtr<IDXGIFactory2> factory;
  hr = adapter->GetParent(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) return hr;

  tearing_ = false;
  if (allow_tearing) {
    ComPtr<IDXGIFactory5> factory5;
    BOOL supported = FALSE;
    if (SUCCEEDED(factory.As(&factory5)) &&
        SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported, sizeof(supported)))) {
      tearing_ = supported != FALSE;
    }
  }

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = width;
  desc.Height = height;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
  ComPtr<IDXGISwapChain1> swap_chain1;
  hr = factory->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, &swap_chain1);
  if (FAILED(hr)) {
    LogError("CreateSwapChainForComposition fallito: {}", Hr(hr));
    return hr;
  }
  hr = swap_chain1.As(&swap_chain_);
  if (FAILED(hr)) return hr;

  // Passthrough: lo stesso colorspace del desktop, quindi colori identici.
  const DXGI_COLOR_SPACE_TYPE color_space = format == DXGI_FORMAT_R16G16B16A16_FLOAT
                                                ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
                                                : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  ComPtr<IDXGISwapChain3> swap_chain3;
  if (SUCCEEDED(swap_chain_.As(&swap_chain3))) {
    UINT support = 0;
    if (SUCCEEDED(swap_chain3->CheckColorSpaceSupport(color_space, &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
      hr = swap_chain3->SetColorSpace1(color_space);
      if (FAILED(hr)) LogWarn("SetColorSpace1 fallito: {}", Hr(hr));
    } else {
      LogWarn("Colorspace {} non supportato dalla swap chain", static_cast<int>(color_space));
    }
  }

  swap_chain_->SetMaximumFrameLatency(1);
  waitable_ = swap_chain_->GetFrameLatencyWaitableObject();

  ComPtr<ID3D11Texture2D> back_buffer;
  hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
  if (FAILED(hr)) return hr;
  hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &rtv_);
  if (FAILED(hr)) return hr;

  hr = device_->CreateVertexShader(g_fullscreen_vs, sizeof(g_fullscreen_vs), nullptr, &vs_);
  if (FAILED(hr)) return hr;
  hr = device_->CreatePixelShader(g_shift_ps, sizeof(g_shift_ps), nullptr, &ps_);
  if (FAILED(hr)) return hr;
  D3D11_BUFFER_DESC cb{};
  cb.ByteWidth = sizeof(ShiftConstants);
  cb.Usage = D3D11_USAGE_DEFAULT;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  hr = device_->CreateBuffer(&cb, nullptr, &constants_);
  if (FAILED(hr)) return hr;
  // Livelli dell'oscuramento letti con interpolazione bilineare: bordi sfumati.
  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  hr = device_->CreateSamplerState(&sd, &sampler_);
  if (FAILED(hr)) return hr;

  hr = DCompositionCreateDevice(dxgi_device.Get(), IID_PPV_ARGS(&dcomp_));
  if (FAILED(hr)) {
    LogError("DCompositionCreateDevice fallito: {}", Hr(hr));
    return hr;
  }
  hr = dcomp_->CreateTargetForHwnd(hwnd, TRUE, &target_);
  if (FAILED(hr)) {
    LogError("CreateTargetForHwnd fallito: {}", Hr(hr));
    return hr;
  }
  hr = dcomp_->CreateVisual(&visual_);
  if (FAILED(hr)) return hr;
  target_->SetRoot(visual_.Get());
  visible_ = false;
  hr = dcomp_->Commit();
  LogInfo("Swap chain {}x{} formato {} tearing={}", width, height, static_cast<int>(format), tearing_);
  return hr;
}

void Presenter::Shutdown() {
  if (visual_) visual_->SetContent(nullptr);
  if (dcomp_) dcomp_->Commit();
  visual_.Reset();
  target_.Reset();
  dcomp_.Reset();
  srv_.Reset();
  srv_source_ = nullptr;
  constants_.Reset();
  sampler_.Reset();
  ps_.Reset();
  vs_.Reset();
  rtv_.Reset();
  if (context_) {
    context_->ClearState();
    context_->Flush();
  }
  swap_chain_.Reset();
  if (waitable_) CloseHandle(waitable_);
  waitable_ = nullptr;
  context_.Reset();
  device_.Reset();
  visible_ = false;
}

HRESULT Presenter::Draw(ID3D11Texture2D* source, Offset offset, const DimDrawInputs& dim, const ShadeParams& shade,
                        const std::function<void(ID3D11Texture2D*)>& before_present) {
  if (!swap_chain_) return E_UNEXPECTED;
  if (source != srv_source_) {
    srv_.Reset();
    HRESULT hr = device_->CreateShaderResourceView(source, nullptr, &srv_);
    if (FAILED(hr)) return hr;
    srv_source_ = source;
  }

  const bool sdr = format_ != DXGI_FORMAT_R16G16B16A16_FLOAT;
  unsigned flags = sdr ? kShiftFlagSdr : 0;
  if (dim.levels) flags |= kShiftFlagDim | (dim.highlight ? kShiftFlagHighlight : 0);
  if (!ShadeIsNeutral(shade)) flags |= kShiftFlagShade;
  ShiftConstants constants{{offset.x, offset.y},
                           {width_ - 1, height_ - 1},
                           {static_cast<float>(dim.cols * 16), static_cast<float>(dim.rows * 16)},
                           dim.sdr_white,
                           flags};
  for (int i = 0; i < 4; ++i) constants.spread[i] = shade.spread[i];
  constants.brightness = shade.brightness;
  constants.vignette_strength = shade.vignette_strength;
  context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);

  const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
  ID3D11RenderTargetView* rtv = rtv_.Get();
  ID3D11ShaderResourceView* srvs[2] = {srv_.Get(), dim.levels};
  ID3D11Buffer* cbuf = constants_.Get();
  ID3D11SamplerState* sampler = sampler_.Get();
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  context_->RSSetViewports(1, &viewport);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(vs_.Get(), nullptr, 0);
  context_->PSSetShader(ps_.Get(), nullptr, 0);
  context_->PSSetShaderResources(0, 2, srvs);
  context_->PSSetSamplers(0, 1, &sampler);
  context_->PSSetConstantBuffers(0, 1, &cbuf);
  context_->Draw(3, 0);
  if (before_present) {
    ComPtr<ID3D11Resource> resource;
    rtv_->GetResource(&resource);
    ComPtr<ID3D11Texture2D> back_buffer;
    if (SUCCEEDED(resource.As(&back_buffer))) before_present(back_buffer.Get());
  }

  const HRESULT hr = tearing_ ? swap_chain_->Present(0, DXGI_PRESENT_ALLOW_TEARING) : swap_chain_->Present(1, 0);
  return hr == DXGI_STATUS_OCCLUDED ? S_OK : hr;
}

void Presenter::SetVisible(bool visible) {
  if (!visual_ || visible == visible_) return;
  visual_->SetContent(visible ? swap_chain_.Get() : nullptr);
  dcomp_->Commit();
  visible_ = visible;
}

}  // namespace oled
