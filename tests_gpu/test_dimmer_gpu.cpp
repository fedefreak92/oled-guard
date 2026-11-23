// Gli shader dell'oscuramento (Dimmer) confrontati con il modello di riferimento, sulle
// stesse scene realistiche dei test del modello.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "dim_scenes.h"
#include "dimmer.h"
#include "gpu_test_util.h"
#include "shade_model.h"
#include "summary_model.h"
#include "test_framework.h"

using oled::DimModel;
using oled::DimParams;
using oled::Dimmer;
using oled::DimSavedState;
using oled::LumaImage;
using namespace oled::gpu_test;
using namespace oled::scenes;

namespace {

constexpr DXGI_FORMAT kHdr = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kSdr = DXGI_FORMAT_B8G8R8A8_UNORM;

// Modello e Dimmer alimentati con gli stessi frame, zone e istanti.
class Sim {
 public:
  Sim(int w, int h, DXGI_FORMAT format, float white, const std::vector<uint8_t>& zones, double start_s = 0,
      const DimSavedState* model_restore = nullptr, const DimSavedState* gpu_restore = nullptr)
      : format_(format), white_(white), model_(w, h, start_s, model_restore) {
    model_.SetZones(zones);
    device_ = WarpDevice();
    if (!device_) return;
    device_->GetImmediateContext(&ctx_);
    cur_ = Texture(device_.Get(), w, h, format, D3D11_BIND_SHADER_RESOURCE);
    prev_ = Texture(device_.Get(), w, h, format, D3D11_BIND_SHADER_RESOURCE);
    ok_ = SUCCEEDED(dimmer_.Init(device_.Get(), w, h, format, start_s * 1000, gpu_restore));
    dimmer_.SetZones(zones);
  }
  bool Ok() const { return ok_; }
  void Active(const std::vector<uint8_t>& ids) {
    model_.SetActiveZones(ids);
    dimmer_.SetActiveZones(ids);
  }
  Dimmer& dimmer() { return dimmer_; }
  DimModel& model() { return model_; }

  void Frame(const LumaImage& img, double t, const DimParams& p = {}) {
    model_.OnFrame(Hdr() ? img : QuantizeSdr(img), t);
    Upload(ctx_.Get(), cur_.Get(), img, format_, white_);
    dimmer_.Detect(cur_.Get(), prev_.Get(), t * 1000, p, Hdr() ? white_ : 1.0f);
    ctx_->CopyResource(prev_.Get(), cur_.Get());  // come la cache della cattura
  }
  void Update(double t, double dt, const DimParams& p = {}, bool suspended = false) {
    model_.Update(t, dt, p, suspended);
    dimmer_.Update(t * 1000, p, suspended);
  }
  // Simula a passi di 0,5 s; `frame` ritorna true se l'immagine e' cambiata.
  void Run(LumaImage& img, double from, double to, const std::function<bool(LumaImage&, double)>& frame = {},
           const DimParams& p = {}) {
    for (double t = from; t <= to + 1e-9; t += 0.5) {
      if (frame && frame(img, t)) Frame(img, t, p);
      Update(t, 0.5, p);
    }
  }
  std::vector<float> GpuLevels() { return ReadFloats(device_.Get(), dimmer_.LevelsSrv()); }
  double MaxDiff() {
    const auto gpu = GpuLevels();
    const auto& ref = model_.Levels();
    if (gpu.size() != ref.size()) return 1e9;
    double d = 0;
    size_t worst = 0;
    for (size_t i = 0; i < gpu.size(); ++i) {
      const double e = std::fabs(static_cast<double>(gpu[i]) - ref[i]);
      if (e > d) { d = e; worst = i; }
    }
    if (d > 0.002) {
      std::printf("    diff max %.4f nella cella (%d,%d): gpu %.4f modello %.4f\n", d,
                  static_cast<int>(worst % model_.Cols()), static_cast<int>(worst / model_.Cols()), gpu[worst], ref[worst]);
    }
    return d;
  }
  float GpuLevel(int cx, int cy) { return GpuLevels()[static_cast<size_t>(cy) * model_.Cols() + cx]; }

