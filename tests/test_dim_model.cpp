#include <cmath>
#include <functional>

#include "config.h"
#include "dim_model.h"
#include "dim_params.h"
#include "dim_scenes.h"
#include "test_framework.h"

using oled::DimModel;
using oled::DimParams;
using oled::LumaImage;
using namespace oled::scenes;

namespace {

// Simula da `from` a `to` (inclusi) a passi di 0,5 s. `frame` puo' modificare l'immagine
// e ritorna true se e' cambiata (solo allora arriva un frame nuovo, come nella cattura).
void Run(DimModel& m, LumaImage& img, double from, double to, const DimParams& p,
         const std::function<bool(LumaImage&, double)>& frame = {}, bool suspended = false) {
  for (double t = from; t <= to + 1e-9; t += 0.5) {
    if (frame && frame(img, t)) m.OnFrame(img, t);
    m.Update(t, 0.5, p, suspended);
  }
}

bool PlayVideo(LumaImage& img, double t) {
  VideoFrame(img, t);
  return true;
}

// Barra bianca alta 80 px (righe di celle 31-35) nella zona della barra delle applicazioni.
LumaImage BarScene() {
  LumaImage img = Black();
  Fill(img, 0, kH - 80, kW, kH, 1.0f);
  return img;
}

DimModel BarAt480(LumaImage& img) {
  img = BarScene();
  DimModel m(kW, kH, 0);
  m.SetZones(TaskbarZones());
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  return m;
}

// Logo fermo sopra un video in riproduzione, in una finestra: a 480 s e' scurito.
DimModel VideoAt480(LumaImage& img, const DimParams& p = {}) {
  img = VideoWithLogo(0);
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kApp));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, p, PlayVideo);
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
  CHECK_NEAR(p.motion_fraction, 0.25, 1e-9);
  CHECK(p == DimParams{});
}

TEST(dim_grid_size_rounds_up) {
  DimModel m(1000, 600, 0);
  CHECK_EQ(m.Cols(), 63);
  CHECK_EQ(m.Rows(), 38);
  CHECK_EQ(m.Levels().size(), size_t{63 * 38});
}

// Il caso reale che non funzionava: icone fitte con etichette sul desktop.
TEST(dim_desktop_icon_grid_dims) {
  LumaImage img = DesktopIcons();
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kShell));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 330, DimParams{});
  CHECK(m.Bright(2, 2));
  CHECK_NEAR(m.Level(2, 2), 0.20, 0.01);
  Run(m, img, 330.5, 480, DimParams{});
  CHECK_NEAR(m.Level(2, 2), 0.40, 1e-4);
  CHECK_NEAR(m.Level(40, 10), 0.0, 1e-6);  // sfondo nero: nulla da scurire
}

// L'altro caso reale: un'interfaccia scura con testo sparso non si deve toccare.
TEST(dim_dark_app_text_not_dimmed) {
  LumaImage img = DarkAppText();
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kApp));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 600, DimParams{});
  CHECK(m.Bright(10, 2));  // riga in grassetto: luminosa ma in una finestra ferma
  CHECK(m.Large(10, 2));
  for (float level : m.Levels()) CHECK_NEAR(level, 0.0, 1e-6);
}

TEST(dim_logo_over_video_dims) {
  LumaImage img;
  DimModel m = VideoAt480(img);
  CHECK(m.Bright(48, 5));
  CHECK(!m.Large(48, 5));
  CHECK_NEAR(m.Level(48, 5), 0.40, 1e-4);
  CHECK_NEAR(m.Level(30, 10), 0.0, 1e-6);  // il video stesso non si scurisce
}

TEST(dim_logo_in_static_window_not_dimmed) {
  LumaImage img = VideoWithLogo(0);  // video fermo (in pausa)
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kApp));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 600, DimParams{});
  CHECK(m.Large(48, 5));
  CHECK_NEAR(m.Level(48, 5), 0.0, 1e-6);
}

