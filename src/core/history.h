// Storico delle statistiche (history.json): grafico del rischio a secchi di 5 minuti per
// 7 giorni, mappa di usura per monitor, uso giornaliero (protetto, contenuti, app, luce
// sulle zone ferme), sessioni e giorni di fila. Logica pura: ora e giorno arrivano
// dall'esterno in ora locale.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "json.h"
#include "summary_model.h"

namespace oled {

enum class ContentKind { StillWork, Video, Games, Away, ManualPause };
inline constexpr int kContentKinds = 5;
inline constexpr int kBucketS = 300;
inline constexpr int kTimelineBuckets = 2016;  // 7 giorni
inline constexpr int kDaysKept = 8;
inline constexpr int kAppsPerDay = 50;
inline constexpr double kStreakDayS = 1800;
inline constexpr int kStreakDaysKept = 400;

// Un secondo di vita dell'app, gia' classificato dal chiamante.
struct SecondSample {
  int64_t local_s = 0;                    // secondi locali dall'epoca (ora locale come se fosse UTC)
  int day_key = 0;                        // AAAAMMGG locale
  const FrameSummary* summary = nullptr;  // null = nessuna misura in questo secondo
  bool overlay_shown = false;
  std::optional<ContentKind> content;     // nessuna = non conta (blocco, errori)
  std::string app;                        // exe in primo piano ("" = nessuna o statistiche per app spente)
  bool app_paused = false;                // tempo dell'app in pausa (altrimenti protetto)
  std::string monitor;                    // nome amichevole del monitor protetto
};

struct WearView {
  std::array<double, kZones> normalized{};  // 0-1 rispetto alla zona peggiore
  int hottest = -1;                         // -1: nessun dato
  double hottest_hours = 0;
  std::string hottest_label;                // "in alto a sinistra"...
  double concentration = 0;                 // zona peggiore / media
  int over_double = 0;                      // zone oltre il doppio della media
  double measured_hours = 0;
  int64_t since_local_s = 0;
};

struct AppView {
  std::string exe;
  double protected_s = 0;
  double paused_s = 0;
};

class History {
 public:
  void AddSecond(const SecondSample& s);
  void CountSession(int day_key);
  void Clear() { *this = History{}; }

  // Media del rischio per secchio, dal piu' vecchio; nullopt dove non ci sono misure.
  std::vector<std::optional<double>> Timeline(int64_t now_local_s, int buckets) const;
  double RiskPeak(int64_t now_local_s, int buckets) const;
  WearView Wear(const std::string& monitor) const;
  std::vector<AppView> TopApps(int today_key, int n) const;              // ultimi 7 giorni
  std::array<double, kContentKinds> ContentShares(int today_key) const;  // frazioni, somma 1 (o tutte 0)
  std::pair<int, int> Streak(int today_key) const;                       // attuale, record
  std::optional<double> WearAvoidedToday(int today_key) const;
  std::optional<double> WearAvoidedTotal() const;
  int SessionsTotal() const { return sessions_total_; }
  int SessionsThisWeek(int today_key) const;
  double ProtectedThisWeekS(int today_key) const;

  JsonValue ToJson() const;
  static History FromJson(const JsonValue& v);  // campi sbagliati ignorati

 private:
  struct Bucket {
    int64_t index = 0;
    double sum = 0;
    int count = 0;
  };
  struct WearMap {
    std::array<double, kZones> hours{};
    int64_t since_local_s = 0;
    double measured_s = 0;
  };
  struct AppTime {
    double protected_s = 0;
    double paused_s = 0;
  };
  struct DayUsage {
    double protected_s = 0;
    std::array<double, kContentKinds> content_s{};
    std::map<std::string, AppTime> apps;
    double light_before = 0;
    double light_removed = 0;
  };

  void AddStreakDay(int day_key);
  bool InLastWeek(int day_key, int today_key) const;

  std::deque<Bucket> timeline_;
  std::map<std::string, WearMap> wear_;
  std::map<int, DayUsage> days_;
  std::map<int, int> sessions_by_day_;  // ultimi 14 giorni
  int sessions_total_ = 0;
  std::vector<int> streak_days_;        // ordinati, giorni con almeno 30 minuti
  int best_streak_ = 0;
  double light_before_total_ = 0;
  double light_removed_total_ = 0;
};

// Vero se il JSON e' uno storico di questa versione (oggetto con "version": 1). Un file
// di un'altra versione non va letto ne' sovrascritto: il chiamante lo mette da parte.
bool HistoryJsonUsable(const JsonValue& v);

int DayKeyAddDays(int day_key, int days);  // calendario gregoriano
int Weekday(int day_key);                  // 0 = lunedi'

}  // namespace oled
