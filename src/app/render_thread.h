// Thread di render: possiede device, duplicazione e swap chain. Esiste solo
// mentre l'overlay e' attivo; fermandolo si rilascia tutto.
#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <thread>

#include "emergency.h"
#include "monitors.h"
#include "shared_dim.h"
#include "shared_shade.h"
#include "shift_scheduler.h"

namespace oled {

inline constexpr UINT WM_APP_RENDER_EVENT = WM_APP + 1;

enum class RenderEvent : WPARAM {
  FirstFrameShown = 1,  // overlay a schermo (anche dopo un recupero)
  CaptureLost,          // overlay nascosto, si tenta il recupero
  CaptureFailed,        // troppi errori: il thread termina
  DisplayRotated,       // display ruotato: non supportato, il thread termina
};

struct RenderParams {
  HWND overlay = nullptr;
  HWND main = nullptr;
  MonitorInfo monitor;
  bool allow_tearing = false;
  SharedShift* shift = nullptr;  // nullptr: offset sempre (0,0)
  SharedDim* dim = nullptr;      // nullptr: nessun oscuramento
  SharedShade* shade = nullptr;  // nullptr: luminosita' piena, niente bordi
  double frame_period_ms = 7;    // periodo di refresh del monitor
  bool dim_diagnostics = false;  // --dim-diagnostics: statistiche nel log e istantanee
  std::filesystem::path diagnostics_dir;
  EmergencyGuard* guard = nullptr;
  unsigned generation = 0;  // inviata negli eventi (lParam) per scartare quelli vecchi
};

class RenderThread {
 public:
  ~RenderThread() { Stop(); }

  bool Start(const RenderParams& params);
  void Stop();
  bool Running() const { return thread_.joinable(); }

 private:
  enum class SessionEnd { Stop, Failed };

  void Run();
  SessionEnd RunSession();
  void Post(RenderEvent event);
  void Beat();
  // Attesa interrompibile da Stop(); continua a battere il watchdog.
  void SleepMs(DWORD ms);

  RenderParams params_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  HANDLE stop_event_ = nullptr;
  int failures_ = 0;
};

}  // namespace oled