  // Riassunto dei segnali dalla GPU: richiesta e lettura asincrona ripetuta.
  bool GpuSummary(double t, const oled::ShadeParams& shade, oled::FrameSummary& out) {
    dimmer_.RequestSummary(t * 1000, shade);
    double measured = 0;
    // WARP lavora in modo asincrono: si riprova per al massimo 2 s, come farebbe il render.
    for (int i = 0; i < 400; ++i) {
      if (dimmer_.TryReadSummary(out, measured)) return true;
      ctx_->Flush();
      Sleep(5);
    }
    return false;
  }
  oled::FrameSummary ModelSummary(double t, const oled::ShadeParams& shade) {
    return oled::ComputeSummary(model_, t, shade);
  }

 private:
  bool Hdr() const { return format_ == kHdr; }
  DXGI_FORMAT format_;
  float white_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> ctx_;
  ComPtr<ID3D11Texture2D> cur_, prev_;
  Dimmer dimmer_;
  DimModel model_;
  bool ok_ = false;
};

LumaImage BarScene() {
  LumaImage img = Black();
  Fill(img, 0, kH - 80, kW, kH, 1.0f);
  return img;
}

bool PlayVideo(LumaImage& img, double t) {
  VideoFrame(img, t);
  return true;
}

}  // namespace

TEST(gpu_desktop_icons_dim_sdr) {
  Sim sim(kW, kH, kSdr, 1.0f, Zones(kShell));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = DesktopIcons();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 330);
  CHECK_NEAR(sim.GpuLevel(2, 2), 0.20, 0.01);
  CHECK(sim.MaxDiff() <= 0.002);
  sim.Run(img, 330.5, 480);
  CHECK_NEAR(sim.GpuLevel(2, 2), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_dark_app_text_not_dimmed_hdr) {
  Sim sim(kW, kH, kHdr, 3.0f, Zones(kApp));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = DarkAppText();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 600);
  for (float v : sim.GpuLevels()) CHECK_NEAR(v, 0.0, 1e-6);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_logo_over_video_and_pause) {
  Sim sim(kW, kH, kHdr, 3.0f, Zones(kApp));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = VideoWithLogo(0);
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480, PlayVideo);
  CHECK_NEAR(sim.GpuLevel(48, 5), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
  sim.Run(img, 480.5, 498);  // video in pausa: ritorno lento
  CHECK_NEAR(sim.GpuLevel(48, 5), 0.40 - 0.40 / 30 * 8.5, 0.01);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_taskbar_under_static_window_sdr) {
  Sim sim(kW, kH, kSdr, 1.0f, TaskbarZones());
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480);
  CHECK_NEAR(sim.GpuLevel(10, 33), 0.40, 0.002);
  CHECK_NEAR(sim.GpuLevel(10, 10), 0.0, 1e-6);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_change_recovers) {
  Sim sim(kW, kH, kHdr, 2.5f, TaskbarZones());
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = BarScene();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480);
  CHECK_NEAR(sim.GpuLevel(10, 33), 0.40, 0.002);
  img.luma[static_cast<size_t>(33 * 16 + 3) * kW + 10 * 16 + 3] = 0.5f;
  sim.Frame(img, 480.5);
  sim.Update(480.5, 0.5);
  CHECK_NEAR(sim.GpuLevel(10, 33), 0.0, 1e-6);
  CHECK_NEAR(sim.GpuLevel(40, 33), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_suspend_and_large_areas) {
  Sim sim(kW, kH, kHdr, 2.5f, Zones(kApp));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);
  DimParams p;
  p.large_enabled = true;
  sim.Frame(img, 0, p);
  sim.Run(img, 0.5, 1350, {}, p);
  CHECK_NEAR(sim.GpuLevel(32, 18), 0.10, 0.005);
  CHECK(sim.MaxDiff() <= 0.002);
  p.large_enabled = false;  // spegnere l'opzione: ritorno in 1 s
  sim.Update(1350.125, 0.125, p);
  CHECK(sim.MaxDiff() <= 0.002);
  sim.Update(1350.5, 0.375, p, true);
  CHECK(sim.MaxDiff() <= 0.002);
  CHECK_NEAR(sim.GpuLevel(32, 18), 0.0, 1e-6);
}

