// Swap chain flip-model per DirectComposition e disegno del desktop spostato.
// Formato e colorspace seguono la cattura: FP16 scRGB in HDR, BGRA8 sRGB in SDR.
#pragma once

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <functional>

#include "offset.h"
#include "shade_model.h"

namespace oled {

// Ingressi dell'oscuramento per un disegno; levels == nullptr: nessun oscuramento.
struct DimDrawInputs {
  ID3D11ShaderResourceView* levels = nullptr;
  UINT cols = 0;
  UINT rows = 0;
  float sdr_white = 1;  // valore del bianco nel formato di uscita (1 in SDR)
  bool highlight = false;
};

class Presenter {
 public:
  ~Presenter() { Shutdown(); }

  HRESULT Init(ID3D11Device* device, HWND hwnd, UINT width, UINT height, DXGI_FORMAT format, bool allow_tearing);
  void Shutdown();
  bool Ready() const { return swap_chain_ != nullptr; }
  UINT Width() const { return width_; }
  UINT Height() const { return height_; }
  DXGI_FORMAT Format() const { return format_; }

  HANDLE FrameWaitable() const { return waitable_; }
  // before_present riceve il back buffer appena disegnato (diagnostica).
  HRESULT Draw(ID3D11Texture2D* source, Offset offset, const DimDrawInputs& dim = {}, const ShadeParams& shade = {},
               const std::function<void(ID3D11Texture2D*)>& before_present = {});
  // Attacca/stacca la swap chain dal visual: staccata, la finestra non mostra nulla.
  void SetVisible(bool visible);

 private:
  template <class T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGISwapChain2> swap_chain_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  ComPtr<ID3D11VertexShader> vs_;
  ComPtr<ID3D11PixelShader> ps_;
  ComPtr<ID3D11Buffer> constants_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11ShaderResourceView> srv_;
  ID3D11Texture2D* srv_source_ = nullptr;
  ComPtr<IDCompositionDevice> dcomp_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual> visual_;
  HANDLE waitable_ = nullptr;
  UINT width_ = 0;
  UINT height_ = 0;
  DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
  bool tearing_ = false;
  bool visible_ = false;
};

}  // namespace oled
