#include "test_framework.h"
#include "windows_version.h"

TEST(winver_minimum) {
  CHECK(!oled::IsSupportedWindowsBuild(19040));
  CHECK(oled::IsSupportedWindowsBuild(19041));
  CHECK(oled::IsSupportedWindowsBuild(26300));
  CHECK(!oled::IsSupportedWindowsBuild(0));
}
