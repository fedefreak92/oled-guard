// Mappa delle zone per l'oscuramento: per ogni cella 16x16 px del monitor, desktop/barra
// (shell) oppure finestra di un'applicazione. Logica pura: i rettangoli arrivano gia'
// filtrati dal codice Win32.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "dim_model.h"

namespace oled {

struct ZoneRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

// Coordinate desktop (pixel fisici). Valore per cella: 0 desktop/barra, altrimenti il
// numero della finestra (1-255) piu' in alto che vi mostra almeno un pixel non coperto
// dalla barra delle applicazioni. `apps` in ordine dall'alto (z-order).
std::vector<uint8_t> ComputeDimZones(int cols, int rows, ZoneRect monitor, const std::vector<ZoneRect>& apps,
                                     const std::vector<ZoneRect>& taskbars);

// Desktop e barra delle applicazioni (maiuscole/minuscole ignorate).
bool IsShellWindowClass(std::string_view cls);
// Selettore di Windows (Alt+Tab, Visualizzazione attivita'): ne' app ne' "tutto schermo".
bool IsTaskSwitcherClass(std::string_view cls);
// Finestra layered del tutto trasparente (opacita' 0 con LWA_ALPHA), come l'overlay di
// NVIDIA: invisibile, quindi non e' un'app anche se copre tutto lo schermo.
bool IsInvisibleOverlay(uint32_t ex_style, bool has_layered_attributes, uint32_t lwa_flags, uint8_t alpha);

}  // namespace oled
