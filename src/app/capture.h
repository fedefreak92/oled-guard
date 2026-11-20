// Cattura del desktop con Desktop Duplication (DuplicateOutput1: SDR e HDR FP16).
// I pixel restano sulla GPU: il frame viene copiato con CopyResource.
#pragma once

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace oled {

class DesktopCapture {
 public:
  enum class Status { NewFrame, NoChange, AccessLost, Failed };

  HRESULT Start(ID3D11Device* device, IDXGIOutput5* output);
  void Stop();
  bool Running() const { return duplication_ != nullptr; }

  DXGI_FORMAT Format() const { return desc_.ModeDesc.Format; }
  UINT Width() const { return desc_.ModeDesc.Width; }
  UINT Height() const { return desc_.ModeDesc.Height; }
  DXGI_MODE_ROTATION Rotation() const { return desc_.Rotation; }

  // Attende un frame fino a timeout_ms; se nuovo lo copia in `cache` (stesso formato
  // e dimensioni) e rilascia subito il frame.
  Status Acquire(UINT timeout_ms, ID3D11Texture2D* cache);

  // Contatori per la diagnostica.
  unsigned frames_copied = 0;
  unsigned frames_skipped = 0;

 private:
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
  DXGI_OUTDUPL_DESC desc_{};
  bool first_frame_ = true;
};

const char* FormatName(DXGI_FORMAT format);

}  // namespace oled
