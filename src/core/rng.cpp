#include "rng.h"

namespace oled {
namespace {

uint64_t SplitMix64(uint64_t& x) {
  uint64_t z = (x += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

constexpr uint64_t Rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

}  // namespace

Rng::Rng(uint64_t seed) {
  // splitmix64 garantisce uno stato iniziale mai tutto a zero.
  for (auto& s : state_) s = SplitMix64(seed);
}

uint64_t Rng::Next() {
  const uint64_t result = Rotl(state_[1] * 5, 7) * 9;
  const uint64_t t = state_[1] << 17;
  state_[2] ^= state_[0];
  state_[3] ^= state_[1];
  state_[1] ^= state_[2];
  state_[0] ^= state_[3];
  state_[2] ^= t;
  state_[3] = Rotl(state_[3], 45);
  return result;
}

int Rng::UniformInt(int lo, int hi) {
  if (hi <= lo) return lo;
  const uint64_t range = static_cast<uint64_t>(static_cast<int64_t>(hi) - lo) + 1;
  // Campionamento con rifiuto: evita il bias del modulo.
  const uint64_t limit = UINT64_MAX - (UINT64_MAX % range);
  uint64_t value;
  do {
    value = Next();
  } while (value >= limit);
  return static_cast<int>(lo + static_cast<int64_t>(value % range));
}

double Rng::UniformReal(double lo, double hi) {
  // 53 bit alti → [0, 1).
  const double unit = static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0);
  return lo + (hi - lo) * unit;
}

}  // namespace oled
