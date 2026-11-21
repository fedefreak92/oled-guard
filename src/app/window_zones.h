// Raccolta delle finestre visibili per la mappa delle zone dell'oscuramento.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "monitors.h"

namespace oled {

// Zone per cella (0 desktop/barra, 1 app) del monitor, cols x rows celle da 16 px.
// `described` (diagnostica): classe e rettangolo di ogni finestra considerata.
std::vector<uint8_t> CollectDimZones(const MonitorInfo& monitor, int cols, int rows,
                                     std::vector<std::string>* described = nullptr);

}  // namespace oled
