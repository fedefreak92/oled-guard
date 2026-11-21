// Parametri dell'oscuramento delle aree statiche, gia' convertiti in unita' comode
// (secondi, frazioni) per il modello di riferimento e per gli shader.
#pragma once

#include "config.h"

namespace oled {

struct DimParams {
  bool enabled = true;
  double strength = 0.40;  // riduzione massima della luminanza, 0-1
  double delay_s = 180;
  double ramp_s = 300;
  double recover_s = 0.5;
  bool large_enabled = false;
  double large_delay_s = 1200;
  double large_strength = 0.20;
  double bright_threshold = 0.25;  // frazione del bianco SDR (luce lineare)
  double large_fraction = 0.40;

  bool operator==(const DimParams&) const = default;
};

DimParams ToDimParams(const Config& c);

inline constexpr int kDimCellPx = 16;
inline constexpr int kDimWindowRadius = 16;  // finestre di 33x33 celle
inline constexpr double kDimBrightPixelFraction = 0.125;
inline constexpr double kDimDetailRange = 0.10;
inline constexpr double kDimMaxLevel = 0.8;  // limite superiore di strength
inline constexpr double kDimCommandRecoverS = 1.0;
inline constexpr double kDimLargeRecoverS = 30.0;

}  // namespace oled
