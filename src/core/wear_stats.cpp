#include "wear_stats.h"

#include <cmath>
#include <cstdlib>

namespace oled {
namespace {

// Numero valido e non negativo, altrimenti nullopt.
std::optional<double> NonNegative(const JsonValue* v) {
  if (!v || v->type() != JsonValue::Type::Number || !std::isfinite(v->AsNumber()) || v->AsNumber() < 0) {
    return std::nullopt;
  }
  return v->AsNumber();
}

}  // namespace

int WearStats::Index(Offset at) {
  if (std::abs(at.x) > kRadius || std::abs(at.y) > kRadius) return -1;
  return (at.y + kRadius) * kSide + (at.x + kRadius);
}

void WearStats::AddProtectedTime(Offset at, double ms) {
  if (!(ms > 0) || !std::isfinite(ms)) return;
  today_ms_ += ms;
  total_ms_ += ms;
  const int i = Index(at);
  if (i >= 0) cells_[static_cast<size_t>(i)] += ms;
}

void WearStats::CountPause(PauseReason reason) {
  const int i = static_cast<int>(reason);
  if (i >= 0 && i < kReasons) ++pauses_today_[static_cast<size_t>(i)];
}

void WearStats::RollDay(int day_key) {
  if (day_key == day_key_) return;
  day_key_ = day_key;
  today_ms_ = 0;
  pauses_today_.fill(0);
}

double WearStats::CellMs(Offset at) const {
  const int i = Index(at);
  return i >= 0 ? cells_[static_cast<size_t>(i)] : 0.0;
}

int WearStats::TodayPauses() const {
  int total = 0;
  for (int n : pauses_today_) total += n;
  return total;
}

int WearStats::TodayPauses(PauseReason reason) const {
  const int i = static_cast<int>(reason);
  return i >= 0 && i < kReasons ? pauses_today_[static_cast<size_t>(i)] : 0;
}

std::optional<double> WearStats::Uniformity(int amplitude) const {
  const int a = amplitude < 1 ? 1 : (amplitude > kRadius ? kRadius : amplitude);
  double sum = 0;
  int n = 0;
  for (int y = -a; y <= a; ++y) {
    for (int x = -a; x <= a; ++x) {
      sum += CellMs({x, y});
      ++n;
    }
  }
  if (sum < 600000.0) return std::nullopt;  // meno di 10 minuti: dato poco significativo
  const double mean = sum / n;
  double var = 0;
  for (int y = -a; y <= a; ++y) {
    for (int x = -a; x <= a; ++x) {
      const double d = CellMs({x, y}) - mean;
      var += d * d;
    }
  }
  const double cv = std::sqrt(var / n) / mean;
  const double u = 100.0 * (1.0 - cv);
  return u < 0 ? 0.0 : (u > 100 ? 100.0 : u);
}

JsonValue WearStats::ToJson() const {
  JsonValue o = JsonValue::MakeObject();
  o.Set("day", JsonValue(day_key_));
  o.Set("today_ms", JsonValue(today_ms_));
  o.Set("total_ms", JsonValue(total_ms_));
  JsonValue cells = JsonValue::MakeArray();
  for (double c : cells_) cells.Push(JsonValue(c));
  o.Set("cells", cells);
  JsonValue pauses = JsonValue::MakeArray();
  for (int p : pauses_today_) pauses.Push(JsonValue(p));
  o.Set("pauses_today", pauses);
  return o;
}

WearStats WearStats::FromJson(const JsonValue& v) {
  WearStats s;
  if (v.type() != JsonValue::Type::Object) return s;
  if (auto day = NonNegative(v.Find("day"))) s.day_key_ = static_cast<int>(*day);
  if (auto today = NonNegative(v.Find("today_ms"))) s.today_ms_ = *today;
  if (auto total = NonNegative(v.Find("total_ms"))) s.total_ms_ = *total;
  if (const JsonValue* cells = v.Find("cells"); cells && cells->type() == JsonValue::Type::Array &&
                                                cells->AsArray().size() == s.cells_.size()) {
    for (size_t i = 0; i < s.cells_.size(); ++i) {
      if (auto c = NonNegative(&cells->AsArray()[i])) s.cells_[i] = *c;
    }
  }
  if (const JsonValue* pauses = v.Find("pauses_today"); pauses && pauses->type() == JsonValue::Type::Array &&
                                                        pauses->AsArray().size() == s.pauses_today_.size()) {
    for (size_t i = 0; i < s.pauses_today_.size(); ++i) {
      if (auto p = NonNegative(&pauses->AsArray()[i])) s.pauses_today_[i] = static_cast<int>(*p);
    }
  }
  if (s.today_ms_ > s.total_ms_) s.total_ms_ = s.today_ms_;
  return s;
}

}  // namespace oled
