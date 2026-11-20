// Percorsi dell'app in %APPDATA%\oled-guard.
#pragma once

#include <filesystem>

namespace oled {

// Crea la cartella se manca.
std::filesystem::path AppDataDir();
std::filesystem::path LogDir();
std::filesystem::path ConfigPath();
std::filesystem::path ExecutablePath();

}  // namespace oled
