#include "capture.h"

#include "logger.h"

using Microsoft::WRL::ComPtr;

namespace oled {

const char* FormatName(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT (HDR)";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM (SDR)";
    default: return "altro";
  }
}

HRESULT DesktopCapture::Start(ID3D11Device* device, IDXGIOutput5* output) {
  Stop();
  device->GetImmediateContext(&context_);
  const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
  const HRESULT hr = output->DuplicateOutput1(device, 0, ARRAYSIZE(formats), formats, &duplication_);
  if (FAILED(hr)) {
    duplication_.Reset();
    return hr;
  }
  duplication_->GetDesc(&desc_);
  first_frame_ = true;
  return S_OK;
}

void DesktopCapture::Stop() {
  duplication_.Reset();
  context_.Reset();
  desc_ = {};
}

DesktopCapture::Status DesktopCapture::Acquire(UINT timeout_ms, ID3D11Texture2D* cache) {
  if (!duplication_) return Status::Failed;
  DXGI_OUTDUPL_FRAME_INFO info{};
  ComPtr<IDXGIResource> resource;
  HRESULT hr = duplication_->AcquireNextFrame(timeout_ms, &info, &resource);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) return Status::NoChange;
  if (hr == DXGI_ERROR_ACCESS_LOST) return Status::AccessLost;
  if (FAILED(hr)) {
    LogWarn("AcquireNextFrame fallito: {}", Hr(hr));
    return Status::Failed;
  }

  if (first_frame_) {
    LogDebug("AcquireNextFrame iniziale: LastPresentTime={} AccumulatedFrames={} Metadata={} risorsa={}",
            info.LastPresentTime.QuadPart, info.AccumulatedFrames, info.TotalMetadataBufferSize,
            resource != nullptr);
  }
  Status status = Status::NoChange;
  // LastPresentTime == 0: e' cambiato solo il puntatore, che non catturiamo.
  // Nessun metadata: nessuna regione cambiata (es. aggiornamenti dell'overlay stesso).
  const bool image_changed = info.LastPresentTime.QuadPart != 0 && (info.TotalMetadataBufferSize > 0 || first_frame_);
  if (image_changed) {
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(resource.As(&texture))) {
      context_->CopyResource(cache, texture.Get());
      status = Status::NewFrame;
      first_frame_ = false;
      ++frames_copied;
    }
  } else {
    ++frames_skipped;
  }
  resource.Reset();
  hr = duplication_->ReleaseFrame();
  if (hr == DXGI_ERROR_ACCESS_LOST) return Status::AccessLost;
  return status;
}

}  // namespace oled
