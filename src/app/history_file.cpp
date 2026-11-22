#include "history_file.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <system_error>

#include "json.h"
#include "logger.h"

namespace oled {

History LoadHistory(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return History{};
  std::ifstream in(file, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  const auto parsed = ParseJson(text.str());
  if (!parsed.value || parsed.value->type() != JsonValue::Type::Object) {
    LogWarn("history.json non leggibile ({}): storico vuoto", parsed.error);
    return History{};
  }
  return History::FromJson(*parsed.value);
}

bool SaveHistory(const std::filesystem::path& file, const History& history) {
  const std::string text = SerializeJson(history.ToJson(), 0);
  // Scrittura atomica: file temporaneo e poi sostituzione, come stats.json.
  const std::filesystem::path temp = file.wstring() + L".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    if (!out) return false;
  }
  return MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

}  // namespace oled
