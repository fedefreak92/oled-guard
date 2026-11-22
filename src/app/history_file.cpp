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
  in.close();
  const auto parsed = ParseJson(text.str());
  if (!parsed.value || !HistoryJsonUsable(*parsed.value)) {
    // Non si sovrascrive in silenzio (puo' essere di una versione piu' nuova): si mette da
    // parte con un nome nuovo e si riparte da uno storico vuoto.
    std::filesystem::path aside = file;
    aside += L".non-letto";
    for (int i = 2; std::filesystem::exists(aside, ec) && i < 100; ++i) {
      aside = file;
      aside += L".non-letto-" + std::to_wstring(i);
    }
    std::filesystem::rename(file, aside, ec);
    LogWarn("history.json non leggibile o di un'altra versione ({}): messo da parte come {}, storico vuoto",
            parsed.value ? "versione diversa" : parsed.error, aside.filename().string());
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
