#include "rng.h"
#include "test_framework.h"

TEST(rng_same_seed_same_sequence) {
  oled::Rng a(42), b(42);
  for (int i = 0; i < 100; ++i) CHECK_EQ(a.Next(), b.Next());
}

TEST(rng_different_seeds_differ) {
  oled::Rng a(1), b(2);
  int same = 0;
  for (int i = 0; i < 100; ++i) same += a.Next() == b.Next() ? 1 : 0;
  CHECK(same < 5);
}

TEST(rng_uniform_int_in_range_and_covers_all) {
  oled::Rng r(7);
  int seen[7] = {};
  for (int i = 0; i < 7000; ++i) {
    const int v = r.UniformInt(-3, 3);
    CHECK(v >= -3 && v <= 3);
    if (v >= -3 && v <= 3) seen[v + 3]++;
  }
  for (int c : seen) CHECK(c > 800 && c < 1200);
}

TEST(rng_uniform_int_single_value) {
  oled::Rng r(3);
  for (int i = 0; i < 10; ++i) CHECK_EQ(r.UniformInt(5, 5), 5);
}

TEST(rng_uniform_real_in_range) {
  oled::Rng r(1);
  for (int i = 0; i < 1000; ++i) {
    const double v = r.UniformReal(2.0, 5.0);
    CHECK(v >= 2.0 && v < 5.0);
  }
}
