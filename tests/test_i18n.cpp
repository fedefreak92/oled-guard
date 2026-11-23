#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "i18n.h"
#include "json.h"
#include "test_framework.h"

using oled::CompareDictionaries;
using oled::I18nParams;
using oled::JsonValue;
using oled::PickLanguage;
using oled::PluralIsOne;
using oled::Translations;

namespace {

JsonValue Dict(std::initializer_list<std::pair<const char*, const char*>> entries) {
  JsonValue o = JsonValue::MakeObject();
  for (const auto& [k, v] : entries) o.Set(k, JsonValue(v));
  return o;
}

std::string ReadFile(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST(i18n_pick_language) {
  const std::vector<std::string> de = {"de-AT", "en-US"};
  const std::vector<std::string> pt = {"pt-BR"};
  const std::vector<std::string> none;
  CHECK_EQ(PickLanguage("auto", de), std::string("de"));
  CHECK_EQ(PickLanguage("auto", pt), std::string("en"));
  CHECK_EQ(PickLanguage("auto", none), std::string("en"));
  CHECK_EQ(PickLanguage("fr", pt), std::string("fr"));
  CHECK_EQ(PickLanguage("xx", de), std::string("en"));
  const std::vector<std::string> upper = {"IT-it"};
  CHECK_EQ(PickLanguage("AUTO", upper), std::string("it"));
}

TEST(i18n_plural_rules) {
  CHECK(PluralIsOne("fr", 0));
  CHECK(PluralIsOne("fr", 1));
  CHECK(!PluralIsOne("fr", 2));
  CHECK(PluralIsOne("it", 1));
  CHECK(!PluralIsOne("it", 0));
  CHECK(!PluralIsOne("en", 2));
  CHECK(PluralIsOne("de", 1));
}

TEST(i18n_missing_key_falls_back_to_english) {
  const JsonValue en = Dict({{"a.only_en", "English only"}, {"a.both", "Both EN"}});
  const JsonValue de = Dict({{"a.both", "Beide"}});
  const Translations t = Translations::FromJson("de", de, en);
  CHECK_EQ(t.Language(), std::string("de"));
  CHECK_EQ(t.Get("a.both"), std::string("Beide"));
  CHECK_EQ(t.Get("a.only_en"), std::string("English only"));
  CHECK_EQ(t.Get("a.nowhere"), std::string("a.nowhere"));
  CHECK_EQ(t.MissingKeys(), std::vector<std::string>{"a.only_en"});
}

TEST(i18n_format_params) {
  const JsonValue en = Dict({{"x", "{n} min of {app}"}, {"y", "{missing} stays"}});
  const JsonValue it = Dict({{"x", "{n} min di {app}"}});
  const Translations t = Translations::FromJson("it", it, en);
  CHECK_EQ(t.Format("x", I18nParams{{"n", "5"}, {"app", "code.exe"}}), std::string("5 min di code.exe"));
  CHECK_EQ(t.Format("y", {}), std::string("{missing} stays"));
}

TEST(i18n_plural_picks_form) {
  const JsonValue en = Dict({{"d_one", "{n} day"}, {"d_other", "{n} days"}});
  const JsonValue fr = Dict({{"d_one", "{n} jour"}, {"d_other", "{n} jours"}});
  const Translations tfr = Translations::FromJson("fr", fr, en);
  CHECK_EQ(tfr.Plural("d", 0), std::string("0 jour"));
  CHECK_EQ(tfr.Plural("d", 3), std::string("3 jours"));
  const Translations ten = Translations::FromJson("en", en, en);
  CHECK_EQ(ten.Plural("d", 1), std::string("1 day"));
  CHECK_EQ(ten.Plural("d", 0), std::string("0 days"));
}

TEST(i18n_dictionary_merges_and_skips_meta) {
  JsonValue en = Dict({{"a", "A"}, {"b", "B"}});
  JsonValue meta = JsonValue::MakeObject();
  meta.Set("name", JsonValue("Italiano"));
  JsonValue it = Dict({{"a", "AI"}});
  it.Set("_meta", meta);
  const JsonValue d = Translations::FromJson("it", it, en).Dictionary();
  CHECK(d.Find("_meta") == nullptr);
  CHECK_EQ(d.Find("a")->AsString(), std::string("AI"));
  CHECK_EQ(d.Find("b")->AsString(), std::string("B"));
}

TEST(i18n_compare_dictionaries) {
  const JsonValue en = Dict({{"a", "x {n}"}, {"b", "y"}});
  CHECK(CompareDictionaries(en, Dict({{"a", "z {n}"}, {"b", "w"}})).empty());
  CHECK_EQ(CompareDictionaries(en, Dict({{"a", "z {n}"}})).size(), size_t{1});                          // manca b
  CHECK_EQ(CompareDictionaries(en, Dict({{"a", "z {n}"}, {"b", "w"}, {"c", "v"}})).size(), size_t{1});  // c in piu'
  CHECK_EQ(CompareDictionaries(en, Dict({{"a", "z {count}"}, {"b", "w"}})).size(), size_t{1});          // segnaposto
}

TEST(i18n_files_match_english) {
  const std::filesystem::path dir = std::filesystem::path(OLED_SOURCE_DIR) / "src" / "ui" / "i18n";
  const auto en = oled::ParseJson(ReadFile(dir / "en.json"));
  CHECK(en.value.has_value());
  if (!en.value) return;
  int files = 0;
  for (std::string_view lang : oled::kLanguages) {
    const auto path = dir / (std::string(lang) + ".json");
    if (!std::filesystem::exists(path)) continue;
    ++files;
    const auto parsed = oled::ParseJson(ReadFile(path));
    CHECK(parsed.value.has_value());
    if (!parsed.value) continue;
    const JsonValue* meta = parsed.value->Find("_meta");
    CHECK(meta && meta->Find("name") && !meta->Find("name")->AsString().empty());
    const JsonValue* review = meta ? meta->Find("review") : nullptr;
    CHECK(review && (review->AsString() == "native" || review->AsString() == "machine"));
    const auto diff = CompareDictionaries(*en.value, *parsed.value);
    for (const auto& d : diff) ::oled::test::ReportFailure(__FILE__, __LINE__, std::string(lang) + ": " + d);
  }
  CHECK(files >= 2);
}

namespace {

// Chiavi letterali usate nella pagina: t('k'), tp('k', ...) (che vuole k_one e k_other) e
// data-i18n="k" / data-i18n-title="k". Le chiavi costruite (template) non si controllano qui.
std::vector<std::string> UiKeys(const std::string& text) {
  std::vector<std::string> keys;
  const auto scan = [&](const std::string& prefix, const std::string& suffix_one, const std::string& suffix_other,
                        char close) {
    size_t pos = 0;
    while ((pos = text.find(prefix, pos)) != std::string::npos) {
      // "t(" non deve far parte di un nome piu' lungo (es. "toggle(", "setText(").
      const bool word_start = pos == 0 || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
      const size_t start = pos + prefix.size();
      const size_t end = text.find(close, start);
      pos = start;
      if (!word_start || end == std::string::npos) continue;
      const std::string key = text.substr(start, end - start);
      // Chiavi composte ('nav.' + id): non sono letterali da controllare.
      if (key.empty() || key.back() == '.' || key.find_first_of(" ${}\n") != std::string::npos) continue;
      if (suffix_one.empty()) {
        keys.push_back(key);
      } else {
        keys.push_back(key + suffix_one);
        keys.push_back(key + suffix_other);
      }
    }
  };
  scan("t('", "", "", '\'');
  scan("tp('", "_one", "_other", '\'');
  scan("data-i18n=\"", "", "", '"');
  scan("data-i18n-title=\"", "", "", '"');
  return keys;
}

}  // namespace

TEST(i18n_ui_key_scanner) {
  const auto keys = UiKeys("a = t('x.y'); b = tp('c.d', 2); setText(e, 'z'); toggle('q'); f(`${t('no space')}`); g(t('nav.' + id)); "
                           "<b data-i18n=\"m.n\" data-i18n-title=\"o.p\">");
  const std::vector<std::string> expected = {"x.y", "c.d_one", "c.d_other", "m.n", "o.p"};
  CHECK_EQ(keys, expected);
}

TEST(i18n_ui_keys_exist) {
  const std::filesystem::path ui = std::filesystem::path(OLED_SOURCE_DIR) / "src" / "ui";
  const auto en = oled::ParseJson(ReadFile(ui / "i18n" / "en.json"));
  CHECK(en.value.has_value());
  if (!en.value) return;
  size_t checked = 0;
  for (const char* file : {"app.js", "pages.js", "setup.js", "main.js", "index.html"}) {
    for (const std::string& key : UiKeys(ReadFile(ui / file))) {
      ++checked;
      if (!en.value->Find(key)) ::oled::test::ReportFailure(__FILE__, __LINE__, std::string(file) + ": chiave mancante " + key);
    }
  }
  CHECK(checked > 100);
}
