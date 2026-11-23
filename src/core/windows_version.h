// Versione minima di Windows: Windows 10 2004 (build 19041). Sotto mancano
// WDA_EXCLUDEFROMCAPTURE e altre funzioni usate dall'overlay.
#pragma once

#include <cstdint>

namespace oled {

inline constexpr uint32_t kMinWindowsBuild = 19041;

constexpr bool IsSupportedWindowsBuild(uint32_t build) { return build >= kMinWindowsBuild; }

}  // namespace oled
