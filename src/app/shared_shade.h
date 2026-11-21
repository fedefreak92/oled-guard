// Luminosita', bordi scuriti e modalita' assente condivisi fra il thread UI (che li
// scrive) e il thread di render (che calcola la rampa e ridisegna). Il conteggio dei
// frame nuovi viaggia al contrario e misura il movimento sul desktop.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "shade_model.h"

namespace oled {

struct ShadeInputs {
  ShadeParams base;           // ToShadeParams(config, 1.0)
  double away_since_ms = -1;  // NowMs() dell'ingresso in assente, -1 se non assente
  double away_target = 1;     // away_brightness_pct / 100
  uint64_t version = 0;
};

class SharedShade {
 public:
  // Incrementa la versione se qualcosa e' cambiato (quella passata e' ignorata).
  void Set(const ShadeInputs& in);
  ShadeInputs Get();
  void CountNewFrame() { new_frames_.fetch_add(1, std::memory_order_relaxed); }  // thread di render
  int TakeNewFrames() { return new_frames_.exchange(0, std::memory_order_relaxed); }  // thread UI, 1 volta al secondo

 private:
  std::mutex mutex_;
  ShadeInputs inputs_;
  std::atomic<int> new_frames_{0};
};

}  // namespace oled
