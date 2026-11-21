// Raccolta delle finestre visibili per la mappa delle zone dell'oscuramento.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "monitors.h"

namespace oled {

// Zone per cella (0 desktop/barra, 1 app) del monitor, cols x rows celle da 16 px.
// `windows`: le finestre delle app in ordine (zona = 1 + indice % 255).
// `described` (diagnostica): classe e rettangolo di ogni finestra considerata.
std::vector<uint8_t> CollectDimZones(const MonitorInfo& monitor, int cols, int rows, std::vector<HWND>* windows,
                                     std::vector<std::string>* described = nullptr);
// Zona della finestra principale che contiene `hwnd` (0 se non e' una finestra di app).
uint8_t ZoneOfWindow(HWND hwnd, const std::vector<HWND>& windows);

}  // namespace oled