TEST(dim_taskbar_under_static_window_dims) {
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH - 80, 1.0f);  // pagina bianca ferma nella finestra sopra
  Fill(img, 0, kH - 80, kW, kH, 1.0f);  // barra bianca
  DimModel m(kW, kH, 0);
  m.SetZones(TaskbarZones());
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-4);
  CHECK_NEAR(m.Level(10, 10), 0.0, 1e-6);  // la pagina non si tocca
}

TEST(dim_shell_static_bright_dims_regardless_of_size) {
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);  // sfondo del desktop bianco
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kShell));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  CHECK_NEAR(m.Level(32, 18), 0.40, 1e-4);
}

TEST(dim_without_zones_everything_is_app) {
  LumaImage img = DesktopIcons();
  DimModel m(kW, kH, 0);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  CHECK_NEAR(m.Level(2, 2), 0.0, 1e-6);
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

// Video messo in pausa: il logo resta uguale, cambia solo la classificazione -> 30 s.
TEST(dim_video_pause_recovers_slow) {
  LumaImage img;
  DimModel m = VideoAt480(img);
  Run(m, img, 480.5, 489.5, DimParams{});  // meno di 10 s: ancora movimento recente
  CHECK_NEAR(m.Level(48, 5), 0.40, 1e-3);
  Run(m, img, 490, 498, DimParams{});
  CHECK(m.Large(48, 5));
  CHECK_NEAR(m.Level(48, 5), 0.40 - 0.40 / 30 * 8.5, 0.01);
  Run(m, img, 498.5, 525, DimParams{});
  CHECK_NEAR(m.Level(48, 5), 0.0, 1e-6);
}

TEST(dim_video_pause_with_large_option_recovers_slow) {
  DimParams p;
  p.large_enabled = true;
  LumaImage img;
  DimModel m = VideoAt480(img, p);
  Run(m, img, 480.5, 498, p);
  CHECK_NEAR(m.Level(48, 5), 0.40 - 0.40 / 30 * 8.5, 0.01);
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
  m.SetZones(Zones(kApp));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 1200, p);
  CHECK_NEAR(m.Level(32, 18), 0.0, 1e-6);
  Run(m, img, 1200.5, 1350, p);
  CHECK_NEAR(m.Level(32, 18), 0.10, 0.005);
  Run(m, img, 1350.5, 1500, p);
  CHECK_NEAR(m.Level(32, 18), 0.20, 1e-4);
}

TEST(dim_disabling_large_option_recovers_in_one_second) {
  LumaImage img = Black();
  Fill(img, 0, 0, kW, kH, 1.0f);
  DimParams p;
  p.large_enabled = true;
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kApp));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 1500, p);
  CHECK_NEAR(m.Level(32, 18), 0.20, 1e-4);
  p.large_enabled = false;
  m.Update(1500.125, 0.125, p, false);
  CHECK_NEAR(m.Level(32, 18), 0.10, 1e-3);
}

TEST(dim_partial_cells_at_edges) {
  LumaImage img = Black(1000, 600);
  Fill(img, 992, 592, 1000, 600, 1.0f);
  DimModel m(1000, 600, 0);
  m.SetZones(Zones(kShell, 63, 38));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  CHECK(m.Bright(62, 37));
  CHECK_NEAR(m.Level(62, 37), 0.40, 1e-4);
  CHECK_NEAR(m.Level(61, 37), 0.0, 1e-6);
}

TEST(dim_huge_dt_does_not_overshoot) {
  LumaImage img = BarScene();
  DimModel m(kW, kH, 0);
  m.SetZones(TaskbarZones());
  m.OnFrame(img, 0);
  m.Update(10000, 10000, DimParams{}, false);
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-6);
  CHECK(!std::isnan(m.Level(10, 33)));
  m.Update(10000, 0, DimParams{}, false);
  CHECK_NEAR(m.Level(10, 33), 0.40, 1e-6);
}

