// Gli shader dell'oscuramento (Dimmer) confrontati con il modello di riferimento.
#include <algorithm>
#include <cmath>
#include <functional>

#include "dimmer.h"
#include "gpu_test_util.h"
#include "test_framework.h"

using oled::DimModel;
using oled::DimParams;
using oled::Dimmer;
using oled::LumaImage;
using namespace oled::gpu_test;

namespace {

LumaImage Black(int w, int h) {
  LumaImage img;
  img.width = w;
  img.height = h;
  img.luma.assign(static_cast<size_t>(w) * h, 0.0f);
  return img;
}

void Fill(LumaImage& img, int x0, int y0, int x1, int y1, float v) {
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) img.luma[static_cast<size_t>(y) * img.width + x] = v;
}

// Modello e Dimmer alimentati con gli stessi frame e gli stessi istanti.
class Sim {
 public:
  Sim(int w, int h, DXGI_FORMAT format, float white) : w_(w), h_(h), format_(format), white_(white), model_(w, h, 0) {
    device_ = WarpDevice();
    if (!device_) return;
    device_->GetImmediateContext(&ctx_);
    cur_ = Texture(device_.Get(), w, h, format, D3D11_BIND_SHADER_RESOURCE);
    prev_ = Texture(device_.Get(), w, h, format, D3D11_BIND_SHADER_RESOURCE);
    ok_ = SUCCEEDED(dimmer_.Init(device_.Get(), w, h, format, 0));
  }
  bool Ok() const { return ok_; }
  Dimmer& dimmer() { return dimmer_; }
  DimModel& model() { return model_; }

  void Frame(const LumaImage& img, double t, const DimParams& p = {}) {
    model_.OnFrame(format_ == DXGI_FORMAT_R16G16B16A16_FLOAT ? img : QuantizeSdr(img), t);
    Upload(ctx_.Get(), cur_.Get(), img, format_, white_);
    dimmer_.Detect(cur_.Get(), prev_.Get(), t * 1000, p, format_ == DXGI_FORMAT_R16G16B16A16_FLOAT ? white_ : 1.0f);
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
    for (size_t i = 0; i < gpu.size(); ++i) d = std::max(d, std::fabs(static_cast<double>(gpu[i]) - ref[i]));
    return d;
  }
  float GpuLevel(int cx, int cy) { return GpuLevels()[static_cast<size_t>(cy) * model_.Cols() + cx]; }

 private:
  int w_, h_;
  DXGI_FORMAT format_;
  float white_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> ctx_;
  ComPtr<ID3D11Texture2D> cur_, prev_;
  Dimmer dimmer_;
  DimModel model_;
  bool ok_ = false;
};

// Barra bianca in fondo e un blocco che cambia ogni 10 s nella meta' alta.
LumaImage BarScene() {
  LumaImage img = Black(1024, 576);
  Fill(img, 0, 496, 1024, 576, 1.0f);
  return img;
}
bool BlockEvery10s(LumaImage& img, double t) {
  if (std::fmod(t, 10.0) != 0.0) return false;
  Fill(img, 480, 64, 544, 128, static_cast<float>(std::fmod(t * 0.013, 1.0)));
  return true;
}

constexpr DXGI_FORMAT kHdr = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kSdr = DXGI_FORMAT_B8G8R8A8_UNORM;

}  // namespace

TEST(gpu_matches_model_bar) {
  Sim sim(1024, 576, kHdr, 2.5f);
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = BarScene();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 330, BlockEvery10s);
  CHECK_NEAR(sim.GpuLevel(10, 33), 0.20, 0.01);
  CHECK(sim.MaxDiff() <= 0.002);
  sim.Run(img, 330.5, 480, BlockEvery10s);
  CHECK_NEAR(sim.GpuLevel(10, 33), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_matches_model_dark_text) {
  Sim sim(1024, 576, kSdr, 1.0f);
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black(1024, 576);
  for (int k = 0; 24 * k < 496; ++k) {
    const int h = k % 3 == 0 ? 4 : 3;
    for (int x = 0; x < 921; x += 16) Fill(img, x, 24 * k, x + 8, 24 * k + h, 0.8f);
  }
  for (int x = 0; x + 32 <= 1024; x += 96) Fill(img, x, 512, x + 32, 544, 1.0f);
  // Icone sparse in un'area libera: queste si devono oscurare anche in SDR.
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 600);
  CHECK(sim.model().Large(5, 4));
  CHECK_NEAR(sim.GpuLevel(5, 4), 0.0, 1e-6);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_icons_dim_in_sdr) {
  Sim sim(1024, 576, kSdr, 1.0f);
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black(1024, 576);
  Fill(img, 200, 200, 232, 232, 1.0f);
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480);
  CHECK_NEAR(sim.GpuLevel(13, 13), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_matches_model_odd_size) {
  Sim sim(1000, 600, kHdr, 2.5f);
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

TEST(gpu_change_recovers) {
  Sim sim(1024, 576, kHdr, 2.5f);
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = BarScene();
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480);
  CHECK_NEAR(sim.GpuLevel(10, 33), 0.40, 0.002);
  img.luma[static_cast<size_t>(33 * 16 + 3) * 1024 + 10 * 16 + 3] = 0.5f;
  sim.Frame(img, 480.5);
  sim.Update(480.5, 0.5);
  CHECK_NEAR(sim.GpuLevel(10, 33), 0.0, 1e-6);
  CHECK_NEAR(sim.GpuLevel(40, 33), 0.40, 0.002);
  CHECK(sim.MaxDiff() <= 0.002);
}

TEST(gpu_suspend_and_large_areas) {
  Sim sim(1024, 576, kHdr, 2.5f);
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black(1024, 576);
  Fill(img, 0, 0, 1024, 576, 1.0f);
  DimParams p;
  p.large_enabled = true;
  sim.Frame(img, 0, p);
  sim.Run(img, 0.5, 1350, {}, p);
  CHECK_NEAR(sim.GpuLevel(32, 18), 0.10, 0.005);
  CHECK(sim.MaxDiff() <= 0.002);
  sim.Update(1350.5, 0.5, p, true);
  CHECK(sim.MaxDiff() <= 0.002);
  CHECK_NEAR(sim.GpuLevel(32, 18), 0.0, 1e-6);
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

// In SDR la soglia vale in luce lineare: un grigio al 15% (codificato ~42%) non e' luminoso.
TEST(gpu_sdr_threshold_in_linear_light) {
  Sim sim(1024, 576, kSdr, 1.0f);
  CHECK(sim.Ok());
  if (!sim.Ok()) return;
  LumaImage img = Black(1024, 576);
  Fill(img, 200, 200, 232, 232, 0.15f);
  sim.Frame(img, 0);
  sim.Run(img, 0.5, 480);
  CHECK(!sim.model().Bright(13, 13));
  CHECK_NEAR(sim.GpuLevel(13, 13), 0.0, 1e-6);
  CHECK(sim.MaxDiff() <= 0.002);
}
