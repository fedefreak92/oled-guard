// Thread di emergenza: hotkey di uscita immediata e watchdog dei thread.
// Funziona anche se il thread UI o quello di render sono bloccati.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "hotkey_parse.h"

namespace oled {

class EmergencyGuard {
 public:
  ~EmergencyGuard() { Stop(); }

  bool Start(Hotkey exit_hotkey);
  void Stop();

  void BeatMain();
  void BeatRender();
  // Il watchdog interviene solo mentre l'overlay e' a schermo.
  void SetOverlayActive(bool active);

 private:
  void Run(Hotkey exit_hotkey);

  std::thread thread_;
  HANDLE stop_event_ = nullptr;
  std::atomic<int64_t> main_beat_{0};
  std::atomic<int64_t> render_beat_{0};
  std::atomic<bool> overlay_active_{false};
  std::atomic<bool> hotkey_ok_{false};
};

}  // namespace oled
