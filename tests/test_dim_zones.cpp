#include <vector>

#include "dim_zones.h"
#include "test_framework.h"

using oled::ZoneRect;

namespace {
uint8_t At(const std::vector<uint8_t>& z, int cols, int x, int y) { return z[static_cast<size_t>(y) * cols + x]; }
}  // namespace

TEST(dim_zones_desktop_only_is_shell) {
  const auto z = oled::ComputeDimZones(10, 5, {0, 0, 160, 80}, {}, {});
  CHECK_EQ(z.size(), size_t{50});
  for (uint8_t v : z) CHECK_EQ(v, oled::kDimZoneShell);
}

bool IsApp(const std::vector<uint8_t>& z, int cols, int x, int y) { return At(z, cols, x, y) != oled::kDimZoneShell; }

TEST(dim_zones_window_cells_are_app) {
  // Finestra da (32,16) a (96,64): colonne 2-5, righe 1-3.
  const auto z = oled::ComputeDimZones(10, 5, {0, 0, 160, 80}, {{32, 16, 96, 64}}, {});
  CHECK(IsApp(z, 10, 2, 1));
  CHECK(IsApp(z, 10, 5, 3));
  CHECK(!IsApp(z, 10, 1, 1));
  CHECK(!IsApp(z, 10, 6, 1));
  CHECK(!IsApp(z, 10, 2, 4));
}

// Una cella con anche solo un pezzo di finestra e' "app" (prudente: niente strisce
// scurite sul bordo delle finestre).
TEST(dim_zones_partial_cells_are_app) {
  const auto z = oled::ComputeDimZones(10, 5, {0, 0, 160, 80}, {{41, 25, 87, 55}}, {});
  CHECK(IsApp(z, 10, 2, 1));  // il centro (40,24) e' fuori, ma la cella ha pixel della finestra
  CHECK(IsApp(z, 10, 5, 3));
  CHECK(!IsApp(z, 10, 6, 1));
}

TEST(dim_zones_taskbar_wins_over_windows) {
  // Finestra a tutto schermo con la barra sopra nelle ultime 16 righe di pixel.
  const auto z = oled::ComputeDimZones(10, 5, {0, 0, 160, 80}, {{0, 0, 160, 80}}, {{0, 64, 160, 80}});
  CHECK(!IsApp(z, 10, 3, 4));
  CHECK(IsApp(z, 10, 3, 3));
}

// Barra che inizia a meta' di una cella (es. 1080p con barra di 48 px): la cella mista
// mostra anche la finestra, quindi resta "app".
TEST(dim_zones_taskbar_mid_cell_keeps_window_cells) {
  const auto z = oled::ComputeDimZones(10, 5, {0, 0, 160, 80}, {{0, 0, 160, 80}}, {{0, 72, 160, 80}});
  CHECK(IsApp(z, 10, 3, 4));
}

// Ogni finestra ha il suo numero: il movimento conta solo dentro la stessa finestra.
TEST(dim_zones_windows_have_ids) {
  // Finestre in ordine dall'alto: la prima vince dove si sovrappongono.
  const auto z = oled::ComputeDimZones(10, 5, {0, 0, 160, 80}, {{0, 0, 64, 80}, {32, 0, 160, 80}}, {});
  CHECK(IsApp(z, 10, 1, 1));
  CHECK(IsApp(z, 10, 8, 1));
  CHECK(At(z, 10, 1, 1) != At(z, 10, 8, 1));
  CHECK_EQ(At(z, 10, 3, 1), At(z, 10, 1, 1));  // sovrapposizione: la finestra sopra
}

TEST(dim_zones_use_monitor_origin) {
  // Monitor a destra di un altro: coordinate desktop da 1000 in poi.
  const auto z = oled::ComputeDimZones(10, 5, {1000, 0, 1160, 80}, {{1000, 0, 1016, 16}}, {});
  CHECK(IsApp(z, 10, 0, 0));
  CHECK(!IsApp(z, 10, 1, 0));
}

TEST(dim_zones_shell_classes) {
  CHECK(oled::IsShellWindowClass("Progman"));
  CHECK(oled::IsShellWindowClass("WorkerW"));
  CHECK(oled::IsShellWindowClass("Shell_TrayWnd"));
  CHECK(oled::IsShellWindowClass("shell_secondarytraywnd"));
  CHECK(!oled::IsShellWindowClass("Chrome_WidgetWin_1"));
}

TEST(dim_zones_switcher_classes) {
  // Alt+Tab e Visualizzazione attivita': finestre explorer a tutto schermo, non app.
  CHECK(oled::IsTaskSwitcherClass("XamlExplorerHostIslandWindow"));
  CHECK(oled::IsTaskSwitcherClass("MultitaskingViewFrame"));
  CHECK(oled::IsTaskSwitcherClass("TaskSwitcherWnd"));
  CHECK(oled::IsTaskSwitcherClass("ForegroundStaging"));
  CHECK(!oled::IsTaskSwitcherClass("CabinetWClass"));  // Esplora file e' una finestra vera
}

TEST(dim_zones_fully_transparent_overlay_is_not_app) {
  // Overlay NVIDIA (CEF-OSC-WIDGET): layered con opacita' 0, copre tutto lo schermo.
  const uint32_t layered = 0x00080000;  // WS_EX_LAYERED
  const uint32_t lwa_alpha = 0x2;       // LWA_ALPHA
  CHECK(oled::IsInvisibleOverlay(0x08080080, true, lwa_alpha, 0));
  CHECK(!oled::IsInvisibleOverlay(0x08080080, true, lwa_alpha, 200));  // semitrasparente: visibile
  CHECK(!oled::IsInvisibleOverlay(layered, false, 0, 0));  // alpha per pixel: non si sa, conta come app
  CHECK(!oled::IsInvisibleOverlay(0, true, lwa_alpha, 0));  // non layered
}
