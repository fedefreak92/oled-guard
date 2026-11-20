#include "hotkey_parse.h"

#include <string>

#include "ascii.h"

namespace oled {
namespace {

std::optional<unsigned> ModifierFor(const std::string& token) {
  if (token == "ctrl" || token == "control") return kModControl;
  if (token == "alt") return kModAlt;
  if (token == "shift") return kModShift;
  if (token == "win") return kModWin;
  return std::nullopt;
}

std::optional<unsigned> KeyFor(const std::string& token) {
  if (token.size() == 1) {
    const char c = token[0];
    if (c >= 'a' && c <= 'z') return 0x41u + static_cast<unsigned>(c - 'a');
    if (c >= '0' && c <= '9') return 0x30u + static_cast<unsigned>(c - '0');
    return std::nullopt;
  }
  if (token[0] == 'f' && token.size() <= 3) {
    int n = 0;
    for (size_t i = 1; i < token.size(); ++i) {
      if (token[i] < '0' || token[i] > '9') return std::nullopt;
      n = n * 10 + (token[i] - '0');
    }
    if (n >= 1 && n <= 24) return 0x70u + static_cast<unsigned>(n - 1);
    return std::nullopt;
  }
  if (token == "pause") return 0x13u;
  if (token == "insert") return 0x2Du;
  if (token == "delete") return 0x2Eu;
  if (token == "home") return 0x24u;
  if (token == "end") return 0x23u;
  if (token == "pageup") return 0x21u;
  if (token == "pagedown") return 0x22u;
  if (token == "space") return 0x20u;
  return std::nullopt;
}

}  // namespace

std::optional<Hotkey> ParseHotkey(std::string_view text) {
  Hotkey result;
  bool has_key = false;
  size_t start = 0;
  for (;;) {
    const size_t plus = text.find('+', start);
    const std::string token =
        ToLowerAscii(TrimAscii(text.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start)));
    if (token.empty()) return std::nullopt;
    if (auto mod = ModifierFor(token)) {
      if (result.modifiers & *mod) return std::nullopt;  // modificatore ripetuto
      result.modifiers |= *mod;
    } else if (auto vk = KeyFor(token)) {
      if (has_key) return std::nullopt;  // un solo tasto
      result.vk = *vk;
      has_key = true;
    } else {
      return std::nullopt;
    }
    if (plus == std::string_view::npos) break;
    start = plus + 1;
  }
  if (!has_key || result.modifiers == 0) return std::nullopt;
  return result;
}

}  // namespace oled
