#include "autostart.h"

#include <windows.h>

#include <string>

#include "logger.h"
#include "paths.h"

namespace oled {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"OledGuard";

}  // namespace

bool SetAutostart(bool enable) {
  LSTATUS status;
  if (enable) {
    const std::wstring command = L"\"" + ExecutablePath().wstring() + L"\"";
    status = RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, command.c_str(),
                             static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
    if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
  }
  if (status != ERROR_SUCCESS) {
    LogWarn("Avvio automatico: modifica del registro fallita ({})", status);
    return false;
  }
  LogInfo("Avvio automatico {}", enable ? "attivato" : "disattivato");
  return true;
}

bool IsAutostartEnabled() {
  DWORD size = 0;
  return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, nullptr, &size) ==
         ERROR_SUCCESS;
}

}  // namespace oled
