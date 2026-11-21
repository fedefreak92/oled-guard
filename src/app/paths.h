// Percorsi dell'app in %APPDATA%\oled-guard.
#pragma once

#include <filesystem>

namespace oled {

// Crea la cartella se manca.
std::filesystem::path AppDataDir();
std::filesystem::path LogDir();
// %LOCALAPPDATA%\oled-guard (dati locali alla macchina, es. cache di WebView2).
std::filesystem::path LocalAppDataDir();
std::filesystem::path ConfigPath();
std::filesystem::path ExecutablePath();

}  // namespace oled
