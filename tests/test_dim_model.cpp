#include <cmath>
#include <functional>

#include "config.h"
#include "dim_model.h"
#include "dim_params.h"
#include "test_framework.h"

using oled::DimModel;
using oled::DimParams;
using oled::LumaImage;

namespace {

constexpr int kW = 1024;
constexpr int kH = 576;  // 64x36 celle

LumaImage Black(int w = kW, int h = kH) {
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

// Barra bianca alta 80 px (righe di celle 31-35) su fondo nero.
LumaImage BarScene() {
  LumaImage img = Black();
  Fill(img, 0, kH - 80, kW, kH, 1.0f);
  return img;
}

// Quadrato 256x256 nella meta' alta con valori diversi a ogni istante.
void Noise(LumaImage& img, double t) {
  for (int y = 32; y < 288; ++y)
    for (int x = 384; x < 640; ++x)
      img.luma[static_cast<size_t>(y) * img.width + x] =
          static_cast<float>(std::fmod(t * 0.37 + (x + y) * 0.013, 1.0));
}

// Simula da `from` a `to` (inclusi) a passi di 0,5 s. `frame` puo' modificare l'immagine
// e ritorna true se e' cambiata (solo allora arriva un frame nuovo, come nella cattura).
void Run(DimModel& m, LumaImage& img, double from, double to, const DimParams& p,
         const std::function<bool(LumaImage&, double)>& frame = {}, bool suspended = false) {
  for (double t = from; t <= to + 1e-9; t += 0.5) {
    if (frame && frame(img, t)) m.OnFrame(img, t);
    m.Update(t, 0.5, p, suspended);
  }
}

bool NoiseFrame(LumaImage& img, double t) {
  Noise(img, t);
  return true;
}

// Barra ferma con rumore sopra, portata a 480 s (livello pieno 0,40).
DimModel BarAt480(LumaImage& img) {
  img = BarScene();
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{}, NoiseFrame);
  return m;
}

}  // namespace

TEST(dim_to_params_converts) {
  const DimParams p = oled::ToDimParams(oled::Config{});
  CHECK(p.enabled);
  CHECK_NEAR(p.strength, 0.40, 1e-9);
  CHECK_NEAR(p.delay_s, 180, 1e-9);
  CHECK_NEAR(p.ramp_s, 300, 1e-9);
  CHECK_NEAR(p.recover_s, 0.5, 1e-9);
  CHECK(!p.large_enabled);
  CHECK_NEAR(p.large_delay_s, 1200, 1e-9);
  CHECK_NEAR(p.large_strength, 0.20, 1e-9);
  CHECK_NEAR(p.bright_threshold, 0.25, 1e-9);
  CHECK_NEAR(p.large_fraction, 0.40, 1e-9);
  CHECK(p == DimParams{});
}

TEST(dim_grid_size_rounds_up) {
  DimModel m(1000, 600, 0);
  CHECK_EQ(m.Cols(), 63);
  CHECK_EQ(m.Rows(), 38);
  CHECK_EQ(m.Levels().size(), size_t{63 * 38});
}

TEST(dim_static_small_bar_dims) {
  LumaImage img = BarScene();
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 180, DimParams{}, NoiseFrame);
  CHECK_NEAR(m.Level(10, 33), 0.0, 1e-6);
  Run(m, img, 180.5, 330, DimParams{}, NoiseFrame);
  CHECK_NEAR(m.Level(10, 33), 0.20, 0.01);
  Run(m, img, 330.5, 480, DimParams{}, NoiseFrame);
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-4);
  CHECK(m.Bright(10, 33));
  CHECK(!m.Large(10, 33));
  CHECK_NEAR(m.Level(10, 10), 0.0, 1e-6);  // fondo nero: nulla da oscurare
}

TEST(dim_large_white_area_not_dimmed) {
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 600, DimParams{});
  for (float level : m.Levels()) CHECK_NEAR(level, 0.0, 1e-6);
  CHECK(m.Large(32, 18));
  CHECK(m.Large(0, 0));
  CHECK(m.Large(63, 35));
}

TEST(dim_dark_text_area_is_large) {
  LumaImage img = Black();
  // Testo chiaro su fondo scuro nelle righe di celle 0-30: trattini 8 px accesi / 8 spenti.
  for (int k = 0; 24 * k < 496; ++k) {
    const int h = k % 3 == 0 ? 4 : 3;  // una riga su tre "in grassetto"
    for (int x = 0; x < 921; x += 16) Fill(img, x, 24 * k, x + 8, 24 * k + h, 0.8f);
  }
  // Icone bianche 32x32 nelle ultime righe.
  for (int x = 0; x + 32 <= kW; x += 96) Fill(img, x, 512, x + 32, 544, 1.0f);
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 600, DimParams{});
  CHECK(m.Content(5, 1));
  CHECK(!m.Bright(5, 1));
  CHECK(m.Bright(5, 4));  // riga in grassetto
  CHECK(m.Large(5, 4));
  CHECK_NEAR(m.Level(5, 4), 0.0, 1e-6);
  CHECK(m.Bright(0, 32));
  CHECK(m.Large(0, 32));
  CHECK_NEAR(m.Level(0, 32), 0.0, 1e-6);
}

