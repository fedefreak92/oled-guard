// Cattura del desktop con Desktop Duplication (DuplicateOutput1: SDR e HDR FP16).
// I pixel restano sulla GPU: il frame viene copiato con CopyResource.
#pragma once

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <functional>

namespace oled {

class DesktopCapture {
 public:
  // FormatChanged: il frame reale ha formato/dimensioni diversi dalla cache; Format(),
  // Width() e Height() riportano ora quelli reali.
  enum class Status { NewFrame, NoChange, AccessLost, FormatChanged, Failed };

  HRESULT Start(ID3D11Device* device, IDXGIOutput5* output);
  void Stop();
  bool Running() const { return duplication_ != nullptr; }

  DXGI_FORMAT Format() const { return format_; }
  UINT Width() const { return width_; }
  UINT Height() const { return height_; }
  DXGI_MODE_ROTATION Rotation() const { return desc_.Rotation; }

  // Attende un frame fino a timeout_ms; se nuovo lo copia in `cache` (stesso formato
  // e dimensioni) e rilascia subito il frame. `before_copy` riceve il frame nuovo
  // mentre la cache contiene ancora il precedente (confronto dell'oscuramento).
  Status Acquire(UINT timeout_ms, ID3D11Texture2D* cache,
                 const std::function<void(ID3D11Texture2D* frame)>& before_copy = {});

  // Contatori per la diagnostica.
  unsigned frames_copied = 0;
  unsigned frames_skipped = 0;

 private:
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
  DXGI_OUTDUPL_DESC desc_{};
  DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
  UINT width_ = 0;
  UINT height_ = 0;
  bool first_frame_ = true;
};

const char* FormatName(DXGI_FORMAT format);

}  // namespace oled
