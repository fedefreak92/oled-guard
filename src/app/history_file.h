// Persistenza dello storico delle statistiche (history.json).
#pragma once

#include <filesystem>

#include "history.h"

namespace oled {

// File mancante o rovinato: storico vuoto (con avviso nel log), mai un errore.
History LoadHistory(const std::filesystem::path& file);
// Scrittura atomica (file temporaneo + sostituzione).
bool SaveHistory(const std::filesystem::path& file, const History& history);

}  // namespace oled
