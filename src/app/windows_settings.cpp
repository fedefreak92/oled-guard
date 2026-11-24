#include "windows_settings.h"

#include <shellapi.h>
#include <powrprof.h>

#include <fstream>
#include <sstream>

#include "ascii.h"
#include "logger.h"
#include "text.h"

namespace oled {
namespace {

constexpr wchar_t kPersonalize[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kAdvanced[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr wchar_t kDwm[] = L"Software\\Microsoft\\Windows\\DWM";
constexpr wchar_t kDesktop[] = L"Control Panel\\Desktop";
constexpr wchar_t kColors[] = L"Control Panel\\Colors";
constexpr DWORD kDisplayTimeoutS = 300;     // 5 minuti
constexpr UINT kScreensaverTimeoutS = 600;  // 10 minuti

// --- registro ------------------------------------------------------------

DWORD ReadDword(const wchar_t* key, const wchar_t* name, DWORD fallback) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS
             ? value
             : fallback;
}

bool WriteDword(const wchar_t* key, const wchar_t* name, DWORD value) {
  return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_DWORD, &value, sizeof(value)) == ERROR_SUCCESS;
}

std::wstring ReadString(const wchar_t* key, const wchar_t* name) {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) return {};
  std::wstring value(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) {
    return {};
  }
  value.resize(wcsnlen(value.c_str(), value.size()));
  return value;
}

bool WriteString(const wchar_t* key, const wchar_t* name, const std::wstring& value) {
  return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_SZ, value.c_str(),
                         static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// Avvisa le app che tema/colori sono cambiati, senza attendere nessuno.
void NotifyColorChange() {
  SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
}

// --- barra delle applicazioni ------------------------------------------------

UINT TaskbarState() {
  APPBARDATA abd{sizeof(abd)};
  return static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &abd));
}

bool SetTaskbarState(UINT state) {
  APPBARDATA abd{sizeof(abd)};
  abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
  abd.lParam = state;
  SHAppBarMessage(ABM_SETSTATE, &abd);
  return true;
}

// --- spegnimento dello schermo (piano energetico attivo, alimentazione da rete) ----

bool ReadDisplayTimeout(DWORD& seconds) {
  GUID* scheme = nullptr;
  if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS) return false;
  const bool ok = PowerReadACValueIndex(nullptr, scheme, &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT,
                                        &seconds) == ERROR_SUCCESS;
  LocalFree(scheme);
  return ok;
}

bool WriteDisplayTimeout(DWORD seconds) {
  GUID* scheme = nullptr;
  if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS) return false;
  bool ok = PowerWriteACValueIndex(nullptr, scheme, &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, seconds) ==
            ERROR_SUCCESS;
  if (ok) ok = PowerSetActiveScheme(nullptr, scheme) == ERROR_SUCCESS;  // rende effettivo il nuovo valore
  LocalFree(scheme);
  return ok;
}

// Durata come chiave di traduzione: mai, secondi o minuti.
void SetDuration(std::string& key, I18nParams& params, DWORD seconds) {
  params.clear();
  if (seconds == 0) {
    key = "windows.value.never";
  } else if (seconds < 60) {
    key = "windows.value.seconds";
    params.emplace_back("n", std::to_string(seconds));
  } else {
    key = "windows.value.minutes";
    params.emplace_back("n", std::to_string(seconds / 60));
  }
}

bool KnownId(const std::string& id);

// --- icone del desktop ---------------------------------------------------------

// Vista delle icone del desktop (SHELLDLL_DefView), figlia di Progman o di un WorkerW.
HWND DesktopView() {
  HWND view = FindWindowExW(FindWindowW(L"Progman", nullptr), nullptr, L"SHELLDLL_DefView", nullptr);
  HWND worker = nullptr;
  while (!view && (worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr) {
    view = FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr);
  }
  return view;
}

bool DesktopIconsVisible() {
  HWND list = FindWindowExW(DesktopView(), nullptr, L"SysListView32", nullptr);
  return list && IsWindowVisible(list);
}

// Explorer non rilegge HideIcons da solo: si usa il comando "mostra icone" della vista
// (non documentato, alterna lo stato), solo se lo stato reale e' diverso da quello voluto.
void SetDesktopIcons(bool visible) {
  WriteDword(kAdvanced, L"HideIcons", visible ? 0 : 1);
  HWND view = DesktopView();
  if (view && DesktopIconsVisible() != visible) PostMessageW(view, WM_COMMAND, 0x7402, 0);
}

// --- sfondo -------------------------------------------------------------------

