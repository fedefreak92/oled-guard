// Luminosita' via software e bordi scuriti: fattore moltiplicativo in luce lineare.
// Riferimento su CPU dello shader dell'overlay (shift_ps.hlsl), che usa la stessa formula.
#pragma once

#include "config.h"

namespace oled {

struct ShadeParams {
  float brightness = 1;            // 0-1, gia' moltiplicato per il fattore assente
  float vignette_strength = 0;     // 0-0,8 (0 se i bordi sono spenti)
  float spread[4] = {0, 0, 0, 0};  // sinistra, destra, alto, basso: frazioni del lato 0-0,4
  bool operator==(const ShadeParams&) const = default;
};

ShadeParams ToShadeParams(const Config& config, double away_factor);
// Nessun effetto: brightness 1 e bordi a intensita' 0.
bool ShadeIsNeutral(const ShadeParams& p);
// Fattore nel punto (u, v) dello schermo, 0-1 ciascuno (0,0 = angolo in alto a sinistra).
float ShadeFactor(const ShadeParams& p, float u, float v);

}  // namespace oled
