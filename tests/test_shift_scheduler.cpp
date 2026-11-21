#include <cstdlib>

#include "shift_scheduler.h"
#include "test_framework.h"

using oled::Offset;
using oled::SharedShift;
using oled::ShiftScheduler;
using oled::ShiftTiming;

namespace {
const ShiftTiming kT{20000, 60000, 2000, 5000};
}

TEST(scheduler_holds_origin_during_first_dwell) {
  ShiftScheduler s(1, 2, kT, 0);
  for (double t = 0; t < 20000; t += 100) CHECK_EQ(s.OffsetAt(t), (Offset{0, 0}));
}

TEST(scheduler_moves_within_dwell_plus_transition) {
  // Interrogato ogni 500 ms come fa il render (una sola chiamata a 65 s sarebbe
  // trattata come un buco e la fase ripartirebbe da "adesso").
  ShiftScheduler s(1, 2, kT, 0);
  for (double t = 0; t < 65000; t += 500) s.OffsetAt(t);
  CHECK(!(s.OffsetAt(65000) == (Offset{0, 0})));
}

TEST(scheduler_phase_durations_within_limits) {
  // Simula 2 ore a passi di 50 ms. Gli intervalli fra un cambio e l'altro sono
  // passi di una transizione (< 5 s) oppure soste (20-60 s, piu' le code delle
  // transizioni adiacenti).
  ShiftScheduler s(21, 2, kT, 0);
  Offset prev = s.OffsetAt(0);
  double last_change = 0;
  int dwells = 0;
  for (double t = 50; t <= 7.2e6; t += 50) {
    const Offset o = s.OffsetAt(t);
    CHECK(std::abs(o.x) <= 2 && std::abs(o.y) <= 2);
    if (!(o == prev)) {
      CHECK(std::abs(o.x - prev.x) <= 1 && std::abs(o.y - prev.y) <= 1);
      const double gap = t - last_change;
      if (gap >= 5050) {
        CHECK(gap >= 20000 - 50 && gap <= 60000 + 5000 + 50);
        ++dwells;
      }
      last_change = t;
      prev = o;
    }
  }
  // Fra 2 ore / (60 + 5) s e 2 ore / 20 s.
  CHECK(dwells >= 100 && dwells <= 360);
}

TEST(scheduler_next_change_is_when_offset_changes) {
  ShiftScheduler s(4, 2, kT, 0);
  double t = 0;
  int changes = 0;
  for (int i = 0; i < 200; ++i) {
    const double n = s.NextChangeAt(t);
    CHECK(n > t);
    const Offset o = s.OffsetAt(t);
    CHECK_EQ(s.OffsetAt(n - 1), o);
    if (!(s.OffsetAt(n) == o)) ++changes;
    t = n;
  }
  CHECK(changes > 20);
}

TEST(scheduler_resume_keeps_offset) {
  ShiftScheduler s(8, 2, kT, 0);
  for (double t = 0; t <= 200000; t += 500) s.OffsetAt(t);
  const Offset before = s.OffsetAt(200000);
  s.Pause(200000);
  const double resumed = 200000 + 7.2e6;
  s.Resume(resumed);
  CHECK_EQ(s.OffsetAt(resumed), before);
}

TEST(scheduler_pause_freezes_phase_timing) {
  ShiftScheduler s(14, 2, kT, 0);
  const double next_before = s.NextChangeAt(1000);
  s.Pause(1000);
  s.Resume(1000 + 3.6e6);
  // Il tempo trascorso in pausa non conta: il prossimo cambio slitta della durata della pausa.
  CHECK_NEAR(s.NextChangeAt(1000 + 3.6e6), next_before + 3.6e6, 1e-6);
  CHECK_EQ(s.OffsetAt(1000 + 3.6e6), (Offset{0, 0}));
}

