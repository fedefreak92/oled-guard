// Modello di riferimento su CPU dell'oscuramento delle aree statiche.
// Non viene usato a runtime (lo fanno gli shader sulla GPU): definisce il
// comportamento, e' coperto dai test e fa da oracolo per i test degli shader.
#pragma once

#include <cstdint>
#include <vector>

#include "dim_params.h"

namespace oled {

// Luminanza lineare di ogni pixel divisa per il bianco SDR (1 = bianco di Windows).
struct LumaImage {
  int width = 0;
  int height = 0;
  std::vector<float> luma;
};

class DimModel {
 public:
  // Tutte le celle partono "cambiate adesso" e con livello 0.
  DimModel(int width, int height, double now_s);

  // Frame nuovo: le celle con almeno un pixel diverso dal frame precedente
  // risultano cambiate adesso. Il primo frame cambia tutte le celle.
  void OnFrame(const LumaImage& frame, double now_s);
  // Classificazione e avanzamento del livello verso il bersaglio.
  void Update(double now_s, double dt_s, const DimParams& p, bool suspended);

  int Cols() const { return cols_; }
  int Rows() const { return rows_; }
  float Level(int cx, int cy) const { return levels_[Index(cx, cy)]; }
  bool Bright(int cx, int cy) const { return bright_[Index(cx, cy)] != 0; }
  bool Content(int cx, int cy) const { return content_[Index(cx, cy)] != 0; }
  bool Large(int cx, int cy) const { return large_[Index(cx, cy)] != 0; }
  double Target(int cx, int cy) const { return targets_[Index(cx, cy)]; }
  const std::vector<float>& Levels() const { return levels_; }

 private:
  size_t Index(int cx, int cy) const { return static_cast<size_t>(cy) * cols_ + cx; }

  int width_;
  int height_;
  int cols_;
  int rows_;
  double bright_threshold_ = 0.25;
  std::vector<float> previous_;
  std::vector<double> last_change_;
  std::vector<uint8_t> bright_;
  std::vector<uint8_t> content_;
  std::vector<uint8_t> large_;
  // Bit 0-1: ultima classe (0 nessuna, 1 elemento piccolo, 2 grande area);
  // bit 2: ritorno lento in corso (la classe e' cambiata con il livello sopra il bersaglio).
  std::vector<uint8_t> class_;
  std::vector<double> targets_;
  std::vector<float> levels_;
};

// Somma/media e massimo su finestre (2r+1)x(2r+1) tagliate ai bordi della griglia,
// con filtri separabili. Usati dal modello e utili ai test.
std::vector<double> WindowMean(const std::vector<double>& grid, int cols, int rows, int radius);
std::vector<double> WindowMax(const std::vector<double>& grid, int cols, int rows, int radius);

}  // namespace oled
