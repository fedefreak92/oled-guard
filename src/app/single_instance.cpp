#include "single_instance.h"

namespace oled {

bool SingleInstance::Acquire() {
  mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\OledGuard.SingleInstance");
  return mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

}  // namespace oled
