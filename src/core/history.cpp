#include "history.h"

#include <algorithm>
#include <cmath>

namespace oled {
namespace {

// Giorni dall'epoca <-> data civile (algoritmo di H. Hinnant).
int64_t DaysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

int CivilFromDays(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  const int y = static_cast<int>(yoe + era * 400 + (m <= 2));
  return y * 10000 + m * 100 + d;
}

int64_t DayNumber(int day_key) { return DaysFromCivil(day_key / 10000, day_key / 100 % 100, day_key % 100); }

// Zona 32x18 descritta a parole (griglia 3x3).
std::string ZoneLabel(int zone) {
  const int zx = zone % kZoneCols, zy = zone / kZoneCols;
  const int col = zx * 3 / kZoneCols, row = zy * 3 / kZoneRows;
  static const char* kRows[3] = {"in alto", "in mezzo", "in basso"};
  static const char* kCols[3] = {"a sinistra", "al centro", "a destra"};
  if (row == 1 && col == 1) return "al centro";
  return std::string(kRows[row]) + " " + kCols[col];
}

}  // namespace

int DayKeyAddDays(int day_key, int days) { return CivilFromDays(DayNumber(day_key) + days); }

int Weekday(int day_key) {
  // 1970-01-01 era un giovedi' (3 con lunedi' = 0).
  const int64_t n = DayNumber(day_key);
  return static_cast<int>(((n % 7) + 7 + 3) % 7);
}

void History::AddSecond(const SecondSample& s) {
  DayUsage& day = days_[s.day_key];

  if (const FrameSummary* f = s.summary) {
    const int64_t index = s.local_s / kBucketS;
    if (timeline_.empty() || timeline_.back().index < index) timeline_.push_back(Bucket{index, 0, 0});
    // Un orologio che torna indietro (ora legale) aggiunge all'ultimo secchio.
    Bucket& b = timeline_.back();
    b.sum += f->risk;
    ++b.count;
    while (!timeline_.empty() && timeline_.front().index <= timeline_.back().index - kTimelineBuckets) {
      timeline_.pop_front();
    }
    auto [it, inserted] = wear_.try_emplace(s.monitor);
    if (inserted) it->second.since_local_s = s.local_s;
    for (int z = 0; z < kZones; ++z) it->second.hours[z] += f->zone_light[z] / 3600.0;
    it->second.measured_s += 1;
    day.light_before += f->light_still_before;
    day.light_removed += f->light_still_removed;
    light_before_total_ += f->light_still_before;
    light_removed_total_ += f->light_still_removed;
  }
  if (s.overlay_shown) {
    const bool was_short = day.protected_s < kStreakDayS;
    day.protected_s += 1;
    if (was_short && day.protected_s >= kStreakDayS) AddStreakDay(s.day_key);
  }
  if (s.content) day.content_s[static_cast<int>(*s.content)] += 1;
  if (!s.app.empty()) {
    AppTime& a = day.apps[s.app];
    (s.app_paused ? a.paused_s : a.protected_s) += 1;
  }
  // Solo gli ultimi giorni (dopo l'uso di `day`, che potrebbe essere il piu' vecchio).
  while (days_.size() > static_cast<size_t>(kDaysKept)) days_.erase(days_.begin());
}

void History::AddStreakDay(int day_key) {
  if (std::find(streak_days_.begin(), streak_days_.end(), day_key) != streak_days_.end()) return;
  streak_days_.insert(std::upper_bound(streak_days_.begin(), streak_days_.end(), day_key), day_key);
  if (streak_days_.size() > static_cast<size_t>(kStreakDaysKept)) streak_days_.erase(streak_days_.begin());
  // Serie che finisce in questo giorno.
  int run = 0;
  for (int d = day_key; std::binary_search(streak_days_.begin(), streak_days_.end(), d); d = DayKeyAddDays(d, -1)) {
    ++run;
  }
  best_streak_ = std::max(best_streak_, run);
}

void History::CountSession(int day_key) {
  ++sessions_total_;
  ++sessions_by_day_[day_key];
  const int oldest = DayKeyAddDays(sessions_by_day_.rbegin()->first, -14);
  while (!sessions_by_day_.empty() && sessions_by_day_.begin()->first <= oldest) {
    sessions_by_day_.erase(sessions_by_day_.begin());
  }
}

std::vector<std::optional<double>> History::Timeline(int64_t now_local_s, int buckets) const {
  std::vector<std::optional<double>> out(static_cast<size_t>(std::max(0, buckets)));
  const int64_t end = now_local_s / kBucketS;
  const int64_t start = end - buckets + 1;
  for (const Bucket& b : timeline_) {
    if (b.index < start || b.index > end || b.count == 0) continue;
    out[static_cast<size_t>(b.index - start)] = b.sum / b.count;
  }
  return out;
}

double History::RiskPeak(int64_t now_local_s, int buckets) const {
  double peak = 0;
  for (const auto& v : Timeline(now_local_s, buckets)) {
    if (v) peak = std::max(peak, *v);
  }
  return peak;
}

WearView History::Wear(const std::string& monitor) const {
  WearView v;
  const auto it = wear_.find(monitor);
  if (it == wear_.end() || it->second.measured_s <= 0) return v;
  const WearMap& m = it->second;
  double max = 0, sum = 0;
  for (int z = 0; z < kZones; ++z) {
    sum += m.hours[z];
    if (m.hours[z] > max) {
      max = m.hours[z];
      v.hottest = z;
    }
  }
  v.measured_hours = m.measured_s / 3600.0;
  v.since_local_s = m.since_local_s;
  if (v.hottest < 0) return v;
  const double mean = sum / kZones;
  v.hottest_hours = max;
  v.hottest_label = ZoneLabel(v.hottest);
  v.concentration = mean > 0 ? max / mean : 0;
  for (int z = 0; z < kZones; ++z) {
    v.normalized[z] = m.hours[z] / max;
    if (m.hours[z] > 2 * mean) ++v.over_double;
  }
  return v;
}

bool History::InLastWeek(int day_key, int today_key) const {
  return day_key <= today_key && day_key > DayKeyAddDays(today_key, -7);
}

std::vector<AppView> History::TopApps(int today_key, int n) const {
  std::map<std::string, AppView> sum;
  for (const auto& [key, day] : days_) {
    if (!InLastWeek(key, today_key)) continue;
    for (const auto& [exe, t] : day.apps) {
      AppView& a = sum[exe];
      a.exe = exe;
      a.protected_s += t.protected_s;
      a.paused_s += t.paused_s;
    }
  }
  std::vector<AppView> out;
  for (auto& [exe, a] : sum) out.push_back(a);
  std::sort(out.begin(), out.end(), [](const AppView& a, const AppView& b) {
    const double ta = a.protected_s + a.paused_s, tb = b.protected_s + b.paused_s;
    return ta != tb ? ta > tb : a.exe < b.exe;
  });
  if (out.size() > static_cast<size_t>(std::max(0, n))) out.resize(static_cast<size_t>(std::max(0, n)));
  return out;
}

std::array<double, kContentKinds> History::ContentShares(int today_key) const {
  std::array<double, kContentKinds> s{};
  double total = 0;
  for (const auto& [key, day] : days_) {
    if (!InLastWeek(key, today_key)) continue;
    for (int k = 0; k < kContentKinds; ++k) {
      s[k] += day.content_s[k];
      total += day.content_s[k];
    }
  }
  if (total > 0) {
    for (double& v : s) v /= total;
  }
  return s;
}

std::pair<int, int> History::Streak(int today_key) const {
  const auto has = [&](int d) { return std::binary_search(streak_days_.begin(), streak_days_.end(), d); };
  int day = has(today_key) ? today_key : DayKeyAddDays(today_key, -1);
  int run = 0;
  while (has(day)) {
    ++run;
    day = DayKeyAddDays(day, -1);
  }
  return {run, std::max(best_streak_, run)};
}

std::optional<double> History::WearAvoidedToday(int today_key) const {
  const auto it = days_.find(today_key);
  if (it == days_.end() || it->second.light_before <= 0) return std::nullopt;
  return it->second.light_removed / it->second.light_before;
}

std::optional<double> History::WearAvoidedTotal() const {
  if (light_before_total_ <= 0) return std::nullopt;
  return light_removed_total_ / light_before_total_;
}

int History::SessionsThisWeek(int today_key) const {
  const int monday = DayKeyAddDays(today_key, -Weekday(today_key));
  int n = 0;
  for (const auto& [key, count] : sessions_by_day_) {
    if (key >= monday && key <= today_key) n += count;
  }
  return n;
}

double History::ProtectedThisWeekS(int today_key) const {
  const int monday = DayKeyAddDays(today_key, -Weekday(today_key));
  double s = 0;
  for (const auto& [key, day] : days_) {
    if (key >= monday && key <= today_key) s += day.protected_s;
  }
  return s;
}

}  // namespace oled
