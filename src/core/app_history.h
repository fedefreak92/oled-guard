// Storico delle app che hanno messo in pausa l'overlay e regole di pausa per app.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "json.h"
#include "pause_rules.h"

namespace oled {

enum class AppRule { Auto, AlwaysPause, NeverPause };

struct AppEntry {
  std::string exe;  // minuscolo
  PauseReason reason = PauseReason::None;
  int64_t last_seen_s = 0;  // tempo Unix
  int count = 0;
};

class AppHistory {
 public:
  static constexpr size_t kMax = 50;

  void Record(std::string_view exe, PauseReason reason, int64_t now_s);
  void Remove(std::string_view exe);
  // Dalla piu' recente.
  const std::vector<AppEntry>& Entries() const { return entries_; }

  JsonValue ToJson() const;
  static AppHistory FromJson(const JsonValue& v);

 private:
  std::vector<AppEntry> entries_;
};

// Regola ricavata da denylist/allowlist della config (unica fonte di verita').
AppRule RuleFor(const Config& config, std::string_view exe);
// Toglie l'exe da entrambe le liste e lo aggiunge (minuscolo) a quella della regola.
Config WithRule(Config config, std::string_view exe, AppRule rule);
// Aggiunge l'exe (minuscolo) ad app_list se manca; nessun cambio di regola.
Config WithListedApp(Config config, std::string_view exe);
// Toglie l'exe da app_list, denylist, allowlist e dim_exclude.
Config WithoutApp(Config config, std::string_view exe);
// Aggiunge o toglie l'exe (minuscolo) da dim_exclude.
Config WithDimExclude(Config config, std::string_view exe, bool on);

}  // namespace oled