TEST(dim_icons_on_black_dim) {
  LumaImage img = Black();
  for (int y = 0; y + 32 <= kH; y += 200)
    for (int x = 0; x + 32 <= kW; x += 200) Fill(img, x + 200 * 0, y, x + 32, y + 32, 1.0f);
  Fill(img, 200, 200, 232, 232, 1.0f);
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  CHECK(m.Bright(13, 13));
  CHECK(!m.Large(13, 13));
  CHECK_NEAR(m.Level(13, 13), 0.40, 1e-4);
}

TEST(dim_change_recovers_fast) {
  LumaImage img;
  DimModel m = BarAt480(img);
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-4);
  img.luma[static_cast<size_t>(33 * 16 + 3) * kW + 10 * 16 + 3] = 0.5f;
  m.OnFrame(img, 480.5);
  m.Update(480.5, 0.5, DimParams{}, false);
  CHECK_NEAR(m.Level(10, 33), 0.0, 1e-6);
  CHECK_NEAR(m.Level(40, 33), 0.40, 1e-3);
}

TEST(dim_becoming_large_recovers_slow) {
  LumaImage img;
  DimModel m = BarAt480(img);
  // La meta' alta diventa bianca e ferma (il rumore si ferma).
  Fill(img, 0, 0, kW, 288, 1.0f);
  m.OnFrame(img, 480.5);
  Run(m, img, 480.5, 660, DimParams{});
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-3);  // la zona sopra non e' ancora ferma da 3 min
  Run(m, img, 660.5, 668, DimParams{});
  CHECK(m.Large(10, 33));
  // Da strength a 0 in 30 s (spec 5.2.3): passaggio invisibile.
  CHECK_NEAR(m.Level(10, 33), 0.40 - 0.40 / 30 * 8, 0.01);
  Run(m, img, 668.5, 680, DimParams{});
  CHECK(m.Level(10, 33) > 0.05);
  Run(m, img, 680.5, 691, DimParams{});
  CHECK_NEAR(m.Level(10, 33), 0.0, 1e-6);
}

// Con le grandi aree attive, un elemento che diventa parte di una grande area scende
// lentamente verso il livello delle grandi aree: cambia solo la classificazione.
TEST(dim_small_to_large_with_option_on_recovers_slow) {
  LumaImage img;
  DimModel m = BarAt480(img);
  DimParams p;
  p.large_enabled = true;
  Fill(img, 0, 0, kW, 288, 1.0f);
  m.OnFrame(img, 480.5);
  Run(m, img, 480.5, 660, p);
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-3);
  Run(m, img, 660.5, 668, p);
  CHECK(m.Large(10, 33));
  CHECK_NEAR(m.Level(10, 33), 0.40 - 0.40 / 30 * 8, 0.01);
}

// Spegnere l'opzione grandi aree e' un comando: ritorno in 1 s.
TEST(dim_disabling_large_option_recovers_in_one_second) {
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);
  DimParams p;
  p.large_enabled = true;
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 1500, p);
  CHECK_NEAR(m.Level(32, 18), 0.20, 1e-4);
  p.large_enabled = false;
  m.Update(1500.125, 0.125, p, false);
  CHECK_NEAR(m.Level(32, 18), 0.10, 1e-3);
}

TEST(dim_suspend_recovers_in_one_second) {
  LumaImage img;
  DimModel m = BarAt480(img);
  m.Update(480.5, 0.5, DimParams{}, true);
  CHECK_NEAR(m.Level(10, 33), 0.0, 1e-6);
  CHECK_NEAR(m.Target(10, 33), 0.0, 1e-9);
}

TEST(dim_disabled_recovers_in_one_second) {
  LumaImage img;
  DimModel m = BarAt480(img);
  DimParams off;
  off.enabled = false;
  m.Update(480.25, 0.25, off, false);
  CHECK_NEAR(m.Level(10, 33), 0.20, 1e-3);
}

TEST(dim_lower_strength_recovers_in_one_second) {
  LumaImage img;
  DimModel m = BarAt480(img);
  DimParams p;
  p.strength = 0.20;
  m.Update(480.125, 0.125, p, false);
  CHECK_NEAR(m.Level(10, 33), 0.30, 1e-3);
  m.Update(480.5, 0.375, p, false);
  CHECK_NEAR(m.Level(10, 33), 0.20, 1e-3);
}

TEST(dim_large_areas_optional) {
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);
  DimParams p;
  p.large_enabled = true;
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 1200, p);
  CHECK_NEAR(m.Level(32, 18), 0.0, 1e-6);
  Run(m, img, 1200.5, 1350, p);
  CHECK_NEAR(m.Level(32, 18), 0.10, 0.005);
  Run(m, img, 1350.5, 1500, p);
  CHECK_NEAR(m.Level(32, 18), 0.20, 1e-4);
}

TEST(dim_partial_cells_at_edges) {
  LumaImage img = Black(1000, 600);
  Fill(img, 992, 592, 1000, 600, 1.0f);
  DimModel m(1000, 600, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  CHECK(m.Bright(62, 37));
  CHECK_NEAR(m.Level(62, 37), 0.40, 1e-4);
  CHECK_NEAR(m.Level(61, 37), 0.0, 1e-6);
}

TEST(dim_huge_dt_does_not_overshoot) {
  LumaImage img = BarScene();
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  m.Update(10000, 10000, DimParams{}, false);
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-6);
  CHECK(!std::isnan(m.Level(10, 33)));
  m.Update(10000, 0, DimParams{}, false);
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-6);
}
