#include <cstdlib>

#include "offset.h"
#include "test_framework.h"

using oled::Offset;
using oled::OffsetAnimator;

TEST(animator_endpoints) {
  OffsetAnimator a({0, 0}, {2, -1}, 3000);
  CHECK_EQ(a.At(0), (Offset{0, 0}));
  CHECK_EQ(a.At(3000), (Offset{2, -1}));
  CHECK_EQ(a.At(99999), (Offset{2, -1}));
  CHECK_EQ(a.At(-50), (Offset{0, 0}));
  CHECK_EQ(a.StepCount(), 2);
}

TEST(animator_midpoint) {
  OffsetAnimator a({0, 0}, {2, -1}, 3000);
  CHECK_EQ(a.At(1500), (Offset{1, -1}));
}

TEST(animator_steps_are_single_pixel_and_monotonic) {
  OffsetAnimator a({-3, 2}, {3, -3}, 5000);
  Offset prev = a.At(0);
  int changes = 0;
  for (int t = 1; t <= 5000; ++t) {
    const Offset o = a.At(t);
    CHECK(std::abs(o.x - prev.x) <= 1 && std::abs(o.y - prev.y) <= 1);
    CHECK(std::abs(3 - o.x) <= std::abs(3 - prev.x));
    CHECK(std::abs(-3 - o.y) <= std::abs(-3 - prev.y));
    if (!(o == prev)) ++changes;
    prev = o;
  }
  CHECK_EQ(changes, 6);
}

TEST(animator_next_step_at_is_exact) {
  OffsetAnimator a({0, 0}, {3, 0}, 3000);
  double t = 0;
  int steps = 0;
  while (auto next = a.NextStepAt(t)) {
    CHECK(*next > t);
    CHECK_EQ(a.At(*next - 1), a.At(t));
    CHECK(!(a.At(*next) == a.At(t)));
    t = *next;
    ++steps;
    if (steps > 10) break;
  }
  CHECK_EQ(steps, 3);
  CHECK_EQ(a.At(t), (Offset{3, 0}));
}

TEST(animator_zero_duration_jumps_to_end) {
  OffsetAnimator a({0, 0}, {1, 1}, 0);
  CHECK_EQ(a.At(0), (Offset{1, 1}));
  CHECK(!a.NextStepAt(0));
}

TEST(animator_same_point_has_no_steps) {
  OffsetAnimator a({1, 1}, {1, 1}, 3000);
  CHECK_EQ(a.StepCount(), 0);
  CHECK_EQ(a.At(1500), (Offset{1, 1}));
  CHECK(!a.NextStepAt(0));
}
