// Applicazione: thread UI, finestra nascosta per i messaggi, ciclo di vita.
#pragma once

#include <windows.h>

#include "config.h"
#include "emergency.h"

namespace oled {

class App {
 public:
  int Run(HINSTANCE instance);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
  bool CreateMainWindow();

  HINSTANCE instance_ = nullptr;
  HWND main_ = nullptr;
  Config config_;
  EmergencyGuard guard_;
};

}  // namespace oled
