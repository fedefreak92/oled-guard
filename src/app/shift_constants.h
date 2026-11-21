// Constant buffer del pixel shader dell'overlay (shift_ps.hlsl), condiviso con i test.
#pragma once

namespace oled {

inline constexpr unsigned kShiftFlagSdr = 1;        // uscita BGRA8 sRGB (altrimenti scRGB lineare)
inline constexpr unsigned kShiftFlagHighlight = 2;  // "Evidenzia sullo schermo"
inline constexpr unsigned kShiftFlagDim = 4;        // oscuramento attivo (texture dei livelli presente)
inline constexpr unsigned kShiftFlagShade = 8;      // luminosita' o bordi scuriti non neutri

struct ShiftConstants {
  int offset[2];
  unsigned size_minus_one[2];
  float grid_size[2];  // dimensioni della griglia in pixel (colonne*16, righe*16)
  float sdr_white;     // valore del bianco nel formato di uscita
  unsigned flags;
  float spread[4];          // bordi scuriti: sinistra, destra, alto, basso (frazioni del lato)
  float brightness;         // moltiplicatore finale (luminosita' x assente)
  float vignette_strength;  // 0-0,8
  float pad[2];
};
static_assert(sizeof(ShiftConstants) == 64);

}  // namespace oled
