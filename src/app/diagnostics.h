// Diagnostica da riga di comando: oled-guard.exe --list-monitors
#pragma once

#include <string_view>

namespace oled {

// Scrive nel log i monitor trovati e il formato catturato dal monitor scelto.
int RunListMonitors(std::string_view monitor_setting);

}  // namespace oled
