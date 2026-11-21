// Raccolta dei segnali di sistema per le regole di pausa (thread UI).
#pragma once

#include <windows.h>

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

// Nomi .exe (minuscoli, unici, ordinati) dei processi con una finestra visibile e con
// titolo, escluso questo processo. Per scegliere un'app da mettere in regola.
std::vector<std::string> ListWindowedProcesses();

}  // namespace oled