TEST(gpu_matches_model_odd_size) {
  Sim sim(1000, 600, kHdr, 2.5f, Zones(kShell, 63, 38));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black(1000, 600);
  Fill(img, 992, 592, 1000, 600, 1.0f);
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480);
  CHECK_EQ(sim.dimmer().Cols(), 63u);
  CHECK_EQ(sim.dimmer().Rows(), 38u);
  CHECK_NEAR(sim.GpuLevel(62, 37), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
}

// In SDR la soglia vale in luce lineare: un grigio al 15% (codificato ~42%) non e' luminoso.
TEST(gpu_sdr_threshold_in_linear_light) {
  Sim sim(kW, kH, kSdr, 1.0f, Zones(kShell));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black();
  Fill(img, 200, 200, 232, 232, 0.15f);
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480);
  CHECK(!sim.model().Bright(13, 13));
  CHECK_NEAR(sim.GpuLevel(13, 13), 0.0, 1e-6);
  CHECK(sim.MaxDiff() <= 0.002);
}

// Pausa: stato salvato dalla GPU, nuovo Dimmer con ripresa, confronto col modello.
TEST(gpu_restore_after_pause) {
  LumaImage img = DesktopIcons();
  Sim before(kW, kH, kHdr, 3.0f, Zones(kShell));
  CHECK(before.Ok());
  if (!before.Ok()) return;
  before.Frame(img, 0);
  before.Run(img, 0.5, 480);
  DimSavedState gpu_saved;
  CHECK(before.dimmer().SaveState(480 * 1000, gpu_saved));
  CHECK_EQ(gpu_saved.ages.size(), size_t{kCols * kRows});
  const DimSavedState model_saved = before.model().Save(480);

  Fill(img, 16, 16, 80, 80, 0.7f);  // un'icona cambiata durante la pausa
  Sim after(kW, kH, kHdr, 3.0f, Zones(kShell), 1000, &model_saved, &gpu_saved);
  CHECK(after.Ok());
  if (!after.Ok()) return;
  after.Frame(img, 1000);
  after.Update(1000, 0);
  CHECK_NEAR(after.GpuLevel(7, 2), 0.0, 1e-6);
  after.Run(img, 1000.5, 1150);
  CHECK_NEAR(after.GpuLevel(7, 2), 0.20, 0.01);  // eta' ripresa: risale lentamente
  CHECK_NEAR(after.GpuLevel(2, 2), 0.0, 1e-6);   // icona cambiata: da capo
  CHECK(after.MaxDiff() <= 0.002);
}

TEST(gpu_restore_ignored_on_size_change) {
  ComPtr<ID3D11Device> device = WarpDevice();
  CHECK(device != nullptr);
  if (!device) return;
  DimSavedState saved;
  saved.width = 1920;
  saved.height = 1080;
  saved.ages.assign(120 * 68, 1000.0f);
  saved.hashes.assign(120 * 68, 0u);
  Dimmer d;
  CHECK(SUCCEEDED(d.Init(device.Get(), kW, kH, kHdr, 0, &saved)));
  CHECK_EQ(d.Cols(), 64u);
}

TEST(gpu_readback_matches_srv) {
  Sim sim(kW, kH, kHdr, 2.5f, TaskbarZones());
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = BarScene();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 400);
  std::vector<float> levels;
  CHECK(!sim.dimmer().TryReadback(levels));  // nessuna copia richiesta
  sim.dimmer().RequestReadback();
  bool ready = false;
  // Come nel render: si riprova ai giri successivi, senza mai attendere la GPU.
  for (int i = 0; i < 200 && !ready; ++i) {
    ready = sim.dimmer().TryReadback(levels);
    if (!ready) Sleep(10);
  }
  CHECK(ready);
  CHECK(levels == sim.GpuLevels());
  CHECK(!sim.dimmer().TryReadback(levels));  // gia' consegnata
}

