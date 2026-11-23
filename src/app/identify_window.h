// "Identifica": per 3 s su ogni schermo un grande numero con il nome, per capire quale
// schermo e' quale (TV e monitor insieme). Finestre trasparenti ai clic, non attivabili.
#pragma once

#include <windows.h>

#include <vector>

#include "monitors.h"

namespace oled {

// Non blocca: le finestre si chiudono da sole dopo 3000 ms (thread che le crea).
void ShowMonitorIdentifiers(HINSTANCE instance, const std::vector<MonitorInfo>& monitors);

}  // namespace oled
