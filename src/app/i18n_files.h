// Lettura dei file di traduzione (ui\i18n\<lingua>.json accanto all'eseguibile) e delle
// lingue preferite di Windows.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "i18n.h"

namespace oled {

// GetUserPreferredUILanguages, es. {"it-IT", "en-US"}. Vuoto se non disponibile.
std::vector<std::string> PreferredUiLanguages();

// Cartella dell'interfaccia: "ui" accanto all'eseguibile.
std::filesystem::path UiDirectory();

// Dizionario della lingua con l'inglese come ripiego. Un file che non si legge vale come
// dizionario vuoto (con un avviso nel log).
Translations LoadTranslations(const std::filesystem::path& ui_dir, std::string_view lang);

}  // namespace oled
