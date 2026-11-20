// Punto di ingresso di OLED Guard.
#include <windows.h>

#include "app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  oled::App app;
  return app.Run(instance);
}
