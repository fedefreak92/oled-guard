// Avviso nella barra delle applicazioni: il rischio NON protetto (zone ferme e luminose
// che l'oscuramento non sta scurendo) resta sopra la soglia per troppo tempo.
#pragma once

#include <optional>
#include <string_view>

namespace oled {

inline constexpr double kAlertResetBelowMs = 120000;  // 2 minuti di fila sotto soglia azzerano
inline constexpr double kAlertMinGapMs = 3600000;     // al massimo un avviso all'ora

double AlertThreshold(std::string_view level);  // low 0.10, medium 0.20, high 0.35 (altro: medium)

struct AlertParams {
  bool enabled = true;
  double threshold = 0.20;
  int after_min = 60;
};

class AlertModel {
 public:
  // unprotected_risk: nullopt se non misurato (pausa) = sotto soglia.
  // Vero = mostrare l'avviso adesso.
  bool Update(double now_ms, std::optional<double> unprotected_risk, const AlertParams& p);
  double AboveForMs(double now_ms) const;  // 0 se il conteggio non e' partito

 private:
  double above_since_ms_ = -1;
  double below_since_ms_ = -1;
  double last_fired_ms_ = -1;
};

}  // namespace oled