std::wstring WallpaperPath() {
  wchar_t path[MAX_PATH] = {};
  SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0);
  return path;
}

void SetWallpaper(const std::wstring& path, COLORREF color, const std::wstring& color_reg) {
  const INT element = COLOR_DESKTOP;
  SetSysColors(1, &element, &color);
  WriteString(kColors, L"Background", color_reg);
  SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(path.c_str()),
                        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

// --- salvaschermo -------------------------------------------------------------

std::wstring BlankScreensaver() {
  wchar_t path[MAX_PATH] = {};
  ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\scrnsave.scr", path, MAX_PATH);
  return path;
}

void SetScreensaver(bool active, UINT timeout_s, const std::wstring& exe) {
  WriteString(kDesktop, L"SCRNSAVE.EXE", exe);
  SystemParametersInfoW(SPI_SETSCREENSAVETIMEOUT, timeout_s, nullptr, SPIF_UPDATEINIFILE);
  SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, active ? TRUE : FALSE, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

// --- lettura di una voce ------------------------------------------------------

WindowsItem ReadItem(const std::string& id) {
  WindowsItem it;
  it.id = id;
  if (id == "dark_theme") {
    const DWORD apps = ReadDword(kPersonalize, L"AppsUseLightTheme", 1);
    const DWORD sys = ReadDword(kPersonalize, L"SystemUsesLightTheme", 1);
    it.current_key = apps == 0 && sys == 0 ? "windows.value.dark"
                                           : (apps != 0 && sys != 0 ? "windows.value.light" : "windows.value.mixed");
    it.recommended_key = "windows.action.dark";
    it.ok = apps == 0 && sys == 0;
  } else if (id == "taskbar_autohide") {
    const bool autohide = (TaskbarState() & ABS_AUTOHIDE) != 0;
    it.current_key = autohide ? "windows.value.autohide" : "windows.value.always_visible";
    it.recommended_key = "windows.action.apply";
    it.ok = autohide;
  } else if (id == "display_timeout") {
    DWORD seconds = 0;
    SetDuration(it.recommended_key, it.recommended_params, kDisplayTimeoutS);
    if (ReadDisplayTimeout(seconds)) {
      SetDuration(it.current_key, it.current_params, seconds);
      it.ok = seconds > 0 && seconds <= kDisplayTimeoutS;
    } else {
      it.current_key = "windows.value.unreadable";
      it.managed = true;
    }
  } else if (id == "screensaver") {
    BOOL active = FALSE;
    UINT timeout = 0;
    SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE, 0, &active, 0);
    SystemParametersInfoW(SPI_GETSCREENSAVETIMEOUT, 0, &timeout, 0);
    const std::string exe = ToLowerAscii(Narrow(ReadString(kDesktop, L"SCRNSAVE.EXE")));
    const bool blank = exe.size() >= 12 && exe.substr(exe.size() - 12) == "scrnsave.scr";
    if (!active || exe.empty()) {
      it.current_key = "windows.value.none";
    } else if (blank) {
      it.current_key = "windows.value.black_after_min";
      it.current_params = {{"n", std::to_string(timeout / 60)}};
    } else {
      it.current_key = "windows.value.other_screensaver";
    }
    SetDuration(it.recommended_key, it.recommended_params, kScreensaverTimeoutS);
    it.ok = active && blank && timeout > 0 && timeout <= kScreensaverTimeoutS;
  } else if (id == "desktop_icons") {
    const bool visible = DesktopIconsVisible();
    it.current_key = visible ? "windows.value.visible" : "windows.value.hidden";
    it.recommended_key = "windows.action.hide";
    it.ok = !visible;
  } else if (id == "transparency") {
    const bool on = ReadDword(kPersonalize, L"EnableTransparency", 1) != 0;
    it.current_key = on ? "windows.value.on" : "windows.value.off";
    it.recommended_key = "windows.action.turn_off";
    it.ok = !on;
  } else if (id == "black_wallpaper") {
    const bool no_image = WallpaperPath().empty();
    const bool black = GetSysColor(COLOR_DESKTOP) == RGB(0, 0, 0);
    it.current_key = no_image ? (black ? "windows.value.solid_black" : "windows.value.solid") : "windows.value.image";
    it.recommended_key = "windows.action.black";
    it.ok = no_image && black;
  } else if (id == "mouse_vanish") {
    BOOL vanish = FALSE;
    SystemParametersInfoW(SPI_GETMOUSEVANISH, 0, &vanish, 0);
    it.current_key = vanish ? "windows.value.on" : "windows.value.off";
    it.recommended_key = "windows.action.turn_on";
    it.ok = vanish != FALSE;
  } else if (id == "accent_titlebar") {
    const bool on = ReadDword(kDwm, L"ColorPrevalence", 0) != 0;
    it.current_key = on ? "windows.value.on" : "windows.value.off";
    it.recommended_key = "windows.action.turn_off";
    it.ok = !on;
  }
  return it;
}

const char* const kIds[] = {"dark_theme",   "taskbar_autohide", "display_timeout", "screensaver",     "desktop_icons",
                            "transparency", "black_wallpaper",  "mouse_vanish",    "accent_titlebar"};

bool KnownId(const std::string& id) {
  for (const char* known : kIds)
    if (id == known) return true;
  return false;
}

// --- valore attuale (per il backup) e scrittura --------------------------------

JsonValue CaptureOriginal(const std::string& id) {
  JsonValue v = JsonValue::MakeObject();
  if (id == "dark_theme") {
    v.Set("apps", JsonValue(static_cast<double>(ReadDword(kPersonalize, L"AppsUseLightTheme", 1))));
    v.Set("system", JsonValue(static_cast<double>(ReadDword(kPersonalize, L"SystemUsesLightTheme", 1))));
  } else if (id == "taskbar_autohide") {
    v.Set("state", JsonValue(static_cast<double>(TaskbarState())));
  } else if (id == "display_timeout") {
    DWORD seconds = 0;
    ReadDisplayTimeout(seconds);
    v.Set("seconds", JsonValue(static_cast<double>(seconds)));
  } else if (id == "screensaver") {
    BOOL active = FALSE;
    UINT timeout = 0;
    SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE, 0, &active, 0);
    SystemParametersInfoW(SPI_GETSCREENSAVETIMEOUT, 0, &timeout, 0);
    v.Set("active", JsonValue(active != FALSE));
    v.Set("timeout", JsonValue(static_cast<double>(timeout)));
    v.Set("exe", JsonValue(Narrow(ReadString(kDesktop, L"SCRNSAVE.EXE"))));
  } else if (id == "desktop_icons") {
    v.Set("visible", JsonValue(DesktopIconsVisible()));
  } else if (id == "transparency") {
    v.Set("value", JsonValue(static_cast<double>(ReadDword(kPersonalize, L"EnableTransparency", 1))));
  } else if (id == "black_wallpaper") {
    v.Set("path", JsonValue(Narrow(WallpaperPath())));
    v.Set("color", JsonValue(static_cast<double>(GetSysColor(COLOR_DESKTOP))));
    v.Set("color_reg", JsonValue(Narrow(ReadString(kColors, L"Background"))));
  } else if (id == "mouse_vanish") {
    BOOL vanish = FALSE;
    SystemParametersInfoW(SPI_GETMOUSEVANISH, 0, &vanish, 0);
    v.Set("value", JsonValue(vanish != FALSE));
  } else if (id == "accent_titlebar") {
    v.Set("value", JsonValue(static_cast<double>(ReadDword(kDwm, L"ColorPrevalence", 0))));
  }
  return v;
}

