// Punto di ingresso di OLED Guard.
#include <windows.h>

#include <string>

#include "app.h"
#include "i18n_files.h"
#include "text.h"
#include "windows_version.h"

namespace {

// Build di Windows letta con RtlGetVersion: GetVersionEx dipende dal manifest.
uint32_t WindowsBuild() {
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  RTL_OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  if (!get || get(&info) != 0) return 0;
  return info.dwBuildNumber;
}

// Testo tradotto, con un testo inglese fisso se i file di lingua non si leggono.
std::wstring Text(const oled::Translations& tr, const char* key, const wchar_t* fallback) {
  const std::string text = tr.Get(key);
  return text == key ? std::wstring(fallback) : oled::Widen(text);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  const uint32_t build = WindowsBuild();
  if (build != 0 && !oled::IsSupportedWindowsBuild(build)) {
    const oled::Translations tr =
        oled::LoadTranslations(oled::UiDirectory(), oled::PickLanguage("auto", oled::PreferredUiLanguages()));
    MessageBoxW(nullptr,
                Text(tr, "startup.windows_too_old.body",
                     L"OLED Guard needs Windows 10 version 2004 or later. Update Windows and try again.")
                    .c_str(),
                L"OLED Guard", MB_OK | MB_ICONERROR);
    return 1;
  }
  oled::App app;
  return app.Run(instance);
}
