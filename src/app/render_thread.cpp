#include "render_thread.h"

#include <algorithm>
#include <climits>

#include "capture.h"
#include "clock.h"
#include "d3d_device.h"
#include "logger.h"
#include "presenter.h"
#include "text.h"

using Microsoft::WRL::ComPtr;

namespace oled {
namespace {

constexpr int kMaxFailures = 5;
constexpr UINT kMaxAcquireTimeoutMs = 500;
constexpr double kMaxUnavailableMs = 30000;

// Errori di avvio della duplicazione che passano da soli (UAC, lock, cambio modalita').
bool IsTransient(HRESULT hr) {
  return hr == E_ACCESSDENIED || hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_SESSION_DISCONNECTED ||
         hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || hr == DXGI_ERROR_WAIT_TIMEOUT;
}

bool IsSupportedRotation(DXGI_MODE_ROTATION r) {
  return r == DXGI_MODE_ROTATION_IDENTITY || r == DXGI_MODE_ROTATION_UNSPECIFIED;
}

}  // namespace

bool RenderThread::Start(const RenderParams& params) {
  Stop();
  params_ = params;
  stop_ = false;
  failures_ = 0;
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stop_event_) return false;
  thread_ = std::thread([this] { Run(); });
  return true;
}

void RenderThread::Stop() {
  if (thread_.joinable()) {
    stop_ = true;
    SetEvent(stop_event_);
    thread_.join();
  }
  if (stop_event_) {
    CloseHandle(stop_event_);
    stop_event_ = nullptr;
  }
}

void RenderThread::Post(RenderEvent event) {
  // La generazione permette all'App di scartare eventi di un thread gia' fermato.
  PostMessageW(params_.main, WM_APP_RENDER_EVENT, static_cast<WPARAM>(event), static_cast<LPARAM>(params_.generation));
}

void RenderThread::Beat() {
  if (params_.guard) params_.guard->BeatRender();
}

void RenderThread::SleepMs(DWORD ms) {
  const double end = NowMs() + ms;
  while (!stop_) {
    Beat();
    const double left = end - NowMs();
    if (left <= 0) break;
    WaitForSingleObject(stop_event_, static_cast<DWORD>(std::min(left, 250.0)));
  }
}

void RenderThread::Run() {
  SetThreadDescription(GetCurrentThread(), L"OledGuard render");
  // Priorita' alta: il ridisegno e' sul percorso di latenza di tutto il desktop.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  while (!stop_) {
    if (RunSession() == SessionEnd::Stop) break;
    if (++failures_ >= kMaxFailures) {
      LogError("Render: {} errori consecutivi, overlay disattivato", failures_);
      Post(RenderEvent::CaptureFailed);
      break;
    }
    SleepMs(static_cast<DWORD>(std::min(2000, 100 << failures_)));
  }
}