double Num(const JsonValue& v, const char* key, double fallback = 0) {
  const JsonValue* f = v.Find(key);
  return f && f->type() == JsonValue::Type::Number ? f->AsNumber() : fallback;
}
bool Bool(const JsonValue& v, const char* key) {
  const JsonValue* f = v.Find(key);
  return f && f->type() == JsonValue::Type::Bool && f->AsBool();
}
std::wstring Str(const JsonValue& v, const char* key) {
  const JsonValue* f = v.Find(key);
  return f && f->type() == JsonValue::Type::String ? Widen(f->AsString()) : std::wstring();
}

bool ApplyRecommended(const std::string& id) {
  if (id == "dark_theme") {
    const bool ok = WriteDword(kPersonalize, L"AppsUseLightTheme", 0) && WriteDword(kPersonalize, L"SystemUsesLightTheme", 0);
    NotifyColorChange();
    return ok;
  }
  if (id == "taskbar_autohide") return SetTaskbarState(ABS_AUTOHIDE | (TaskbarState() & ABS_ALWAYSONTOP));
  if (id == "display_timeout") return WriteDisplayTimeout(kDisplayTimeoutS);
  if (id == "screensaver") {
    SetScreensaver(true, kScreensaverTimeoutS, BlankScreensaver());
    return true;
  }
  if (id == "desktop_icons") {
    SetDesktopIcons(false);
    return true;
  }
  if (id == "transparency") {
    const bool ok = WriteDword(kPersonalize, L"EnableTransparency", 0);
    NotifyColorChange();
    return ok;
  }
  if (id == "black_wallpaper") {
    SetWallpaper(L"", RGB(0, 0, 0), L"0 0 0");
    return true;
  }
  if (id == "mouse_vanish") {
    return SystemParametersInfoW(SPI_SETMOUSEVANISH, 0, reinterpret_cast<PVOID>(TRUE),
                                 SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) != FALSE;
  }
  if (id == "accent_titlebar") {
    const bool ok = WriteDword(kDwm, L"ColorPrevalence", 0);
    NotifyColorChange();
    return ok;
  }
  return false;
}

