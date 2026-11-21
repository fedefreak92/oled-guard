// Persistenza di statistiche, storico delle app e posizione della finestra (stats.json).
#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>

#include "app_history.h"
#include "wear_stats.h"

namespace oled {

struct StatsData {
  WearStats wear;
  AppHistory apps;
  std::optional<RECT> ui_rect;
};

// File mancante o rovinato: dati vuoti (con avviso nel log), mai un errore.
StatsData LoadStats(const std::filesystem::path& file);
// Scrittura atomica (file temporaneo + sostituzione).
bool SaveStats(const std::filesystem::path& file, const StatsData& data);

}  // namespace oled
