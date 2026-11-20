#include "paths.h"

#include <windows.h>
#include <shlobj.h>

#include <system_error>

namespace oled {

std::filesystem::path AppDataDir() {
  std::filesystem::path base;
  PWSTR raw = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &raw))) {
    base = raw;
  } else {
    base = std::filesystem::temp_directory_path();
  }
  CoTaskMemFree(raw);
  std::filesystem::path dir = base / L"oled-guard";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::filesystem::path LogDir() {
  std::filesystem::path dir = AppDataDir() / L"logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::filesystem::path ConfigPath() { return AppDataDir() / L"config.json"; }

std::filesystem::path ExecutablePath() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n < buffer.size()) {
      buffer.resize(n);
      return buffer;
    }
    buffer.resize(buffer.size() * 2);
  }
}

}  // namespace oled