void Restore(const std::string& id, const JsonValue& v) {
  if (id == "dark_theme") {
    WriteDword(kPersonalize, L"AppsUseLightTheme", static_cast<DWORD>(Num(v, "apps", 1)));
    WriteDword(kPersonalize, L"SystemUsesLightTheme", static_cast<DWORD>(Num(v, "system", 1)));
    NotifyColorChange();
  } else if (id == "taskbar_autohide") {
    SetTaskbarState(static_cast<UINT>(Num(v, "state", ABS_ALWAYSONTOP)));
  } else if (id == "display_timeout") {
    WriteDisplayTimeout(static_cast<DWORD>(Num(v, "seconds")));
  } else if (id == "screensaver") {
    SetScreensaver(Bool(v, "active"), static_cast<UINT>(Num(v, "timeout", 600)), Str(v, "exe"));
  } else if (id == "desktop_icons") {
    SetDesktopIcons(Bool(v, "visible"));
  } else if (id == "transparency") {
    WriteDword(kPersonalize, L"EnableTransparency", static_cast<DWORD>(Num(v, "value", 1)));
    NotifyColorChange();
  } else if (id == "black_wallpaper") {
    SetWallpaper(Str(v, "path"), static_cast<COLORREF>(Num(v, "color")), Str(v, "color_reg"));
  } else if (id == "mouse_vanish") {
    SystemParametersInfoW(SPI_SETMOUSEVANISH, 0, reinterpret_cast<PVOID>(static_cast<INT_PTR>(Bool(v, "value"))),
                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
  } else if (id == "accent_titlebar") {
    WriteDword(kDwm, L"ColorPrevalence", static_cast<DWORD>(Num(v, "value")));
    NotifyColorChange();
  }
}

}  // namespace

JsonValue WindowsSettings::LoadBackup() const {
  std::ifstream in(backup_file_, std::ios::binary);
  if (!in) return JsonValue::MakeObject();
  std::ostringstream text;
  text << in.rdbuf();
  const auto parsed = ParseJson(text.str());
  return parsed.value && parsed.value->type() == JsonValue::Type::Object ? *parsed.value : JsonValue::MakeObject();
}

void WindowsSettings::SaveBackup(const JsonValue& backup) const {
  const std::filesystem::path temp = backup_file_.wstring() + L".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << SerializeJson(backup) << "\n";
  }
  MoveFileExW(temp.c_str(), backup_file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

std::vector<WindowsItem> WindowsSettings::ReadAll() const {
  std::lock_guard lock(mutex_);
  const JsonValue backup = LoadBackup();
  std::vector<WindowsItem> items;
  for (const char* id : kIds) {
    WindowsItem it = ReadItem(id);
    it.restorable = backup.Find(id) != nullptr;
    items.push_back(std::move(it));
  }
  return items;
}

bool WindowsSettings::Apply(const std::string& id) {
  std::lock_guard lock(mutex_);
  if (!KnownId(id)) return false;  // id sconosciuto
  JsonValue backup = LoadBackup();
  // Si salva l'originale solo la prima volta: "Ripristina" torna a com'era prima di OLED Guard.
  if (!backup.Find(id)) {
    backup.Set(id, CaptureOriginal(id));
    SaveBackup(backup);
  }
  const bool ok = ApplyRecommended(id);
  LogInfo("Windows per OLED: {} {}", id, ok ? "applicato" : "non applicato");
  return ok;
}

void WindowsSettings::ApplyAll() {
  for (const char* id : kIds) {
    const WindowsItem it = ReadItem(id);
    if (!it.ok && !it.managed) Apply(id);
  }
}

void WindowsSettings::RestoreAll() {
  std::lock_guard lock(mutex_);
  const JsonValue backup = LoadBackup();
  for (const auto& [id, value] : backup.AsObject()) {
    Restore(id, value);
    LogInfo("Windows per OLED: {} ripristinato", id);
  }
  SaveBackup(JsonValue::MakeObject());
}

}  // namespace oled