TEST(gpu_reset_changes_grid) {
  ComPtr<ID3D11Device> device = WarpDevice();
  CHECK(device != nullptr);
  if (!device) return;
  Dimmer d;
  CHECK(SUCCEEDED(d.Init(device.Get(), 1024, 576, kHdr, 0)));
  CHECK(SUCCEEDED(d.Init(device.Get(), 1920, 1080, kSdr, 5000)));
  CHECK_EQ(d.Cols(), 120u);
  CHECK_EQ(d.Rows(), 68u);
  const auto levels = ReadFloats(device.Get(), d.LevelsSrv());
  CHECK_EQ(levels.size(), size_t{120 * 68});
  for (float v : levels) CHECK_NEAR(v, 0.0, 1e-9);
}

// Deviazione breve (icona evidenziata dal mouse per 5 s): stessa logica del modello.
TEST(gpu_brief_change_keeps_age) {
  Sim sim(kW, kH, kHdr, 3.0f, Zones(kShell));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = DesktopIcons();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480);
  const LumaImage original = img;
  Fill(img, 16, 16, 80, 80, 0.9f);
  sim.Frame(img, 480.5);
  sim.Update(480.5, 0.5);
  CHECK_NEAR(sim.GpuLevel(2, 2), 0.0, 1e-6);
  img = original;
  sim.Frame(img, 485.5);
  sim.Update(485.5, 5.0);  // 5 s dall'ultimo aggiornamento, per entrambi
  sim.Run(img, 486, 635.5);
  CHECK_NEAR(sim.GpuLevel(2, 2), 0.20, 0.01);
  CHECK(sim.MaxDiff() <= 0.002);
  // Deviazione lunga: da capo.
  Fill(img, 16, 16, 80, 80, 0.9f);
  sim.Frame(img, 636);
  sim.Run(img, 636, 660);
  img = original;
  sim.Frame(img, 660.5);
  sim.Update(660.5, 0.5);
  CHECK_NEAR(sim.GpuLevel(2, 2), 0.0, 1e-6);
  CHECK(sim.MaxDiff() <= 0.002);
}

// Movimento di un'altra finestra: non conta (niente alone intorno ai video).
TEST(gpu_motion_of_other_window_does_not_count) {
  std::vector<uint8_t> zones(static_cast<size_t>(kCols) * kRows, 3);
  for (int y = 0; y < kRows; ++y)
    for (int x = 0; x < 20; ++x) zones[static_cast<size_t>(y) * kCols + x] = 2;
  Sim sim(kW, kH, kHdr, 3.0f, zones);
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = VideoWithLogo(0);
  Fill(img, 0, 0, 320, kH, 1.0f);
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480, PlayVideo);
  CHECK_NEAR(sim.GpuLevel(18, 10), 0.0, 1e-6);
  CHECK_NEAR(sim.GpuLevel(48, 5), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_save_before_first_frame_keeps_pending_restore) {
  ComPtr<ID3D11Device> device = WarpDevice();
  CHECK(device != nullptr);
  if (!device) return;
  DimSavedState saved;
  saved.width = kW;
  saved.height = kH;
  saved.ages.assign(kCols * kRows, 123.0f);
  saved.hashes.assign(kCols * kRows, 42u);
  Dimmer d;
  CHECK(SUCCEEDED(d.Init(device.Get(), kW, kH, kHdr, 0, &saved)));
  DimSavedState again;
  CHECK(d.SaveState(5000, again));
  CHECK(again.ages == saved.ages);
  CHECK(again.hashes == saved.hashes);
}

