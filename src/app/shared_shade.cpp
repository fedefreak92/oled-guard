#include "shared_shade.h"

namespace oled {

void SharedShade::Set(const ShadeInputs& in) {
  std::lock_guard lock(mutex_);
  if (in.base == inputs_.base && in.away_since_ms == inputs_.away_since_ms && in.away_target == inputs_.away_target) {
    return;
  }
  const uint64_t version = inputs_.version + 1;
  inputs_ = in;
  inputs_.version = version;
}

ShadeInputs SharedShade::Get() {
  std::lock_guard lock(mutex_);
  return inputs_;
}

}  // namespace oled
