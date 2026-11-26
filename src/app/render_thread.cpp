#include "render_thread.h"

#include <algorithm>
#include <climits>
#include <fstream>
#include <limits>
#include <optional>

#include "capture.h"
#include "clock.h"
#include "d3d_device.h"
#include "diagnostics_report.h"
#include "dim_diagnostics.h"
#include "dim_stats.h"
#include "dim_schedule.h"
#include "dimmer.h"
#include "logger.h"
#include "away_model.h"
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

std::string RenderThread::StatsLine() const {
  RenderCounters c;
  c.last_copied = last_copied_.load();
  c.last_skipped = last_skipped_.load();
  c.total_copied = total_copied_.load();
  c.total_skipped = total_skipped_.load();
  c.sessions = sessions_.load();
  c.measured_s = measured_ms_.load() / 1000;
  return RenderStatsLine(c);
}

RenderThread::SessionEnd RenderThread::RunSession() {
  Beat();
  ++sessions_;
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
  Dimmer dimmer;
  DimRedrawSchedule dim_schedule;
  bool dim_ok = false;
  uint64_t dim_version = 0;
  bool highlight_on = false;
  double readback_requested_ms = -1e9;
  std::vector<float> readback_levels;
  // Diagnostica (--dim-diagnostics).
  double diag_stats_at = NowMs() + 5000;
  double diag_snap_at = NowMs() + 20000;
  int diag_snaps = 0;
  bool diag_want_snap = false;
  FrameSnapshot snap_in, snap_out;
  std::filesystem::path snap_in_file, snap_out_file;
  DimDebugGrids diag_grids;
  uint64_t zones_applied = 0;
  // All'uscita (pausa, errore) si salva lo stato dell'oscuramento per riprenderlo dopo.
  struct SaveOnExit {
    Dimmer& dimmer;
    SharedDim* shared;
    ~SaveOnExit() {
      DimSavedState state;
      if (shared && dimmer.Ready() && dimmer.SaveState(NowMs(), state)) shared->SaveState(std::move(state));
    }
  } save_on_exit{dimmer, params_.dim};
  ComPtr<ID3D11Texture2D> cache;
  DWORD backoff_ms = 100;
  double unavailable_since = -1;  // inizio degli errori "transitori" consecutivi
  bool have_frame = false;
  bool shown = false;
  Offset last_drawn{INT_MIN, INT_MIN};
  ShadeParams last_shade;
  uint64_t shade_version = 0;
  double summary_requested_ms = -1e9;
  // Luminosita' effettiva: base x rampa della modalita' assente.
  const auto shade_at = [&](double t, const ShadeInputs& in) {
    ShadeParams p = in.base;
    p.brightness *= static_cast<float>(AwayFactorAt(t, in.away_since_ms, in.away_target));
    return p;
  };
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

  // Nuova griglia con la dimensione e il formato correnti. Le celle rimaste uguali
  // (stessa firma dei pixel) riprendono l'eta' che avevano prima della pausa.
  const auto reset_dimmer = [&] {
    dim_ok = false;
    if (!params_.dim) return;
    std::optional<DimSavedState> saved;
    DimSavedState state;
    if (dimmer.Ready() && dimmer.SaveState(NowMs(), state)) saved = std::move(state);
    if (!saved) saved = params_.dim->TakeSavedState();
    const HRESULT dim_hr = dimmer.Init(device.Get(), capture.Width(), capture.Height(), capture.Format(), NowMs(),
                                       saved ? &*saved : nullptr);
    dim_ok = SUCCEEDED(dim_hr);
    zones_applied = 0;  // la griglia nuova parte tutta "app": si riapplicano le zone
    if (!dim_ok) LogWarn("Render: oscuramento non disponibile ({}), continuo senza", Hr(dim_hr));
    else if (saved) LogInfo("Oscuramento: ripresa dello stato precedente ({}x{})", saved->width, saved->height);
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
    reset_dimmer();
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
      // Anche senza reshape (es. dopo ACCESS_LOST) la mappa riparte da zero.
      if (!needs_reshape) reset_dimmer();
      LogInfo("Cattura avviata: {}x{} {}", capture.Width(), capture.Height(), FormatName(capture.Format()));
    }

    double now = NowMs();
    const DimInputs dim_in = params_.dim ? params_.dim->Get() : DimInputs{};
    if (dim_ok && params_.dim->ZonesVersion() != zones_applied) {
      const DimZones zones = params_.dim->GetZones();
      if (zones.cols == static_cast<int>(dimmer.Cols()) && zones.rows == static_cast<int>(dimmer.Rows())) {
        dimmer.SetZones(zones.cells);
      }
      zones_applied = zones.version;
    }
    if (dim_ok) dimmer.SetActiveZones(dim_in.active_zones);
    const bool hdr = capture.Format() == DXGI_FORMAT_R16G16B16A16_FLOAT;
    const float white = hdr ? dim_in.sdr_white_scrgb : 1.0f;
    const bool highlight = dim_in.highlight_until_ms > now;
    if (dim_in.version != dim_version || highlight != highlight_on) {
      dim_schedule.Poke(now);
      dim_version = dim_in.version;
      highlight_on = highlight;
    }
    double next = now + kMaxAcquireTimeoutMs;
    if (params_.shift) next = std::min(next, params_.shift->NextChangeAt(now));
    const ShadeInputs shade_in = params_.shade ? params_.shade->Get() : ShadeInputs{};
    // Rampa della modalita' assente: ridisegni al massimo a 20 Hz finche' dura.
    if (have_frame && shade_in.away_since_ms >= 0 && now < shade_in.away_since_ms + kAwayRampMs) {
      next = std::min(next, now + 50);
    }
    // Valori nuovi dall'interfaccia (o ingresso/uscita dall'assenza): ridisegno subito.
    if (shade_in.version != shade_version) {
      next = now;
      shade_version = shade_in.version;
    }
    // Senza un frame da mostrare non si ridisegna: i tempi dell'oscuramento non contano
    // (altrimenti un ridisegno "dovuto" ma impossibile farebbe girare il ciclo a vuoto).
    if (dim_ok && have_frame) {
      next = std::min(next, dim_schedule.NextRedrawAt(now, dim_in.params, params_.frame_period_ms));
      // Segnali: un riassunto al secondo, letto senza mai attendere la GPU.
      if (params_.signals) {
        FrameSummary summary;
        double measured_ms = 0;
        if (dimmer.TryReadSummary(summary, measured_ms)) params_.signals->Publish(summary, measured_ms);
        if (now - summary_requested_ms >= 1000) {
          dimmer.RequestSummary(now, shade_at(now, shade_in));
          summary_requested_ms = now;
        }
        next = std::min(next, summary_requested_ms + 1000);
        if (dimmer.SummaryPending()) next = std::min(next, now + 16);
      }
      if (highlight) next = std::min(next, dim_in.highlight_until_ms);
      // Mappa per la scheda: una copia al secondo, letta senza mai attendere la GPU.
      if (dim_in.watch) {
        if (dimmer.TryReadback(readback_levels)) {
          params_.dim->PublishMap(DimMap{readback_levels, static_cast<int>(dimmer.Cols()),
                                         static_cast<int>(dimmer.Rows()), now});
        }
        if (now - readback_requested_ms >= 1000) {
          dimmer.RequestReadback();
          readback_requested_ms = now;
        }
        next = std::min(next, dimmer.ReadbackPending() ? now + 16 : readback_requested_ms + 1000);
      }
      if (params_.dim_diagnostics) {
        if (now >= diag_stats_at) {
          dimmer.RequestDebugReadback(now);
          diag_stats_at = now + 10000;
        }
        if (dimmer.TryDebugReadback(diag_grids)) {
          const DimGridStats st = SummarizeDimGrids(diag_grids.levels, diag_grids.flags, diag_grids.classes,
                                                    diag_grids.last_change, diag_grids.now_s, dim_in.params.delay_s);
          LogInfo("Oscuramento diag: {} (bianco {:.2f}, attivo {}, sospeso {})", FormatDimGridStats(st), white,
                  dim_in.params.enabled, dim_in.suspended);
          // Mappa delle celle: ' ' niente, '.' in movimento, 'b' luminosa, 'S' elemento da
          // scurire, 'L' da non toccare, '1'-'9' livello (decimi), '*' cambiata da 2 s.
          std::ofstream grid(params_.diagnostics_dir / "dim-grid.txt");
          const UINT gc = dimmer.Cols(), gr = dimmer.Rows();
          for (UINT y = 0; y < gr; ++y) {
            std::string line(gc, ' ');
            for (UINT x = 0; x < gc; ++x) {
              const size_t i = static_cast<size_t>(y) * gc + x;
              const uint32_t cls = diag_grids.classes[i] & 3u;
              const float lv = diag_grids.levels[i];
              char ch = diag_grids.now_s - diag_grids.last_change[i] < kDimMotionWindowS ? '.' : ' ';
              if (diag_grids.flags[i] & 1u) ch = 'b';
              if (cls == 2) ch = 'L';
              if (cls == 1) ch = 'S';
              if (lv > 0.05f) ch = static_cast<char>('0' + std::clamp(static_cast<int>(lv * 10 + 0.5f), 1, 9));
              if (diag_grids.now_s - diag_grids.last_change[i] < 2.0) ch = '*';
              line[x] = ch;
            }
            grid << line << '\n';
          }
        }
        if (now >= diag_snap_at) {
          diag_want_snap = diag_snaps < 8;
          // Dopo l'ultima istantanea il timer non deve restare nel passato (timeout 0).
          diag_snap_at = diag_snaps + 1 < 8 ? now + 60000 : std::numeric_limits<double>::infinity();
        }
        if (snap_in.TryWrite(snap_in_file, white) | snap_out.TryWrite(snap_out_file, white)) {}
        if (snap_in.Pending() || snap_out.Pending()) next = std::min(next, now + 16);
        next = std::min(next, std::min(diag_stats_at, diag_snap_at));
      }
    }
    const UINT timeout = static_cast<UINT>(std::clamp(next - now, 0.0, static_cast<double>(kMaxAcquireTimeoutMs)));

    const auto status = capture.Acquire(timeout, cache.Get(), [&](ID3D11Texture2D* frame) {
      if (!dim_ok) return;
      const double t = NowMs();
      if (FAILED(dimmer.Detect(frame, cache.Get(), t, dim_in.params, white))) {
        LogWarn("Render: confronto dell'oscuramento fallito, continuo senza");
        dim_ok = false;
        return;
      }
      dim_schedule.OnNewFrame(t);
    });
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
    if (new_frame && params_.shade) params_.shade->CountNewFrame();
    if (!have_frame) continue;

    now = NowMs();
    const Offset offset = params_.shift ? params_.shift->OffsetAt(now) : Offset{};
    const bool dim_due = dim_ok && now >= dim_schedule.NextRedrawAt(now, dim_in.params, params_.frame_period_ms);
    // Si presenta solo se c'e' qualcosa di nuovo da mostrare.
    const ShadeParams shade_now = shade_at(now, shade_in);
    if (new_frame || !(offset == last_drawn) || dim_due || diag_want_snap || !(shade_now == last_shade)) {
      WaitForSingleObjectEx(presenter.FrameWaitable(), 100, TRUE);
      DimDrawInputs dim_draw;
      if (dim_ok) {
        now = NowMs();
        dimmer.Update(now, dim_in.params, dim_in.suspended);
        dim_schedule.OnRedraw(now);
        dim_draw = DimDrawInputs{dimmer.LevelsSrv(), dimmer.Cols(), dimmer.Rows(), white, highlight};
      }
      std::function<void(ID3D11Texture2D*)> grab;
      if (diag_want_snap) {
        ++diag_snaps;
        const std::string stamp = std::to_string(static_cast<long long>(GetTickCount64() / 1000));
        snap_in_file = params_.diagnostics_dir / ("dim-" + stamp + "-in.bmp");
        snap_out_file = params_.diagnostics_dir / ("dim-" + stamp + "-out.bmp");
        snap_in.Request(device.Get(), cache.Get());
        grab = [&](ID3D11Texture2D* back_buffer) { snap_out.Request(device.Get(), back_buffer); };
        diag_want_snap = false;
      }
      hr = presenter.Draw(cache.Get(), offset, dim_draw, shade_now, grab);
      if (FAILED(hr)) {
        LogWarn("Render: Present fallito: {}", Hr(hr));
        hide();
        return SessionEnd::Failed;
      }
      if (!(offset == last_drawn)) LogDebug("Offset disegnato ({},{})", offset.x, offset.y);
      last_drawn = offset;
      last_shade = shade_now;
      if (!shown) {
        presenter.SetVisible(true);
        shown = true;
        failures_ = 0;
        Post(RenderEvent::FirstFrameShown);
      }
    }

    if (now - stats_since >= 10000) {
      LogDebug("Render: ultimi 10 s {} frame copiati, {} ignorati", capture.frames_copied, capture.frames_skipped);
      last_copied_ = capture.frames_copied;
      last_skipped_ = capture.frames_skipped;
      total_copied_ += capture.frames_copied;
      total_skipped_ += capture.frames_skipped;
      measured_ms_ += static_cast<uint64_t>(now - stats_since);
      capture.frames_copied = 0;
      capture.frames_skipped = 0;
      stats_since = now;
    }
  }
  presenter.SetVisible(false);
  return SessionEnd::Stop;
}

}  // namespace oled