TEST(gpu_save_during_brief_change_keeps_anchor) {
  Sim before(kW, kH, kHdr, 3.0f, Zones(kShell));
  CHECK(before.Ok());
  if (!before.Ok()) return;
  LumaImage img = DesktopIcons();
  before.Frame(img, 0);
  before.Run(img, 0.5, 480);
  const LumaImage original = img;
  Fill(img, 16, 16, 80, 80, 0.9f);
  before.Frame(img, 480.5);
  DimSavedState gpu_saved;
  CHECK(before.dimmer().SaveState(482 * 1000, gpu_saved));
  const DimSavedState model_saved = before.model().Save(482);
  Sim after(kW, kH, kHdr, 3.0f, Zones(kShell), 1000, &model_saved, &gpu_saved);
  CHECK(after.Ok());
  if (!after.Ok()) return;
  img = original;
  after.Frame(img, 1000);
  after.Run(img, 1000.5, 1150);
  CHECK_NEAR(after.GpuLevel(2, 2), 0.20, 0.01);
  CHECK(after.MaxDiff() <= 0.002);
}

// Finestre come il desktop, con la finestra attiva che resta normale e poi riparte da zero.
TEST(gpu_windows_like_desktop_and_activation) {
  std::vector<uint8_t> zones(static_cast<size_t>(kCols) * kRows, 3);
  for (int y = 0; y < kRows; ++y)
    for (int x = 0; x < 32; ++x) zones[static_cast<size_t>(y) * kCols + x] = 2;
  Sim sim(kW, kH, kSdr, 1.0f, zones);
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  DimParams p;
  p.windows_like_desktop = true;
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);
  sim.Active({2});
  sim.Frame(img, 0, p);
  sim.Run(img, 0.5, 480, {}, p);
  CHECK_NEAR(sim.GpuLevel(10, 10), 0.0, 1e-6);
  CHECK_NEAR(sim.GpuLevel(50, 10), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
  sim.Active({3});  // si passa all'altra finestra
  sim.Update(480.5, 0.5, p);
  CHECK_NEAR(sim.GpuLevel(50, 10), 0.0, 1e-6);  // torna normale in mezzo secondo
  sim.Run(img, 481, 810, {}, p);
  CHECK_NEAR(sim.GpuLevel(10, 10), 0.20, 0.01);  // la prima ha ripreso il conteggio da 480
  CHECK(sim.MaxDiff() <= 0.002);
}

namespace {
// Differenza massima fra riassunto della GPU e del modello (luci relative sopra 1).
double SummaryDiff(const oled::FrameSummary& a, const oled::FrameSummary& b) {
  const auto rel = [](double x, double y) { return std::fabs(x - y) / std::max(1.0, std::fabs(y)); };
  double d = std::max({rel(a.risk, b.risk), rel(a.risk_unprotected, b.risk_unprotected), rel(a.motion, b.motion),
                       rel(a.brightness, b.brightness), rel(a.light_still_before, b.light_still_before),
                       rel(a.light_still_removed, b.light_still_removed)});
  if (!(a.grid == b.grid)) {
    std::printf("    griglia gpu/modello: %dx%d / %dx%d\n", a.grid.cols, a.grid.rows, b.grid.cols, b.grid.rows);
    return 1.0;
  }
  for (int z = 0; z < b.grid.Count(); ++z) d = std::max(d, rel(a.zone_light[z], b.zone_light[z]));
  if (d > 1e-3) {
    std::printf("    riassunto gpu/modello: rischio %.4f/%.4f non protetto %.4f/%.4f movimento %.4f/%.4f "
                "luminosita' %.4f/%.4f prima %.4f/%.4f tolta %.4f/%.4f\n",
                a.risk, b.risk, a.risk_unprotected, b.risk_unprotected, a.motion, b.motion, a.brightness,
                b.brightness, a.light_still_before, b.light_still_before, a.light_still_removed,
                b.light_still_removed);
  }
  return d;
}
}  // namespace

TEST(gpu_summary_matches_model_sdr) {
  Sim sim(kW, kH, kSdr, 1.0f, Zones(kShell));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = DesktopIcons();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 90);
  oled::FrameSummary gpu;
  CHECK(sim.GpuSummary(90, {}, gpu));
  const oled::FrameSummary ref = sim.ModelSummary(90, {});
  CHECK(ref.risk > 0.01);
  CHECK(SummaryDiff(gpu, ref) <= 1e-3);
}

