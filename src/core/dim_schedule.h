// Quando ridisegnare l'overlay per far avanzare l'oscuramento anche senza frame
// nuovi (un desktop fermo non ne produce). Tempo sempre passato come argomento (ms).
#pragma once

#include <limits>

#include "dim_params.h"

namespace oled {

inline constexpr double kDimSlowRedrawMs = 500;

class DimRedrawSchedule {
 public:
  void OnNewFrame(double now_ms) { last_frame_ms_ = now_ms; }
  // Parametri cambiati, sospensione, evidenziazione accesa o spenta.
  void Poke(double now_ms) { last_poke_ms_ = now_ms; }
  void OnRedraw(double now_ms) { last_redraw_ms_ = now_ms; }
  // Istante assoluto del prossimo ridisegno dovuto all'oscuramento; +infinito se nessuno.
  double NextRedrawAt(double now_ms, const DimParams& p, double frame_period_ms) const;

 private:
  static constexpr double kNever = -std::numeric_limits<double>::infinity();
  double last_frame_ms_ = kNever;
  double last_poke_ms_ = kNever;
  double last_redraw_ms_ = kNever;
};

}  // namespace oled
