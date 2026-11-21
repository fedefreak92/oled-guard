#include "shift_scheduler.h"

#include <algorithm>
#include <cstdlib>

namespace oled {
namespace {

// Oltre questo ritardo una fase scaduta riparte da "adesso", per evitare raffiche
// di transizioni dopo un buco nelle chiamate.
constexpr double kCatchUpLimitMs = 5000.0;

double Pick(Rng& rng, double lo, double hi) { return hi > lo ? rng.UniformReal(lo, hi) : lo; }

bool Inside(Offset o, int amplitude) { return std::abs(o.x) <= amplitude && std::abs(o.y) <= amplitude; }

}  // namespace

ShiftScheduler::ShiftScheduler(uint64_t seed, int amplitude, ShiftTiming timing, double now_ms)
    : generator_(seed, amplitude),
      rng_(seed ^ 0xD1B54A32D192ED03ull),
      timing_(timing),
      last_now_ms_(now_ms) {
  StartDwell(now_ms);
}

double ShiftScheduler::ClampTime(double now_ms) {
  // Due thread possono leggere l'orologio in ordine diverso: il tempo non torna indietro.
  if (now_ms < last_now_ms_) return last_now_ms_;
  last_now_ms_ = now_ms;
  return now_ms;
}

void ShiftScheduler::StartDwell(double start_ms) {
  phase_ = Phase::Dwell;
  animator_.reset();
  phase_start_ms_ = start_ms;
  phase_end_ms_ = start_ms + Pick(rng_, timing_.dwell_min_ms, timing_.dwell_max_ms);
}

void ShiftScheduler::StartTransition(double start_ms) {
  // Il generatore non ripete il proprio punto; dopo una ripresa a meta' transizione
  // la posizione reale puo' differire, quindi si esclude anche quella.
  Offset target = generator_.NextTarget();
  while (target == offset_) target = generator_.NextTarget();
  const double duration = Pick(rng_, timing_.transition_min_ms, timing_.transition_max_ms);
  animator_.emplace(offset_, target, duration);
  phase_ = Phase::Transition;
  phase_start_ms_ = start_ms;
  phase_end_ms_ = start_ms + duration;
}

void ShiftScheduler::Advance(double now_ms) {
  for (int guard = 0; guard < 1000 && now_ms >= phase_end_ms_; ++guard) {
    const double next_start = now_ms - phase_end_ms_ > kCatchUpLimitMs ? now_ms : phase_end_ms_;
    if (phase_ == Phase::Dwell) {
      StartTransition(next_start);
    } else {
      offset_ = animator_->To();
      StartDwell(next_start);
    }
  }
  if (phase_ == Phase::Transition) offset_ = animator_->At(now_ms - phase_start_ms_);
}

double ShiftScheduler::SchedulerTime(double now_ms) {
  // In pausa il tempo dello scheduler e' fermo all'istante della pausa: modifiche fatte
  // durante la pausa non devono far scorrere le fasi (Resume aggiunge poi l'intervallo).
  if (paused_at_ms_) return *paused_at_ms_;
  return ClampTime(now_ms);
}

Offset ShiftScheduler::OffsetAt(double now_ms) {
  Advance(SchedulerTime(now_ms));
  return offset_;
}

double ShiftScheduler::NextChangeAt(double now_ms) {
  const double now = SchedulerTime(now_ms);
  Advance(now);
  if (phase_ == Phase::Transition) {
    if (auto step = animator_->NextStepAt(now - phase_start_ms_)) return phase_start_ms_ + *step;
  }
  return phase_end_ms_;
}

void ShiftScheduler::Reconfigure(int amplitude, ShiftTiming timing, double now_ms) {
  const double now = SchedulerTime(now_ms);
  Advance(now);
  generator_.SetAmplitude(amplitude);
  timing_ = timing;
  const int a = generator_.Amplitude();
  const bool target_outside = phase_ == Phase::Transition && !Inside(animator_->To(), a);
  // Posizione o destinazione fuori dai nuovi limiti: si rientra subito.
  if (!Inside(offset_, a) || target_outside) StartTransition(now);
}

void ShiftScheduler::Pause(double now_ms) {
  const double now = ClampTime(now_ms);
  Advance(now);
  paused_at_ms_ = now;
}

void ShiftScheduler::Resume(double now_ms) {
  const double now = ClampTime(now_ms);
  if (!paused_at_ms_) {
    // offset_ e' l'ultima posizione mostrata: si riparte da li' con una sosta nuova.
    StartDwell(now);
    return;
  }
  // La fase slitta della durata della pausa: stessa posizione, stesso tempo residuo.
  // Pause brevi e frequenti non congelano quindi lo spostamento.
  const double gap = now - *paused_at_ms_;
  phase_start_ms_ += gap;
  phase_end_ms_ += gap;
  paused_at_ms_.reset();
}

void ShiftScheduler::MoveSoon(double now_ms) {
  const double now = SchedulerTime(now_ms);
  Advance(now);
  if (phase_ == Phase::Dwell && phase_end_ms_ > now + 500) phase_end_ms_ = now + 500;
}

void SharedShift::MoveSoon(double now_ms) {
  std::lock_guard lock(mutex_);
  scheduler_.MoveSoon(now_ms);
}

SharedShift::SharedShift(uint64_t seed, int amplitude, ShiftTiming timing, double now_ms)
    : scheduler_(seed, amplitude, timing, now_ms) {}

Offset SharedShift::OffsetAt(double now_ms) {
  std::lock_guard lock(mutex_);
  return scheduler_.OffsetAt(now_ms);
}

double SharedShift::NextChangeAt(double now_ms) {
  std::lock_guard lock(mutex_);
  return scheduler_.NextChangeAt(now_ms);
}

void SharedShift::Reconfigure(int amplitude, ShiftTiming timing, double now_ms) {
  std::lock_guard lock(mutex_);
  scheduler_.Reconfigure(amplitude, timing, now_ms);
}

void SharedShift::Pause(double now_ms) {
  std::lock_guard lock(mutex_);
  scheduler_.Pause(now_ms);
}

void SharedShift::Resume(double now_ms) {
  std::lock_guard lock(mutex_);
  scheduler_.Resume(now_ms);
}

}  // namespace oled