TEST(gpu_summary_matches_model_hdr_odd_size) {
  Sim sim(1366, 768, kHdr, 2.5f, Zones(kShell, 86, 48));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black(1366, 768);
  Fill(img, 100, 100, 400, 300, 40.0f);   // molto oltre il bianco SDR
  Fill(img, 1360, 760, 1366, 768, 1.0f);  // cella parziale nell'angolo
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 90, [](LumaImage& im, double t) {
    if (t < 85) return false;
    Fill(im, 600, 400, 900, 600, static_cast<float>(std::fmod(t, 1.0) * 3.0));  // movimento recente
    return true;
  });
  oled::FrameSummary gpu;
  CHECK(sim.GpuSummary(90, {}, gpu));
  const oled::FrameSummary ref = sim.ModelSummary(90, {});
  CHECK(ref.risk > 0.01 && ref.motion > 0.01 && ref.brightness > 1.0);
  CHECK(SummaryDiff(gpu, ref) <= 1e-3);
}

TEST(gpu_summary_matches_model_active_window) {
  Sim sim(kW, kH, kSdr, 1.0f, Zones(kApp));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);  // pagina bianca ferma sotto il mouse
  sim.Active({kApp});
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 90);
  oled::FrameSummary gpu;
  CHECK(sim.GpuSummary(90, {}, gpu));
  CHECK_NEAR(gpu.risk, 1.0, 1e-3);
  CHECK(SummaryDiff(gpu, sim.ModelSummary(90, {})) <= 1e-3);
}

TEST(gpu_summary_with_dim_and_shade) {
  Sim sim(kW, kH, kSdr, 1.0f, Zones(kShell));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = DesktopIcons();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 400);
  oled::ShadeParams shade;
  shade.brightness = 0.7f;
  shade.vignette_strength = 0.5f;
  shade.spread[0] = 0.2f;
  oled::FrameSummary gpu;
  CHECK(sim.GpuSummary(400, shade, gpu));
  const oled::FrameSummary ref = sim.ModelSummary(400, shade);
  CHECK(ref.light_still_removed > 0.0);
  CHECK(ref.risk_unprotected < ref.risk);
  CHECK(SummaryDiff(gpu, ref) <= 1e-3);
}

namespace {
// Riassunto GPU contro modello su uno schermo w x h non 16:9: zone luminose ferme,
// una cella parziale nell'angolo e movimento recente.
void CheckSummaryOnSize(int w, int h) {
  const int cols = (w + 15) / 16, rows = (h + 15) / 16;
  Sim sim(w, h, kSdr, 1.0f, Zones(kShell, cols, rows));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black(w, h);
  Fill(img, 0, 0, w / 5, h / 4, 1.0f);              // in alto a sinistra
  Fill(img, w - 40, h - 20, w, h, 0.8f);            // angolo in basso a destra, celle parziali
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 90, [w, h](LumaImage& im, double t) {
    if (t < 85) return false;
    Fill(im, w / 2, h / 3, w / 2 + 48, h / 3 + 32, static_cast<float>(std::fmod(t, 1.0)));
    return true;
  });
  oled::FrameSummary gpu;
  CHECK(sim.GpuSummary(90, {}, gpu));
  const oled::FrameSummary ref = sim.ModelSummary(90, {});
  CHECK(ref.grid == oled::GridForSize(w, h));
  CHECK(ref.risk > 0.01 && ref.motion > 0.0);
  CHECK(SummaryDiff(gpu, ref) <= 1e-3);
}
}  // namespace

TEST(gpu_summary_matches_model_ultrawide) { CheckSummaryOnSize(430, 180); }
TEST(gpu_summary_matches_model_superwide) { CheckSummaryOnSize(512, 144); }
TEST(gpu_summary_matches_model_16_10_odd) { CheckSummaryOnSize(321, 201); }

TEST(gpu_summary_never_waits) {
  Sim sim(kW, kH, kSdr, 1.0f, Zones(kShell));
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = DesktopIcons();
  sim.Frame(img, 0);
  oled::FrameSummary out;
  double measured = -1;
  CHECK(!sim.dimmer().TryReadSummary(out, measured));  // niente richiesto: nessuna lettura
  CHECK(sim.GpuSummary(1, {}, out));
}
