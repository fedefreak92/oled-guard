#include "i18n_files.h"

#include <windows.h>

#include <fstream>
#include <sstream>

#include "json.h"
#include "logger.h"
#include "paths.h"
#include "text.h"

namespace oled {
namespace {

JsonValue ReadDictionary(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    LogWarn("Traduzioni: file {} non trovato", Narrow(file.wstring()));
    return JsonValue::MakeObject();
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const JsonParseResult parsed = ParseJson(ss.str());
  if (!parsed.value || parsed.value->type() != JsonValue::Type::Object) {
    LogWarn("Traduzioni: file {} non valido ({})", Narrow(file.wstring()), parsed.error);
    return JsonValue::MakeObject();
  }
  return *parsed.value;
}

}  // namespace

std::vector<std::string> PreferredUiLanguages() {
  ULONG count = 0;
  ULONG size = 0;
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) || size == 0) return {};
  std::wstring buffer(size, L'\0');
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size)) return {};
  // Elenco di stringhe separate da zero, chiuso da un doppio zero.
  std::vector<std::string> out;
  for (const wchar_t* p = buffer.c_str(); *p; p += wcslen(p) + 1) out.push_back(Narrow(p));
  return out;
}

std::filesystem::path UiDirectory() { return ExecutablePath().parent_path() / L"ui"; }

Translations LoadTranslations(const std::filesystem::path& ui_dir, std::string_view lang) {
  const std::filesystem::path dir = ui_dir / L"i18n";
  const JsonValue english = ReadDictionary(dir / L"en.json");
  const JsonValue active = lang == "en" ? english : ReadDictionary(dir / (Widen(std::string(lang)) + L".json"));
  return Translations::FromJson(std::string(lang), active, english);
}

}  // namespace oled
