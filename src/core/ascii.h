// Piccoli helper su stringhe ASCII (nomi .exe, hotkey, nomi monitor).
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace oled {

inline char AsciiLower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

inline std::string ToLowerAscii(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), AsciiLower);
  return out;
}

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return AsciiLower(x) == AsciiLower(y); });
}

inline bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle) {
  return ToLowerAscii(haystack).find(ToLowerAscii(needle)) != std::string::npos;
}

inline std::string_view TrimAscii(std::string_view s) {
  const auto space = [](char c) { return c == ' ' || c == '\t'; };
  while (!s.empty() && space(s.front())) s.remove_prefix(1);
  while (!s.empty() && space(s.back())) s.remove_suffix(1);
  return s;
}

}  // namespace oled
