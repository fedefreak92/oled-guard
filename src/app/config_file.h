// File di configurazione JSON: lettura, scrittura atomica e sorveglianza delle modifiche.
#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <thread>

#include "config.h"

namespace oled {

class ConfigFile {
 public:
  explicit ConfigFile(std::filesystem::path path) : path_(std::move(path)) {}
  ~ConfigFile() { StopWatching(); }

  const std::filesystem::path& Path() const { return path_; }
  // Se il file manca lo crea con i valori predefiniti.
  ConfigLoadResult Load();
  bool Save(const Config& config);
  // true se il file e' cambiato dall'ultima Load/Save (data di modifica).
  bool ChangedOnDisk() const;

  // Invia `msg` a `notify` quando qualcosa cambia nella cartella del file.
  bool StartWatching(HWND notify, UINT msg);
  void StopWatching();

 private:
  std::optional<std::filesystem::file_time_type> WriteTime() const;

  std::filesystem::path path_;
  std::optional<std::filesystem::file_time_type> known_write_time_;
  std::thread watcher_;
  HANDLE stop_event_ = nullptr;
};

}  // namespace oled