TEST(dim_zones_of_wrong_size_ignored) {
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kShell, 10, 10));
  LumaImage img = DesktopIcons();
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  CHECK_NEAR(m.Level(2, 2), 0.0, 1e-6);  // resta "app"
}

// Dopo una pausa (overlay distrutto) le celle uguali riprendono la loro eta'.
TEST(dim_restore_keeps_age_of_unchanged_cells) {
  LumaImage img = DesktopIcons();
  DimModel before(kW, kH, 0);
  before.SetZones(Zones(kShell));
  before.OnFrame(img, 0);
  Run(before, img, 0.5, 480, DimParams{});
  const oled::DimSavedState saved = before.Save(480);
  CHECK_EQ(saved.ages.size(), size_t{kCols * kRows});

  // Pausa di 520 s; al ritorno un'icona e' cambiata.
  Fill(img, 16, 16, 80, 80, 0.7f);
  DimModel after(kW, kH, 1000, &saved);
  after.SetZones(Zones(kShell));
  after.OnFrame(img, 1000);
  after.Update(1000, 0, DimParams{}, false);
  CHECK_NEAR(after.Level(7, 2), 0.0, 1e-6);  // i livelli ripartono da 0
  CHECK_NEAR(after.Target(7, 2), 0.40, 1e-6);  // ma l'eta' e' quella di prima
  CHECK_NEAR(after.Target(2, 2), 0.0, 1e-6);   // icona cambiata: si riparte
  Run(after, img, 1000.5, 1150, DimParams{});
  CHECK_NEAR(after.Level(7, 2), 0.20, 0.01);  // risale con la discesa lenta normale
}

TEST(dim_restore_ignored_on_size_change) {
  LumaImage img = DesktopIcons();
  DimModel before(kW, kH, 0);
  before.SetZones(Zones(kShell));
  before.OnFrame(img, 0);
  Run(before, img, 0.5, 480, DimParams{});
  const oled::DimSavedState saved = before.Save(480);
  DimModel after(1000, 600, 1000, &saved);
  LumaImage other = Black(1000, 600);
  after.SetZones(Zones(kShell, 63, 38));
  after.OnFrame(other, 1000);
  after.Update(1000, 0, DimParams{}, false);
  for (int i = 0; i < 63 * 38; ++i) CHECK_NEAR(after.Target(i % 63, i / 63), 0.0, 1e-9);
}

// Evidenziazione di un'icona al passaggio del mouse (o Visualizzazione attivita'): la cella
// cambia per pochi secondi e torna identica. Torna normale subito, ma conserva l'eta'.
TEST(dim_brief_change_keeps_age) {
  LumaImage img = DesktopIcons();
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kShell));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  CHECK_NEAR(m.Level(2, 2), 0.40, 1e-4);
  const LumaImage original = img;
  Fill(img, 16, 16, 80, 80, 0.9f);  // icona evidenziata
  m.OnFrame(img, 480.5);
  m.Update(480.5, 0.5, DimParams{}, false);
  CHECK_NEAR(m.Level(2, 2), 0.0, 1e-6);  // contenuto diverso: torna normale subito
  img = original;                        // il mouse se ne va dopo 5 s
  m.OnFrame(img, 485.5);
  m.Update(485.5, 0.5, DimParams{}, false);
  CHECK_NEAR(m.Target(2, 2), 0.40, 1e-6);  // eta' ripresa: e' di nuovo da scurire
  CHECK(m.Level(2, 2) < 0.01);             // ma risale piano, senza scatti
  Run(m, img, 486, 635.5, DimParams{});
  CHECK_NEAR(m.Level(2, 2), 0.20, 0.01);
}

