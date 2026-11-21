// Impostazioni di Windows utili a un pannello OLED: lettura, applicazione del valore
// consigliato e ripristino del valore originale. Nessuna richiede diritti di amministratore.
// Le modifiche avvengono solo su richiesta esplicita dell'utente.
#pragma once

#include <windows.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "json.h"
#include "ui_protocol.h"

namespace oled {

class WindowsSettings {
 public:
  explicit WindowsSettings(std::filesystem::path backup_file) : backup_file_(std::move(backup_file)) {}

  // Solo letture: veloce, si puo' chiamare dal thread UI.
  std::vector<WindowsItem> ReadAll() const;
  // Scritture: possono attendere le notifiche di Windows, vanno chiamate da un thread di lavoro.
  bool Apply(const std::string& id);
  void ApplyAll();
  void RestoreAll();

 private:
  JsonValue LoadBackup() const;
  void SaveBackup(const JsonValue& backup) const;

  std::filesystem::path backup_file_;
  mutable std::mutex mutex_;
};

// Spegne il monitor (la TV va in standby e si riaccende al movimento del mouse).
void TurnDisplayOff(HWND any_own_window);

}  // namespace oled
