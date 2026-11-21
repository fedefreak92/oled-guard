// Istanza singola tramite mutex con nome nella sessione.
#pragma once

#include <windows.h>

namespace oled {

class SingleInstance {
 public:
  ~SingleInstance() {
    if (mutex_) CloseHandle(mutex_);
  }
  // false se un'altra istanza e' gia' in esecuzione.
  bool Acquire();

 private:
  HANDLE mutex_ = nullptr;
};

}  // namespace oled
