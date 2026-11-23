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

TEST(monitor_auto_prefers_hdr_hdmi) {
  // Una TV in HDR via HDMI, non principale, vince sul monitor principale via DisplayPort.
  MonitorDesc tv{"\\\\.\\DISPLAY2", "SAMSUNG", false};
  tv.hdr = true;
  tv.connection = "HDMI";
  MonitorDesc desk{"\\\\.\\DISPLAY1", "DELL", true};
  desk.connection = "DP";
  const std::vector<MonitorDesc> m = {desk, tv};
  auto i = SelectMonitor(m, "auto");
  CHECK(i.has_value() && *i == 1);
  // HDR ma via DisplayPort: non basta, vince il principale.
  MonitorDesc hdr_dp = tv;
  hdr_dp.connection = "DP";
  const std::vector<MonitorDesc> m2 = {desk, hdr_dp};
  auto j = SelectMonitor(m2, "auto");
  CHECK(j.has_value() && *j == 0);
}

TEST(monitor_auto_ignores_brand_names) {
  // Nessun trattamento speciale per i nomi: senza HDR via HDMI vince il principale.
  auto i = SelectMonitor(kTwo, "auto");
  CHECK(i.has_value() && *i == 0);
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

// La pagina Monitor manda il device name: si salva il nome amichevole (stabile anche se
// Windows rinumera gli schermi), il device name solo fra due schermi con lo stesso nome.
TEST(monitor_setting_for_device) {
  const std::vector<oled::MonitorDesc> two = {{"DISPLAY1", "DELL U2723QE", true}, {"DISPLAY2", "LG TV SSCR2", false}};
  CHECK_EQ(oled::MonitorSettingForDevice(two, "display2"), std::string("LG TV SSCR2"));
  CHECK_EQ(oled::MonitorSettingForDevice(two, "auto"), std::string("auto"));
  CHECK_EQ(oled::MonitorSettingForDevice(two, "DISPLAY9"), std::string("DISPLAY9"));  // sconosciuto: invariato
  const std::vector<oled::MonitorDesc> twins = {{"DISPLAY1", "LG TV SSCR2", true}, {"DISPLAY2", "LG TV SSCR2", false}};
  CHECK_EQ(oled::MonitorSettingForDevice(twins, "DISPLAY2"), std::string("DISPLAY2"));
}
