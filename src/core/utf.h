// Conversioni Unicode minime per la logica pura (nessuna API Windows).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oled {

inline void AppendUtf8(std::string& s, uint32_t cp) {
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Decodifica UTF-16 (senza BOM) in UTF-8; nullopt se i byte non sono UTF-16 valido.
inline std::optional<std::string> Utf16ToUtf8(std::string_view bytes, bool little_endian) {
  if (bytes.size() % 2 != 0) return std::nullopt;
  const auto unit = [&](size_t i) -> uint32_t {
    const auto lo = static_cast<uint8_t>(bytes[little_endian ? i : i + 1]);
    const auto hi = static_cast<uint8_t>(bytes[little_endian ? i + 1 : i]);
    return lo | (static_cast<uint32_t>(hi) << 8);
  };
  std::string out;
  for (size_t i = 0; i < bytes.size(); i += 2) {
    uint32_t cp = unit(i);
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      if (i + 3 >= bytes.size()) return std::nullopt;
      const uint32_t low = unit(i + 2);
      if (low < 0xDC00 || low > 0xDFFF) return std::nullopt;
      cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
      i += 2;
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
      return std::nullopt;
    }
    AppendUtf8(out, cp);
  }
  return out;
}

}  // namespace oled
