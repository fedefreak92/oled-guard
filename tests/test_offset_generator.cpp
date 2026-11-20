#include <cstdlib>

#include "offset.h"
#include "test_framework.h"

using oled::Offset;
using oled::OffsetGenerator;

TEST(generator_starts_at_origin) {
  OffsetGenerator g(1, 2);
  CHECK_EQ(g.Current(), (Offset{0, 0}));
}

TEST(generator_never_repeats_consecutively) {
  OffsetGenerator g(3, 1);
  Offset prev = g.Current();
  for (int i = 0; i < 10000; ++i) {
    const Offset t = g.NextTarget();
    CHECK(!(t == prev));
    CHECK_EQ(g.Current(), t);
    prev = t;
  }
}

TEST(generator_stays_within_amplitude) {
  for (int a = 1; a <= 3; ++a) {
    OffsetGenerator g(5, a);
    for (int i = 0; i < 5000; ++i) {
      const Offset t = g.NextTarget();
      CHECK(std::abs(t.x) <= a && std::abs(t.y) <= a);
    }
  }
}

TEST(generator_is_deterministic_with_seed) {
  OffsetGenerator a(99, 2), b(99, 2);
  for (int i = 0; i < 200; ++i) CHECK_EQ(a.NextTarget(), b.NextTarget());
}

TEST(generator_covers_grid_uniformly) {
  // 25 punti, 25000 estrazioni: circa 1000 ciascuno.
  OffsetGenerator g(11, 2);
  int counts[5][5] = {};
  for (int i = 0; i < 25000; ++i) {
    const Offset t = g.NextTarget();
    counts[t.y + 2][t.x + 2]++;
  }
  for (auto& row : counts)
    for (int c : row) CHECK(c > 850 && c < 1150);
}

TEST(generator_shrinking_amplitude_brings_target_inside) {
  OffsetGenerator g(2, 3);
  while (std::abs(g.Current().x) < 3) g.NextTarget();
  g.SetAmplitude(1);
  CHECK_EQ(g.Amplitude(), 1);
  const Offset t = g.NextTarget();
  CHECK(std::abs(t.x) <= 1 && std::abs(t.y) <= 1);
}

TEST(generator_clamps_amplitude) {
  OffsetGenerator g(2, 9);
  CHECK_EQ(g.Amplitude(), 3);
  g.SetAmplitude(0);
  CHECK_EQ(g.Amplitude(), 1);
}
