// File di configurazione JSON: lettura, scrittura atomica e sorveglianza delle modifiche.
#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include "config.h"

namespace oled {

class ConfigFile {
 public:
  explicit ConfigFile(std::filesystem::path path) : path_(std::move(path)) {}
  ~ConfigFile() { StopWatching(); }

  const std::filesystem::path& Path() const { return path_; }
  // Se il file manca lo crea con i valori predefiniti.
  // create_if_missing: un file assente viene creato con i valori predefiniti. Al primo
  // avvio no: il file nasce alla fine della procedura (se resta a meta', riparte).
  ConfigLoadResult Load(bool create_if_missing = true);
  bool Save(const Config& config);
  // true se il file e' cambiato dall'ultima Load/Save (data di modifica).
  bool ChangedOnDisk() const;
  // false se il file su disco esiste ma non si riesce a leggere come config (es.
  // l'utente lo sta modificando): in quel caso non va sovrascritto.
  bool OnDiskIsValid() const;

  // Invia `msg` a `notify` quando qualcosa cambia nella cartella del file.
  bool StartWatching(HWND notify, UINT msg);
  void StopWatching();

 private:
  std::optional<std::filesystem::file_time_type> WriteTime() const;
  std::optional<std::string> ReadText() const;

  std::filesystem::path path_;
  std::optional<std::filesystem::file_time_type> known_write_time_;
  std::thread watcher_;
  HANDLE stop_event_ = nullptr;
};

}  // namespace oled
