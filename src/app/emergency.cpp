#include "emergency.h"

#include "clock.h"
#include "logger.h"
#include "watchdog.h"

namespace oled {
namespace {

constexpr int kHotkeyId = 0xE5C;
constexpr int64_t kStallLimitMs = 3000;
constexpr DWORD kCheckIntervalMs = 500;

}  // namespace

bool EmergencyGuard::Start(Hotkey exit_hotkey) {
  Stop();
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stop_event_) return false;
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  thread_ = std::thread([this, exit_hotkey, ready] {
    // La coda dei messaggi del thread deve esistere prima di RegisterHotKey.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    hotkey_ok_ = RegisterHotKey(nullptr, kHotkeyId, exit_hotkey.modifiers | MOD_NOREPEAT, exit_hotkey.vk) != FALSE;
    SetEvent(ready);
    Run(exit_hotkey);
  });
  WaitForSingleObject(ready, 2000);
  CloseHandle(ready);
  if (!hotkey_ok_) LogWarn("Hotkey di uscita di emergenza non registrata (gia' in uso?)");
  return hotkey_ok_;
}

void EmergencyGuard::Stop() {
  if (thread_.joinable()) {
    SetEvent(stop_event_);
    thread_.join();
  }
  if (stop_event_) {
    CloseHandle(stop_event_);
    stop_event_ = nullptr;
  }
}

void EmergencyGuard::BeatMain() { main_beat_ = NowMsInt(); }
void EmergencyGuard::BeatRender() { render_beat_ = NowMsInt(); }

void EmergencyGuard::SetOverlayActive(bool active) {
  // Battiti azzerati: la creazione dell'overlay non conta come blocco.
  const int64_t now = NowMsInt();
  main_beat_ = now;
  render_beat_ = now;
  overlay_active_ = active;
}

void EmergencyGuard::Run(Hotkey) {
  int64_t last_check = NowMsInt();
  for (;;) {
    const DWORD wait = MsgWaitForMultipleObjects(1, &stop_event_, FALSE, kCheckIntervalMs, QS_ALLINPUT);
    if (wait == WAIT_OBJECT_0) break;

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId) {
        LogError("Uscita di emergenza richiesta dall'utente");
        TerminateProcess(GetCurrentProcess(), 3);
      }
    }

    const int64_t now = NowMsInt();
    const int64_t main_beat = main_beat_.load();
    const int64_t render_beat = render_beat_.load();
    switch (CheckWatchdog(now, last_check, main_beat, render_beat, overlay_active_, kStallLimitMs, kCheckIntervalMs)) {
      case WatchdogVerdict::ClockJump:
        // Ripresa da sospensione: si riparte da battiti freschi.
        LogInfo("Watchdog: salto dell'orologio di {} ms (sospensione?), battiti azzerati", now - last_check);
        main_beat_ = now;
        render_beat_ = now;
        break;
      case WatchdogVerdict::Stalled:
        LogError("Watchdog: thread bloccato (UI {} ms, render {} ms), chiusura forzata", now - main_beat,
                 now - render_beat);
        TerminateProcess(GetCurrentProcess(), 4);
        break;
      case WatchdogVerdict::Ok:
        break;
    }
    last_check = now;
  }
  if (hotkey_ok_) UnregisterHotKey(nullptr, kHotkeyId);
}

}  // namespace oled
