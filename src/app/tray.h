// Icona nella area di notifica.
#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string_view>

namespace oled {

// Icona dell'app disegnata in codice (anello con un punto spostato).
HICON CreateAppIcon(int size);

class Tray {
 public:
  ~Tray() { Destroy(); }

  bool Create(HWND owner, UINT callback_msg, HICON icon);
  void Destroy();
  // Da chiamare su "TaskbarCreated" (Explorer riavviato).
  bool Recreate();
  void SetTooltip(std::wstring_view text);
  void ShowBalloon(std::wstring_view title, std::wstring_view text);

 private:
  NOTIFYICONDATAW data_{};
  bool added_ = false;
};

}  // namespace oled
