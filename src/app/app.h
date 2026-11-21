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
#include "pause_monitor.h"
#include "pause_rules.h"
#include "render_thread.h"
#include "tray.h"

namespace oled {

class App {
 public:
  int Run(HINSTANCE instance);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static void CALLBACK ForegroundHook(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
  bool CreateMainWindow();
  bool HasArg(std::wstring_view arg) const;

  // Valuta i segnali di pausa e crea/distrugge l'overlay di conseguenza.
  void Tick();
  bool StartOverlay();
  void StopOverlay();
  void RefreshMonitor();
  void OnRenderEvent(RenderEvent event);
  void OnTrayEvent(UINT event);
  void ShowTrayMenu();
  void UpdateTrayTooltip();
  void SetManualPause(bool paused);
  void Quit();

  std::vector<std::wstring> args_;
  HINSTANCE instance_ = nullptr;
  HWND main_ = nullptr;
  UINT taskbar_created_msg_ = 0;
  HICON icon_ = nullptr;
  HWINEVENTHOOK foreground_hook_ = nullptr;
  Config config_;
  EmergencyGuard guard_;
  Tray tray_;
  OverlayWindow overlay_;
  RenderThread render_;
  // Stato dello shift: appartiene all'App e sopravvive alle pause dell'overlay.
  std::unique_ptr<SharedShift> shift_;

  PauseMonitor pause_monitor_;
  PauseHysteresis hysteresis_{300, 2500};
  bool manual_pause_ = false;
  double capture_failed_until_ms_ = 0;
  bool display_rotated_ = false;
  bool last_paused_ = false;
  PauseReason last_reason_ = PauseReason::None;
  PauseReason last_raw_ = PauseReason::None;
  NotificationState last_notification_ = NotificationState::AcceptsNotifications;
  bool first_tick_ = true;

  std::optional<MonitorInfo> monitor_;
  bool monitor_valid_ = false;
  bool overlay_running_ = false;
};

}  // namespace oled
