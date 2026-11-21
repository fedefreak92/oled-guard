#include "shared_dim.h"

namespace oled {

void SharedDim::Set(const DimInputs& in) {
  std::lock_guard lock(mutex_);
  const bool same = in.params == inputs_.params && in.suspended == inputs_.suspended &&
                    in.sdr_white_scrgb == inputs_.sdr_white_scrgb &&
                    in.highlight_until_ms == inputs_.highlight_until_ms && in.watch == inputs_.watch;
  if (same) return;
  const uint64_t version = inputs_.version + 1;
  inputs_ = in;
  inputs_.version = version;
}

DimInputs SharedDim::Get() {
  std::lock_guard lock(mutex_);
  return inputs_;
}

void SharedDim::PublishMap(DimMap map) {
  std::lock_guard lock(mutex_);
  map_ = std::move(map);
}

std::optional<DimMap> SharedDim::TakeMap() {
  std::lock_guard lock(mutex_);
  std::optional<DimMap> out = std::move(map_);
  map_.reset();
  return out;
}

}  // namespace oled
