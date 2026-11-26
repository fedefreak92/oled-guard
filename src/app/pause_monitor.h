// Raccolta dei segnali di sistema per le regole di pausa (thread UI).
#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "monitors.h"
#include "pause_rules.h"

namespace oled {

class PauseMonitor {
 public:
  PauseMonitor();

  // Da chiamare su WM_WTSSESSION_CHANGE.
  void OnSessionChange(WPARAM wts_event);
  // monitor nullptr: monitor non collegato. Le finestre di questo processo sono ignorate.
  PauseInputs Collect(const MonitorInfo* monitor) const;

 private:
  bool locked_ = false;
  bool inactive_ = false;
};

// Nome .exe (minuscolo, UTF-8) del processo di una finestra. Per le app UWP, che risultano
// tutte ApplicationFrameHost.exe, quello della finestra figlia Windows.UI.Core.CoreWindow;
// in quel caso `host` (se non nullo) riceve il nome del processo ospite, altrimenti vuoto.
std::string WindowProcessName(HWND hwnd, std::string* host = nullptr);

// Prima finestra visibile, non ridotta a icona, non "cloaked", non di questo processo, che
// copre almeno 64x64 px dello schermo e si esclude dalla cattura (GetWindowDisplayAffinity
// diverso da WDA_NONE). Restituisce il nome del processo, nullopt se non ce ne sono.
std::optional<std::string> FindProtectedWindow(const RECT& monitor);

// Nomi .exe (minuscoli, unici, ordinati) dei processi con una finestra visibile e con
// titolo, escluso questo processo. Per scegliere un'app da mettere in regola.
std::vector<std::string> ListWindowedProcesses();

}  // namespace oled