TEST(dim_long_change_resets_age) {
  LumaImage img = DesktopIcons();
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kShell));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  const LumaImage original = img;
  Fill(img, 16, 16, 80, 80, 0.9f);
  m.OnFrame(img, 480.5);
  Run(m, img, 480.5, 500, DimParams{});  // diversa per 20 s: contenuto nuovo
  img = original;
  m.OnFrame(img, 500.5);
  m.Update(500.5, 0.5, DimParams{}, false);
  CHECK_NEAR(m.Target(2, 2), 0.0, 1e-9);  // si riparte da zero
}

// Una cella che cambia di continuo (video) non deve mai "riprendere" un'eta' vecchia.
TEST(dim_brief_change_ignores_new_content) {
  LumaImage img = DesktopIcons();
  DimModel m(kW, kH, 0);
  m.SetZones(Zones(kShell));
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{});
  Fill(img, 16, 16, 80, 80, 0.9f);
  m.OnFrame(img, 480.5);
  Fill(img, 16, 16, 80, 80, 0.6f);  // un altro contenuto ancora, non quello di prima
  m.OnFrame(img, 482);
  m.Update(482, 1.5, DimParams{}, false);
  CHECK_NEAR(m.Target(2, 2), 0.0, 1e-9);
}

// Pagina chiara ferma in una finestra accanto a un video in un'altra finestra: il
// movimento dell'altra finestra non conta, la pagina non si scurisce (niente alone).
TEST(dim_motion_of_other_window_does_not_count) {
  LumaImage img = VideoWithLogo(0);
  Fill(img, 0, 0, 320, kH, 1.0f);  // pagina bianca a sinistra del video
  std::vector<uint8_t> zones(static_cast<size_t>(kCols) * kRows, 3);  // finestra del video
  for (int y = 0; y < kRows; ++y)
    for (int x = 0; x < 20; ++x) zones[static_cast<size_t>(y) * kCols + x] = 2;  // finestra della pagina
  DimModel m(kW, kH, 0);
  m.SetZones(zones);
  m.OnFrame(img, 0);
  Run(m, img, 0.5, 480, DimParams{}, PlayVideo);
  CHECK_NEAR(m.Level(18, 10), 0.0, 1e-6);   // pagina vicino al bordo: nessun alone
  CHECK_NEAR(m.Level(48, 5), 0.40, 1e-4);   // il logo sul video si' (stessa finestra)
}

// Riavvio prima del primo frame (es. accesso alla cattura perso subito dopo lo sblocco):
// lo stato da riprendere non va perso.
TEST(dim_save_before_first_frame_keeps_pending_restore) {
  LumaImage img = DesktopIcons();
  DimModel before(kW, kH, 0);
  before.SetZones(Zones(kShell));
  before.OnFrame(img, 0);
  Run(before, img, 0.5, 480, DimParams{});
  const oled::DimSavedState saved = before.Save(480);
  DimModel restarted(kW, kH, 1000, &saved);
  const oled::DimSavedState again = restarted.Save(1001);
  CHECK(again.ages == saved.ages);
  CHECK(again.hashes == saved.hashes);
}

// Icona evidenziata dal mouse proprio quando parte la pausa: si salva il contenuto di
// riferimento, cosi' al ritorno (mouse altrove) la cella riprende l'eta'.
TEST(dim_save_during_brief_change_keeps_anchor) {
  LumaImage img = DesktopIcons();
  DimModel before(kW, kH, 0);
  before.SetZones(Zones(kShell));
  before.OnFrame(img, 0);
  Run(before, img, 0.5, 480, DimParams{});
  const LumaImage original = img;
  Fill(img, 16, 16, 80, 80, 0.9f);
  before.OnFrame(img, 480.5);
  const oled::DimSavedState saved = before.Save(482);
  DimModel after(kW, kH, 1000, &saved);
  after.SetZones(Zones(kShell));
  after.OnFrame(original, 1000);
  after.Update(1000, 0, DimParams{}, false);
  CHECK_NEAR(after.Target(2, 2), 0.40, 1e-6);
}
