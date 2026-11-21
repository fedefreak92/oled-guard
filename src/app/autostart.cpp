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

AutostartState GetAutostartState() {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) {
    return AutostartState::Missing;
  }
  std::wstring value(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, value.data(), &size) !=
      ERROR_SUCCESS) {
    return AutostartState::OtherPath;
  }
  value.resize(wcsnlen(value.c_str(), value.size()));
  const std::wstring expected = L"\"" + ExecutablePath().wstring() + L"\"";
  return _wcsicmp(value.c_str(), expected.c_str()) == 0 ? AutostartState::ThisExe : AutostartState::OtherPath;
}

}  // namespace oled
