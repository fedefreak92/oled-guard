// Statistiche di usura: tempo trascorso su ogni posizione dell'offset, tempo protetto,
// pause del giorno. Logica pura: il giorno e il tempo arrivano dall'esterno.
#pragma once

#include <array>
#include <optional>

#include "json.h"
#include "offset.h"
#include "pause_rules.h"

namespace oled {

class WearStats {
 public:
  static constexpr int kRadius = 3;
  static constexpr int kSide = 2 * kRadius + 1;
  static constexpr int kReasons = kPauseReasonCount;

  // Tempo con l'overlay a schermo nella posizione `at` (fuori da +-3: ignorato).
  void AddProtectedTime(Offset at, double ms);
  void CountPause(PauseReason reason);
  // day_key = AAAAMMGG; se cambia, i valori di "oggi" ripartono da zero.
  void RollDay(int day_key);

  double CellMs(Offset at) const;  // cumulativo, tutti i giorni
  double TodayProtectedMs() const { return today_ms_; }
  double TotalProtectedMs() const { return total_ms_; }
  int TodayPauses() const;
  int TodayPauses(PauseReason reason) const;
  // 100 x (1 - deviazione/media) sulle celle dentro +-amplitude; nullopt sotto 10 minuti.
  std::optional<double> Uniformity(int amplitude) const;

  JsonValue ToJson() const;
  // Campi mancanti, di tipo sbagliato o negativi vengono ignorati.
  static WearStats FromJson(const JsonValue& v);

 private:
  static int Index(Offset at);

  std::array<double, kSide * kSide> cells_{};
  std::array<int, kReasons> pauses_today_{};
  int day_key_ = 0;
  double today_ms_ = 0;
  double total_ms_ = 0;
};

}  // namespace oled
