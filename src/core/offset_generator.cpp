#include <algorithm>
#include <cstdlib>

#include "offset.h"

namespace oled {
namespace {

constexpr int kMinAmplitude = 1;
constexpr int kMaxAmplitude = 3;

}  // namespace

OffsetGenerator::OffsetGenerator(uint64_t seed, int amplitude)
    : rng_(seed), amplitude_(std::clamp(amplitude, kMinAmplitude, kMaxAmplitude)) {}

void OffsetGenerator::SetAmplitude(int amplitude) {
  amplitude_ = std::clamp(amplitude, kMinAmplitude, kMaxAmplitude);
}

Offset OffsetGenerator::NextTarget() {
  const int side = 2 * amplitude_ + 1;
  const int cells = side * side;
  const bool current_inside = std::abs(current_.x) <= amplitude_ && std::abs(current_.y) <= amplitude_;
  const int current_index = (current_.y + amplitude_) * side + (current_.x + amplitude_);

  // Se la cella corrente e' nella griglia la si esclude: si estrae fra le restanti
  // e si salta quella corrente, cosi' la scelta resta uniforme.
  int index = rng_.UniformInt(0, cells - (current_inside ? 2 : 1));
  if (current_inside && index >= current_index) ++index;

  current_ = Offset{index % side - amplitude_, index / side - amplitude_};
  return current_;
}

}  // namespace oled
