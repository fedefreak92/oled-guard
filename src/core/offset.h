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

// Transizione da `from` a `to` a passi di 1 px (diagonali ammessi), distribuiti nel
// tempo con una curva smoothstep. Mai offset frazionari: il testo resta nitido.
class OffsetAnimator {
 public:
  OffsetAnimator(Offset from, Offset to, double duration_ms);

  // t_ms relativo all'inizio della transizione.
  Offset At(double t_ms) const;
  // Istante (relativo, ms interi) del prossimo cambio di posizione dopo t_ms;
  // nullopt se la transizione e' finita.
  std::optional<double> NextStepAt(double t_ms) const;
  int StepCount() const { return steps_; }
  double Duration() const { return duration_ms_; }
  Offset To() const { return to_; }

 private:
  int IndexAt(double t_ms) const;
  Offset PointAt(int index) const;

  Offset from_;
  Offset to_;
  double duration_ms_;
  int steps_;
};

}  // namespace oled
