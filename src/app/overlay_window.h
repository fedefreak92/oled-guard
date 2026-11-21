// Finestra overlay: borderless, topmost, trasparente ai clic, esclusa dalla cattura.
#pragma once

#include <windows.h>

namespace oled {

class OverlayWindow {
 public:
  ~OverlayWindow() { Destroy(); }

  // false se la finestra non puo' essere esclusa dalla cattura (feedback loop).
  bool Create(HINSTANCE instance, const RECT& monitor_rect);
  void Destroy();
  HWND Hwnd() const { return hwnd_; }
  void ReassertTopmost();

 private:
  HWND hwnd_ = nullptr;
};

}  // namespace oled
