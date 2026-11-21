// Constant buffer del pixel shader dell'overlay (shift_ps.hlsl), condiviso con i test.
#pragma once

namespace oled {

inline constexpr unsigned kShiftFlagSdr = 1;        // uscita BGRA8 sRGB (altrimenti scRGB lineare)
inline constexpr unsigned kShiftFlagHighlight = 2;  // "Evidenzia sullo schermo"
inline constexpr unsigned kShiftFlagDim = 4;        // oscuramento attivo (texture dei livelli presente)

struct ShiftConstants {
  int offset[2];
  unsigned size_minus_one[2];
  float grid_size[2];  // dimensioni della griglia in pixel (colonne*16, righe*16)
  float sdr_white;     // valore del bianco nel formato di uscita
  unsigned flags;
};
static_assert(sizeof(ShiftConstants) == 32);

}  // namespace oled
