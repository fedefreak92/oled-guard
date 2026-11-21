// Applicazione: thread UI, finestra nascosta per i messaggi, ciclo di vita.
#pragma once

#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "emergency.h"
#include "monitors.h"
#include "overlay_window.h"
#include "render_thread.h"
#include "tray.h"

namespace oled {

class App {
 public:
  int Run(HINSTANCE instance);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
  bool CreateMainWindow();
  bool HasArg(std::wstring_view arg) const;

  bool StartOverlay();
  void StopOverlay();
  void OnRenderEvent(RenderEvent event);
  void OnTrayEvent(UINT event);
  void ShowTrayMenu();
  void Quit();

  std::vector<std::wstring> args_;
  HINSTANCE instance_ = nullptr;
  HWND main_ = nullptr;
  UINT taskbar_created_msg_ = 0;
  HICON icon_ = nullptr;
  Config config_;
  EmergencyGuard guard_;
  Tray tray_;
  OverlayWindow overlay_;
  RenderThread render_;
  // Stato dello shift: appartiene all'App e sopravvive alle pause dell'overlay.
  std::unique_ptr<SharedShift> shift_;
  std::optional<MonitorInfo> monitor_;
  bool overlay_running_ = false;
};

}  // namespace oled