RenderThread::SessionEnd RenderThread::RunSession() {
  Beat();
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIOutput5> output;
  HRESULT hr = OpenOutput(params_.monitor, &adapter, &output);
  if (FAILED(hr)) {
    LogWarn("Render: output {} non trovato: {}", Narrow(params_.monitor.device_name), Hr(hr));
    return SessionEnd::Failed;
  }
  ComPtr<ID3D11Device> device;
  hr = CreateDeviceForAdapter(adapter.Get(), &device);
  if (FAILED(hr)) {
    LogWarn("Render: D3D11CreateDevice fallito: {}", Hr(hr));
    return SessionEnd::Failed;
  }

  DesktopCapture capture;
  Presenter presenter;
  ComPtr<ID3D11Texture2D> cache;
  DWORD backoff_ms = 100;
  double unavailable_since = -1;  // inizio degli errori "transitori" consecutivi
  bool have_frame = false;
  bool shown = false;
  Offset last_drawn{INT_MIN, INT_MIN};
  double stats_since = NowMs();

  // Nasconde l'overlay e disarma subito il watchdog: le operazioni che seguono
  // (ricreazione di device o duplicazione) possono durare piu' del limite.
  const auto hide = [&] {
    presenter.SetVisible(false);
    if (shown) {
      if (params_.guard) params_.guard->SetOverlayActive(false);
      Post(RenderEvent::CaptureLost);
    }
    shown = false;
  };

  // Cache e swap chain con il formato e le dimensioni reali della cattura.
  const auto reshape = [&]() -> bool {
    hide();
    presenter.Shutdown();
    cache.Reset();
    hr = CreateCacheTexture(device.Get(), capture.Width(), capture.Height(), capture.Format(), &cache);
    if (FAILED(hr)) {
      LogWarn("Render: texture di appoggio non creata: {}", Hr(hr));
      return false;
    }
    hr = presenter.Init(device.Get(), params_.overlay, capture.Width(), capture.Height(), capture.Format(),
                        params_.allow_tearing);
    if (FAILED(hr)) {
      LogWarn("Render: presenter non inizializzato: {}", Hr(hr));
      return false;
    }
    have_frame = false;
    return true;
  };

  while (!stop_) {
    Beat();

    if (!capture.Running()) {
      hr = capture.Start(device.Get(), output.Get());
      if (FAILED(hr)) {
        if (!IsTransient(hr)) {
          LogWarn("Render: DuplicateOutput1 fallito: {}", Hr(hr));
          return SessionEnd::Failed;
        }
        // Lock e UAC mettono in pausa l'app entro pochi istanti; se la duplicazione resta
        // indisponibile a lungo (es. altre app di cattura occupano tutti gli slot) non
        // e' piu' un caso transitorio e la tray deve dirlo.
        if (unavailable_since < 0) unavailable_since = NowMs();
        if (NowMs() - unavailable_since > kMaxUnavailableMs) {
          LogWarn("Render: duplicazione non disponibile da {} s ({})", kMaxUnavailableMs / 1000, Hr(hr));
          Post(RenderEvent::CaptureFailed);
          return SessionEnd::Stop;
        }
        LogDebug("Render: duplicazione non disponibile ({}), nuovo tentativo tra {} ms", Hr(hr), backoff_ms);
        SleepMs(backoff_ms);
        backoff_ms = std::min<DWORD>(backoff_ms * 2, 2000);
        continue;
      }
      backoff_ms = 100;
      unavailable_since = -1;
      if (!IsSupportedRotation(capture.Rotation())) {
        LogWarn("Render: display ruotato ({}), non supportato", static_cast<int>(capture.Rotation()));
        Post(RenderEvent::DisplayRotated);
        return SessionEnd::Stop;
      }
      const bool needs_reshape = !presenter.Ready() || presenter.Width() != capture.Width() ||
                                 presenter.Height() != capture.Height() || presenter.Format() != capture.Format();
      if (needs_reshape && !reshape()) return SessionEnd::Failed;
      have_frame = false;
      LogInfo("Cattura avviata: {}x{} {}", capture.Width(), capture.Height(), FormatName(capture.Format()));
    }

    double now = NowMs();
    UINT timeout = kMaxAcquireTimeoutMs;
    if (params_.shift) {
      const double next = params_.shift->NextChangeAt(now);
      timeout = static_cast<UINT>(std::clamp(next - now, 0.0, static_cast<double>(kMaxAcquireTimeoutMs)));
    }

    const auto status = capture.Acquire(timeout, cache.Get());
    if (status == DesktopCapture::Status::AccessLost) {
      // Nessuna immagine vecchia a schermo: si nasconde subito l'overlay.
      hide();
      capture.Stop();
      LogInfo("Render: accesso alla cattura perso, ricreo la duplicazione");
      continue;
    }
    if (status == DesktopCapture::Status::FormatChanged) {
      // Il frame reale non corrisponde a quanto dichiarato: si riallinea tutto.
      LogInfo("Render: frame {}x{} formato {} diverso dalla cache, riallineo", capture.Width(), capture.Height(),
              static_cast<int>(capture.Format()));
      if (!reshape()) {
        hide();
        return SessionEnd::Failed;
      }
      continue;
    }
    if (status == DesktopCapture::Status::Failed) {
      hide();
      return SessionEnd::Failed;
    }
    const bool new_frame = status == DesktopCapture::Status::NewFrame;
    if (new_frame) have_frame = true;
    if (!have_frame) continue;

    now = NowMs();
    const Offset offset = params_.shift ? params_.shift->OffsetAt(now) : Offset{};
    // Si presenta solo se c'e' qualcosa di nuovo da mostrare.
    if (new_frame || !(offset == last_drawn)) {
      WaitForSingleObjectEx(presenter.FrameWaitable(), 100, TRUE);
      hr = presenter.Draw(cache.Get(), offset);
      if (FAILED(hr)) {
        LogWarn("Render: Present fallito: {}", Hr(hr));
        hide();
        return SessionEnd::Failed;
      }
      if (!(offset == last_drawn)) LogDebug("Offset disegnato ({},{})", offset.x, offset.y);
      last_drawn = offset;
      if (!shown) {
        presenter.SetVisible(true);
        shown = true;
        failures_ = 0;
        Post(RenderEvent::FirstFrameShown);
      }
    }

    if (now - stats_since >= 10000) {
      LogDebug("Render: ultimi 10 s {} frame copiati, {} ignorati", capture.frames_copied, capture.frames_skipped);
      capture.frames_copied = 0;
      capture.frames_skipped = 0;
      stats_since = now;
    }
  }
  presenter.SetVisible(false);
  return SessionEnd::Stop;
}

}  // namespace oled
