// Attivita' recente mostrata nella Panoramica: ultimi eventi in memoria (si perdono
// alla chiusura dell'app), eventi uguali ravvicinati fusi in uno.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

#include "i18n.h"

namespace oled {

struct ActivityEvent {
  int64_t t_ms = 0;  // tempo Unix in millisecondi
  std::string kind;  // "pause" | "resume" | "manual" | "test" | "away" | "back" | "monitor" | "windows" | "config"
  std::string key;   // chiave di traduzione
  I18nParams params; // segnaposto della chiave
  int count = 1;     // eventi uguali fusi
};

class ActivityLog {
 public:
  static constexpr size_t kMax = 100;
  static constexpr int64_t kMergeMs = 5000;

  // Restituisce l'evento aggiunto o aggiornato (fuso).
  const ActivityEvent& Add(int64_t t_ms, std::string_view kind, std::string_view key, I18nParams params = {});
  const std::deque<ActivityEvent>& Events() const { return events_; }  // dal piu' recente
  void Clear() { events_.clear(); }

 private:
  std::deque<ActivityEvent> events_;
};

}  // namespace oled
