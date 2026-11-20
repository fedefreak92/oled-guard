// Parsing di combinazioni tipo "Ctrl+Alt+Shift+P" in modificatori + virtual key.
#pragma once

#include <optional>
#include <string_view>

namespace oled {

// Stessi valori di MOD_ALT, MOD_CONTROL, MOD_SHIFT, MOD_WIN di Win32.
inline constexpr unsigned kModAlt = 0x1;
inline constexpr unsigned kModControl = 0x2;
inline constexpr unsigned kModShift = 0x4;
inline constexpr unsigned kModWin = 0x8;

struct Hotkey {
  unsigned modifiers = 0;
  unsigned vk = 0;
  bool operator==(const Hotkey&) const = default;
};

std::optional<Hotkey> ParseHotkey(std::string_view text);

}  // namespace oled
