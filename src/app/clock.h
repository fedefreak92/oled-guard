// Orologio monotono in millisecondi, condiviso da tutti i thread.
#pragma once

#include <chrono>
#include <cstdint>

namespace oled {

inline double NowMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

inline int64_t NowMsInt() { return static_cast<int64_t>(NowMs()); }

}  // namespace oled
