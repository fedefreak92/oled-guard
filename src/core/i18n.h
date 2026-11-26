// Traduzioni: dizionari JSON piatti (chiave -> testo), ripiego sull'inglese, segnaposto
// {nome} e plurali con i suffissi _one/_other. Logica pura: i file li legge l'app.
#pragma once

#include <array>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json.h"

namespace oled {

inline constexpr std::array<std::string_view, 5> kLanguages = {"en", "it", "es", "de", "fr"};

using I18nParams = std::vector<std::pair<std::string, std::string>>;

// "auto": prima lingua di `preferred` (es. "de-AT") supportata per prefisso, altrimenti "en".
// Codice supportato: quello. Altro: "en". Maiuscole ignorate.
std::string PickLanguage(std::string_view setting, std::span<const std::string> preferred);

// fr: forma "one" per n < 2; le altre lingue: solo per n == 1.
bool PluralIsOne(std::string_view lang, double n);

class Translations {
 public:
  Translations() = default;
  // `active` e `english`: oggetti JSON piatti (chiave -> stringa); "_meta" e valori non
  // stringa ignorati.
  static Translations FromJson(std::string lang, const JsonValue& active, const JsonValue& english);

  const std::string& Language() const { return lang_; }
  // Testo della lingua attiva, altrimenti inglese, altrimenti la chiave stessa.
  std::string Get(std::string_view key) const;
  // Come Get, con i segnaposto {nome} sostituiti in un solo passaggio (un valore che
  // contiene {altro} resta com'e'); quelli senza valore restano invariati. Con due
  // valori per lo stesso nome vale il primo.
  std::string Format(std::string_view key, const I18nParams& params) const;
  // key_one o key_other secondo PluralIsOne; {n} vale n (intero se lo e').
  std::string Plural(std::string_view key, double n, I18nParams params = {}) const;
  // Dizionario per la pagina: inglese sovrascritto dalla lingua attiva, senza _meta.
  JsonValue Dictionary() const;
  // Chiavi dell'inglese che mancano nella lingua attiva.
  std::vector<std::string> MissingKeys() const;
  // Chiamato una volta per chiave quando Get non la trova nemmeno in inglese (per il log).
  // Solo dal thread che usa le traduzioni.
  void SetMissingHandler(std::function<void(const std::string&)> handler) { on_missing_ = std::move(handler); }

 private:
  const std::string* Find(std::string_view key) const;

  std::string lang_ = "en";
  std::vector<std::pair<std::string, std::string>> active_;
  std::vector<std::pair<std::string, std::string>> english_;
  std::function<void(const std::string&)> on_missing_;
  mutable std::set<std::string, std::less<>> reported_;
};

// Differenze fra due dizionari (chiavi mancanti o in piu', segnaposto diversi), una
// riga leggibile per differenza. "_meta" escluso. Vuoto = uguali.
std::vector<std::string> CompareDictionaries(const JsonValue& reference, const JsonValue& other);

}  // namespace oled
