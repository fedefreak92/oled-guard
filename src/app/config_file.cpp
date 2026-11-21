#include "config_file.h"

#include <fstream>
#include <sstream>
#include <system_error>

#include "logger.h"

namespace oled {

std::optional<std::filesystem::file_time_type> ConfigFile::WriteTime() const {
  std::error_code ec;
  const auto t = std::filesystem::last_write_time(path_, ec);
  if (ec) return std::nullopt;
  return t;
}

ConfigLoadResult ConfigFile::Load() {
  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) {
    ConfigLoadResult result;
    if (Save(result.config)) LogInfo("Creato il file di configurazione con i valori predefiniti");
    return result;
  }
  // La data va letta prima del contenuto: una scrittura che arriva in mezzo verra'
  // vista come modifica e ricaricata.
  const auto write_time = WriteTime();
  const auto text = ReadText();
  if (!text) {
    ConfigLoadResult result;
    result.parse_failed = true;
    result.warnings.push_back("impossibile leggere il file di configurazione");
    return result;
  }
  known_write_time_ = write_time;
  return ConfigFromJson(*text);
}

std::optional<std::string> ConfigFile::ReadText() const {
  std::ifstream in(path_, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool ConfigFile::OnDiskIsValid() const {
  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) return true;
  const auto text = ReadText();
  return text && !ConfigFromJson(*text).parse_failed;
}

bool ConfigFile::Save(const Config& config) {
  // Scrittura su file temporaneo e sostituzione: mai un file a meta'.
  const std::filesystem::path temp = path_.wstring() + L".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << ConfigToJson(config);
    if (!out) return false;
  }
  if (!MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    LogWarn("Salvataggio della configurazione fallito: {}", GetLastError());
    return false;
  }
  known_write_time_ = WriteTime();
  return true;
}

bool ConfigFile::ChangedOnDisk() const { return WriteTime() != known_write_time_; }

bool ConfigFile::StartWatching(HWND notify, UINT msg) {
  StopWatching();
  HANDLE change = FindFirstChangeNotificationW(path_.parent_path().c_str(), FALSE,
                                               FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
  if (change == INVALID_HANDLE_VALUE) return false;
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  watcher_ = std::thread([change, stop = stop_event_, notify, msg] {
    SetThreadDescription(GetCurrentThread(), L"OledGuard config watcher");
    const HANDLE handles[] = {stop, change};
    while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
      PostMessageW(notify, msg, 0, 0);
      if (!FindNextChangeNotification(change)) break;
    }
    FindCloseChangeNotification(change);
  });
  return true;
}

void ConfigFile::StopWatching() {
  if (watcher_.joinable()) {
    SetEvent(stop_event_);
    watcher_.join();
  }
  if (stop_event_) CloseHandle(stop_event_);
  stop_event_ = nullptr;
}

}  // namespace oled
