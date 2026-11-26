#include "i18n.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "ascii.h"

namespace oled {
namespace {

using Entries = std::vector<std::pair<std::string, std::string>>;

Entries ToEntries(const JsonValue& dict) {
  Entries out;
  if (dict.type() != JsonValue::Type::Object) return out;
  for (const auto& [key, value] : dict.AsObject()) {
    if (key == "_meta" || value.type() != JsonValue::Type::String) continue;
    out.emplace_back(key, value.AsString());
  }
  return out;
}

const std::string* FindIn(const Entries& entries, std::string_view key) {
  for (const auto& [k, v] : entries)
    if (k == key) return &v;
  return nullptr;
}

bool IsSupported(std::string_view lang) {
  return std::find(kLanguages.begin(), kLanguages.end(), lang) != kLanguages.end();
}

// Nomi dei segnaposto {nome} di un testo, ordinati e senza ripetizioni.
std::set<std::string> Placeholders(std::string_view text) {
  std::set<std::string> out;
  size_t pos = 0;
  while ((pos = text.find('{', pos)) != std::string_view::npos) {
    const size_t end = text.find('}', pos + 1);
    if (end == std::string_view::npos) break;
    out.emplace(text.substr(pos + 1, end - pos - 1));
    pos = end + 1;
  }
  return out;
}

std::string NumberText(double n) {
  if (std::floor(n) == n && std::fabs(n) < 1e15) return std::to_string(static_cast<long long>(n));
  std::string s = std::to_string(n);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

}  // namespace

std::string PickLanguage(std::string_view setting, std::span<const std::string> preferred) {
  const std::string wanted = ToLowerAscii(TrimAscii(setting));
  if (wanted != "auto") return IsSupported(wanted) ? wanted : "en";
  for (const std::string& tag : preferred) {
    const std::string lower = ToLowerAscii(tag);
    const std::string prefix = lower.substr(0, lower.find_first_of("-_"));
    if (IsSupported(prefix)) return prefix;
  }
  return "en";
}

bool PluralIsOne(std::string_view lang, double n) { return lang == "fr" ? n < 2 : n == 1; }

Translations Translations::FromJson(std::string lang, const JsonValue& active, const JsonValue& english) {
  Translations t;
  t.lang_ = std::move(lang);
  t.active_ = ToEntries(active);
  t.english_ = ToEntries(english);
  return t;
}

const std::string* Translations::Find(std::string_view key) const {
  if (const std::string* v = FindIn(active_, key)) return v;
  return FindIn(english_, key);
}

std::string Translations::Get(std::string_view key) const {
  if (const std::string* v = Find(key)) return *v;
  if (on_missing_ && reported_.find(key) == reported_.end()) {
    reported_.emplace(key);
    on_missing_(std::string(key));
  }
  return std::string(key);
}

std::string Translations::Format(std::string_view key, const I18nParams& params) const {
  const std::string text = Get(key);
  std::string out;
  out.reserve(text.size());
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t open = text.find('{', pos);
    const size_t close = open == std::string::npos ? std::string::npos : text.find('}', open + 1);
    if (close == std::string::npos) break;
    out.append(text, pos, open - pos);
    const std::string_view name(text.data() + open + 1, close - open - 1);
    const auto it = std::find_if(params.begin(), params.end(), [&](const auto& p) { return p.first == name; });
    if (it != params.end()) {
      out += it->second;
      pos = close + 1;
    } else {
      out += '{';  // non e' un segnaposto con valore: si va avanti di un carattere
      pos = open + 1;
    }
  }
  out.append(text, pos, std::string::npos);
  return out;
}

std::string Translations::Plural(std::string_view key, double n, I18nParams params) const {
  const std::string form = std::string(key) + (PluralIsOne(lang_, n) ? "_one" : "_other");
  params.emplace_back("n", NumberText(n));
  return Format(form, params);
}

JsonValue Translations::Dictionary() const {
  JsonValue out = JsonValue::MakeObject();
  for (const auto& [k, v] : english_) out.Set(k, JsonValue(v));
  for (const auto& [k, v] : active_) out.Set(k, JsonValue(v));
  return out;
}

std::vector<std::string> Translations::MissingKeys() const {
  std::vector<std::string> out;
  for (const auto& [k, v] : english_)
    if (!FindIn(active_, k)) out.push_back(k);
  return out;
}

std::vector<std::string> CompareDictionaries(const JsonValue& reference, const JsonValue& other) {
  const Entries ref = ToEntries(reference);
  const Entries oth = ToEntries(other);
  std::vector<std::string> out;
  for (const auto& [k, v] : ref) {
    const std::string* o = FindIn(oth, k);
    if (!o) {
      out.push_back("chiave mancante: " + k);
    } else if (Placeholders(v) != Placeholders(*o)) {
      out.push_back("segnaposto diversi: " + k);
    }
  }
  for (const auto& [k, v] : oth)
    if (!FindIn(ref, k)) out.push_back("chiave in piu': " + k);
  return out;
}

}  // namespace oled
