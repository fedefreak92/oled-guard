#include "hotkey_parse.h"
#include "test_framework.h"

using namespace oled;

TEST(hotkey_parses) {
  auto a = ParseHotkey("Ctrl+Alt+Shift+F12");
  CHECK(a.has_value());
  if (a) CHECK(*a == (Hotkey{kModControl | kModAlt | kModShift, 0x7B}));
  auto b = ParseHotkey("win + p");
  CHECK(b.has_value());
  if (b) CHECK(*b == (Hotkey{kModWin, 0x50}));
  auto c = ParseHotkey("Control+Shift+5");
  CHECK(c.has_value());
  if (c) CHECK(*c == (Hotkey{kModControl | kModShift, 0x35}));
  auto d = ParseHotkey("Alt+PageDown");
  CHECK(d.has_value());
  if (d) CHECK(*d == (Hotkey{kModAlt, 0x22}));
  auto e = ParseHotkey("Ctrl+F24");
  CHECK(e.has_value());
  if (e) CHECK(*e == (Hotkey{kModControl, 0x87}));
}

TEST(hotkey_rejects) {
  for (const char* bad : {"F12", "Ctrl+Alt", "Ctrl+A+B", "Ctrl+F25", "", "Ctrl+", "Ctrl+Ctrl+A", "Ctrl+Pippo"}) {
    CHECK(!ParseHotkey(bad).has_value());
  }
}
