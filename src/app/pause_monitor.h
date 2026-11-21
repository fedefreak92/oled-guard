// Raccolta dei segnali di sistema per le regole di pausa (thread UI).
#pragma once

#include <windows.h>

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

}  // namespace oled
