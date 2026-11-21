// Scene sintetiche ma realistiche per i test dell'oscuramento (modello e shader).
// 1024x576 px = 64x36 celle. Zone: 0 shell (desktop e barra), 1 finestra di un'app.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "dim_model.h"

namespace oled::scenes {

constexpr int kW = 1024;
constexpr int kH = 576;
constexpr int kCols = 64;
constexpr int kRows = 36;
constexpr uint8_t kShell = 0;
constexpr uint8_t kApp = 1;

inline LumaImage Black(int w = kW, int h = kH) {
  LumaImage img;
  img.width = w;
  img.height = h;
  img.luma.assign(static_cast<size_t>(w) * h, 0.0f);
  return img;
}

inline void Fill(LumaImage& img, int x0, int y0, int x1, int y1, float v) {
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) img.luma[static_cast<size_t>(y) * img.width + x] = v;
}

// Righe di "testo": trattini alti h px, `on` accesi e `off` spenti, da x0 a x1.
inline void TextLine(LumaImage& img, int x0, int x1, int y, int h, int on, int off, float v) {
  for (int x = x0; x < x1; x += on + off) Fill(img, x, y, std::min(x + on, x1), y + h, v);
}

// Desktop: 4 colonne x 5 righe di icone 64x64 colorate (luminanza 0,5) con l'etichetta
// bianca sotto, su sfondo nero. Come la colonna di icone a sinistra del desktop reale.
inline LumaImage DesktopIcons() {
  LumaImage img = Black();
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 5; ++r) {
      const int x = 16 + 96 * c, y = 16 + 112 * r;
      Fill(img, x, y, x + 64, y + 64, 0.5f);
      TextLine(img, x, x + 64, y + 72, 3, 6, 4, 1.0f);
      TextLine(img, x + 8, x + 56, y + 80, 3, 6, 4, 1.0f);
    }
  }
  return img;
}

// Interfaccia scura con testo chiaro sparso (come la finestra di OLED Guard): una riga
// ogni 48 px, una su tre in grassetto (abbastanza pixel per essere "luminosa").
inline LumaImage DarkAppText() {
  LumaImage img = Black();
  for (int k = 0; 48 * k + 40 < kH; ++k) {
    const int h = k % 3 == 0 ? 5 : 2;
    TextLine(img, 64, 900, 40 + 48 * k, h, 7, 3, 0.85f);
  }
  return img;
}

// Video 512x288 in (320,64) che cambia a ogni istante, con un logo bianco 48x32 fermo
// nell'angolo in alto a destra (celle 48-49, righe 5-6).
inline void VideoFrame(LumaImage& img, double t) {
  for (int y = 64; y < 352; ++y) {
    for (int x = 320; x < 832; ++x) {
      if (x >= 768 && x < 816 && y >= 80 && y < 112) continue;  // logo
      img.luma[static_cast<size_t>(y) * img.width + x] =
          static_cast<float>(std::fmod(t * 0.37 + (x * 3 + y * 7) * 0.0013, 0.9));
    }
  }
}
inline LumaImage VideoWithLogo(double t) {
  LumaImage img = Black();
  Fill(img, 768, 80, 816, 112, 1.0f);
  VideoFrame(img, t);
  return img;
}

inline std::vector<uint8_t> Zones(uint8_t z, int cols = kCols, int rows = kRows) {
  return std::vector<uint8_t>(static_cast<size_t>(cols) * rows, z);
}

// Barra delle applicazioni (shell) nelle ultime `rows_bar` righe, il resto app.
inline std::vector<uint8_t> TaskbarZones(int rows_bar = 5) {
  auto z = Zones(kApp);
  for (int y = kRows - rows_bar; y < kRows; ++y)
    for (int x = 0; x < kCols; ++x) z[static_cast<size_t>(y) * kCols + x] = kShell;
  return z;
}

}  // namespace oled::scenes