TEST(scheduler_short_pause_cycles_still_progress) {
  // Pause frequenti piu' brevi della sosta minima (es. alt-tab in un gioco ogni 5 s):
  // lo spostamento deve comunque procedere col tempo attivo accumulato.
  ShiftScheduler s(15, 2, kT, 0);
  const Offset start = s.OffsetAt(0);
  bool moved = false;
  double t = 0;
  for (int cycle = 0; cycle < 40 && !moved; ++cycle) {
    for (int i = 0; i < 10; ++i, t += 500) moved = moved || !(s.OffsetAt(t) == start);
    s.Pause(t);
    t += 5000;
    s.Resume(t);
  }
  CHECK(moved);
}

TEST(scheduler_resume_without_pause_is_safe) {
  ShiftScheduler s(16, 2, kT, 0);
  const Offset a = s.OffsetAt(1000);
  s.Resume(2000);
  CHECK_EQ(s.OffsetAt(2000), a);
}

TEST(scheduler_tolerates_time_going_backwards) {
  ShiftScheduler s(9, 2, kT, 0);
  const Offset a = s.OffsetAt(100000);
  CHECK_EQ(s.OffsetAt(50000), a);
  CHECK(s.NextChangeAt(50000) >= 100000);
}

TEST(scheduler_long_gap_does_not_burst) {
  // Senza Resume, un buco di 10 minuti non deve far scorrere decine di transizioni:
  // la fase successiva parte da "adesso".
  ShiftScheduler s(12, 2, kT, 0);
  const Offset a = s.OffsetAt(1000);
  const Offset b = s.OffsetAt(600000);
  CHECK(std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1);
}

TEST(scheduler_reconfigure_shrinks_amplitude) {
  ShiftScheduler s(10, 3, kT, 0);
  double t = 0;
  while (std::abs(s.OffsetAt(t).x) < 2 && std::abs(s.OffsetAt(t).y) < 2) t += 1000;
  s.Reconfigure(1, kT, t);
  const Offset o = s.OffsetAt(t + 6000);
  CHECK(std::abs(o.x) <= 1 && std::abs(o.y) <= 1);
}

TEST(scheduler_move_soon_shortens_dwell) {
  // "Prova ora": la prossima mossa deve partire entro mezzo secondo anche a meta' sosta.
  ShiftScheduler s(17, 2, kT, 0);
  s.OffsetAt(1000);
  s.MoveSoon(1000);
  CHECK(s.NextChangeAt(1000) <= 1500);
  // Durante una transizione non cambia nulla.
  for (double t = 1000; t < 1600; t += 50) s.OffsetAt(t);
  const double during = s.NextChangeAt(1600);
  s.MoveSoon(1600);
  CHECK_NEAR(s.NextChangeAt(1600), during, 1e-9);
}

TEST(scheduler_move_soon_during_pause_applies_at_resume) {
  // Pausa di 5 minuti, "Prova ora" a meta' pausa: alla ripresa la mossa parte subito,
  // non dopo un tempo pari alla pausa.
  ShiftScheduler s(18, 2, kT, 0);
  for (double t = 0; t <= 10000; t += 500) s.OffsetAt(t);
  s.Pause(10000);
  s.MoveSoon(160000);
  s.Resume(310000);
  CHECK(s.NextChangeAt(310000) <= 310500);
}

TEST(scheduler_reconfigure_during_pause_keeps_frozen_time) {
  // Una modifica della config durante una pausa lunga non deve spostare in avanti la
  // fase: dopo la ripresa la prossima mossa arriva entro la sosta massima.
  ShiftScheduler s(19, 2, kT, 0);
  for (double t = 0; t <= 10000; t += 500) s.OffsetAt(t);
  s.Pause(10000);
  s.Reconfigure(2, kT, 3.6e6);
  s.Resume(7.2e6);
  CHECK(s.NextChangeAt(7.2e6) <= 7.2e6 + 60000 + 1);
}

TEST(shared_shift_matches_scheduler) {
  ShiftScheduler s(33, 2, kT, 0);
  SharedShift sh(33, 2, kT, 0);
  for (double t = 0; t < 600000; t += 250) {
    CHECK_EQ(sh.OffsetAt(t), s.OffsetAt(t));
    CHECK_EQ(sh.NextChangeAt(t), s.NextChangeAt(t));
  }
}
