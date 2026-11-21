#include <vector>

#include "monitor_select.h"
#include "test_framework.h"

using oled::MonitorDesc;
using oled::SelectMonitor;

namespace {
const std::vector<MonitorDesc> kTwo = {
    {"\\\\.\\DISPLAY1", "DELL U2720Q", true},
    {"\\\\.\\DISPLAY2", "LG TV SSCR2", false},
};
}

TEST(monitor_auto_prefers_lg) {
  auto i = SelectMonitor(kTwo, "auto");
  CHECK(i.has_value() && *i == 1);
}

TEST(monitor_auto_falls_back_to_primary) {
  const std::vector<MonitorDesc> m = {{"\\\\.\\DISPLAY1", "ASUS", false}, {"\\\\.\\DISPLAY2", "DELL", true}};
  auto i = SelectMonitor(m, "auto");
  CHECK(i.has_value() && *i == 1);
}

TEST(monitor_auto_falls_back_to_first) {
  const std::vector<MonitorDesc> m = {{"\\\\.\\DISPLAY1", "ASUS", false}};
  auto i = SelectMonitor(m, "AUTO");
  CHECK(i.has_value() && *i == 0);
}

TEST(monitor_by_name_or_device) {
  auto a = SelectMonitor(kTwo, "lg tv sscr2");
  CHECK(a.has_value() && *a == 1);
  auto b = SelectMonitor(kTwo, "\\\\.\\display1");
  CHECK(b.has_value() && *b == 0);
  CHECK(!SelectMonitor(kTwo, "Samsung").has_value());
}

TEST(monitor_setting_uses_friendly_name_when_unique) {
  CHECK_EQ(oled::MonitorSettingFor(kTwo, 1), std::string("LG TV SSCR2"));
}

TEST(monitor_setting_uses_device_name_for_duplicates) {
  // Due monitor identici: il nome amichevole non basterebbe a scegliere il secondo.
  const std::vector<MonitorDesc> twins = {{"\\\\.\\DISPLAY1", "DELL U2720Q", true},
                                          {"\\\\.\\DISPLAY2", "dell u2720q", false}};
  CHECK_EQ(oled::MonitorSettingFor(twins, 0), std::string("\\\\.\\DISPLAY1"));
  CHECK_EQ(oled::MonitorSettingFor(twins, 1), std::string("\\\\.\\DISPLAY2"));
  const auto back = SelectMonitor(twins, oled::MonitorSettingFor(twins, 1));
  CHECK(back.has_value() && *back == 1);
}

TEST(monitor_none_connected) {
  CHECK(!SelectMonitor({}, "auto").has_value());
}
