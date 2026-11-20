// Generatore pseudo-casuale deterministico (xoshiro256**), seed iniettabile.
// Non usa le distribuzioni della libreria standard, che non sono portabili.
#pragma once

#include <cstdint>

namespace oled {

class Rng {
 public:
  explicit Rng(uint64_t seed);

  uint64_t Next();
  // Intero uniforme in [lo, hi], estremi inclusi.
  int UniformInt(int lo, int hi);
  // Reale uniforme in [lo, hi).
  double UniformReal(double lo, double hi);

 private:
  uint64_t state_[4];
};

}  // namespace oled
