// Alternanza sosta/transizione dell'offset. Il tempo e' sempre passato come
// argomento (ms), mai letto dall'orologio: tutto e' testabile.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "offset.h"
#include "rng.h"

namespace oled {

struct ShiftTiming {
  double dwell_min_ms = 20000;
  double dwell_max_ms = 60000;
  double transition_min_ms = 2000;
  double transition_max_ms = 5000;
};

class ShiftScheduler {
 public:
  ShiftScheduler(uint64_t seed, int amplitude, ShiftTiming timing, double now_ms);

  Offset OffsetAt(double now_ms);
  // Istante assoluto (ms) del prossimo possibile cambio di offset.
  double NextChangeAt(double now_ms);
  void Reconfigure(int amplitude, ShiftTiming timing, double now_ms);
  // Overlay distrutto: il tempo della fase corrente si ferma.
  void Pause(double now_ms);
  // Overlay ricreato: la fase riprende da dove era, slittata della durata della pausa.
  // Senza una Pause precedente riparte con una sosta nuova dalla posizione corrente.
  void Resume(double now_ms);
  // Se in sosta, la prossima transizione parte entro 500 ms ("Prova ora").
  void MoveSoon(double now_ms);

 private:
  enum class Phase { Dwell, Transition };

  double ClampTime(double now_ms);
  double SchedulerTime(double now_ms);
  void Advance(double now_ms);
  void StartDwell(double start_ms);
  void StartTransition(double start_ms);

  OffsetGenerator generator_;
  Rng rng_;
  ShiftTiming timing_;
  Phase phase_ = Phase::Dwell;
  double phase_start_ms_ = 0;
  double phase_end_ms_ = 0;
  Offset offset_{};
  std::optional<OffsetAnimator> animator_;
  double last_now_ms_ = 0;
  std::optional<double> paused_at_ms_;
};

// Stessa API di ShiftScheduler, protetta da mutex: usata dal thread UI e dal render.
class SharedShift {
 public:
  SharedShift(uint64_t seed, int amplitude, ShiftTiming timing, double now_ms);

  Offset OffsetAt(double now_ms);
  double NextChangeAt(double now_ms);
  void Reconfigure(int amplitude, ShiftTiming timing, double now_ms);
  void Pause(double now_ms);
  void Resume(double now_ms);
  void MoveSoon(double now_ms);

 private:
  std::mutex mutex_;
  ShiftScheduler scheduler_;
};

}  // namespace oled
