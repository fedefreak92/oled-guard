// Offset del pixel shift: generatore del random walk e animazione a passi interi.
#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <vector>

#include "rng.h"

namespace oled {

struct Offset {
  int x = 0;
  int y = 0;
  bool operator==(const Offset&) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const Offset& o) {
  return os << '(' << o.x << ',' << o.y << ')';
}

// Random walk limitato: |x|, |y| <= ampiezza, mai due volte lo stesso punto di fila.
class OffsetGenerator {
 public:
  OffsetGenerator(uint64_t seed, int amplitude);

  Offset Current() const { return current_; }
  // Sceglie uniformemente un nuovo punto della griglia diverso da quello corrente.
  Offset NextTarget();
  void SetAmplitude(int amplitude);
  int Amplitude() const { return amplitude_; }

 private:
  Rng rng_;
  int amplitude_;
  Offset current_{};
};

}  // namespace oled
