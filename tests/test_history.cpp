// Storico delle statistiche: grafico del rischio, mappa di usura, app, contenuti,
// sessioni, giorni di fila, usura evitata e salvataggio.
#include <string>

#include "history.h"
#include "json.h"
#include "summary_model.h"
#include "test_framework.h"

using oled::ContentKind;
using oled::FrameSummary;
using oled::History;
using oled::JsonValue;
using oled::SecondSample;

namespace {

constexpr int64_t kDay = 86400;
// 2026-11-23 (lunedi') 00:00 ora locale, in secondi "locali" dall'epoca.
constexpr int64_t kMonday = 20780 * kDay;

FrameSummary Risk(double r) {
  FrameSummary s;
  s.risk = r;
  return s;
}

SecondSample Sec(int64_t local_s, int day_key, const FrameSummary* summary = nullptr) {
  SecondSample s;
  s.local_s = local_s;
  s.day_key = day_key;
  s.summary = summary;
  s.overlay_shown = summary != nullptr;
  s.monitor = "LG TV SSCR2";
  return s;
}

}  // namespace

TEST(history_weekday_and_add_days) {
  CHECK_EQ(oled::Weekday(20261123), 0);  // lunedi'
  CHECK_EQ(oled::Weekday(20261129), 6);  // domenica
  CHECK_EQ(oled::DayKeyAddDays(20261231, 1), 20270101);
  CHECK_EQ(oled::DayKeyAddDays(20240228, 1), 20240229);
  CHECK_EQ(oled::DayKeyAddDays(20260301, -1), 20260228);
  CHECK_EQ(oled::DayKeyAddDays(20261123, -7), 20261116);
}

TEST(history_timeline_buckets_average) {
  History h;
  const FrameSummary a = Risk(0.2), b = Risk(0.4);
  for (int i = 0; i < 300; ++i) h.AddSecond(Sec(kMonday + i, 20261123, &a));
  for (int i = 300; i < 600; ++i) h.AddSecond(Sec(kMonday + i, 20261123, &b));
  const auto t = h.Timeline(kMonday + 599, 2);
  CHECK_EQ(t.size(), size_t{2});
  CHECK(t[0].has_value() && t[1].has_value());
  if (t[0] && t[1]) {
    CHECK_NEAR(*t[0], 0.2, 1e-9);
    CHECK_NEAR(*t[1], 0.4, 1e-9);
  }
  CHECK_NEAR(h.RiskPeak(kMonday + 599, 2), 0.4, 1e-9);
}

TEST(history_timeline_gaps_are_empty) {
  History h;
  const FrameSummary a = Risk(0.3);
  h.AddSecond(Sec(kMonday, 20261123, &a));
  h.AddSecond(Sec(kMonday + 300, 20261123));  // nessuna misura
  h.AddSecond(Sec(kMonday + 600, 20261123, &a));
  const auto t = h.Timeline(kMonday + 600, 3);
  CHECK(t[0].has_value());
  CHECK(!t[1].has_value());
  CHECK(t[2].has_value());
  // Dopo 8 giorni i secchi vecchi spariscono, quelli intermedi sono vuoti.
  const int64_t later = kMonday + 8 * kDay;
  h.AddSecond(Sec(later, 20261201, &a));
  const auto week = h.Timeline(later, oled::kTimelineBuckets);
  CHECK_EQ(week.size(), size_t{oled::kTimelineBuckets});
  int filled = 0;
  for (const auto& v : week) filled += v.has_value() ? 1 : 0;
  CHECK_EQ(filled, 1);
}

TEST(history_wear_map_hotspot) {
  History h;
  FrameSummary s;
  s.zone_light.fill(0.25);
  s.zone_light[0] = 1.0;
  for (int i = 0; i < 3600; ++i) h.AddSecond(Sec(kMonday + i, 20261123, &s));
  const auto w = h.Wear("LG TV SSCR2");
  CHECK_EQ(w.hottest, 0);
  CHECK_NEAR(w.hottest_hours, 1.0, 1e-9);
  CHECK_EQ(w.hottest_key, std::string("wear.zone.top_left"));
  const double mean = (1.0 + 0.25 * (oled::kZones - 1)) / oled::kZones;
  CHECK_NEAR(w.concentration, 1.0 / mean, 1e-9);
  CHECK_EQ(w.over_double, 1);
  CHECK_NEAR(w.normalized[0], 1.0, 1e-9);
  CHECK_NEAR(w.normalized[1], 0.25, 1e-9);
  CHECK_NEAR(w.measured_hours, 1.0, 1e-9);
  CHECK_EQ(w.since_local_s, kMonday);
  s.zone_light.fill(0);
  s.zone_light[oled::kZones - 1] = 9;
  History c;
  c.AddSecond(Sec(kMonday, 20261123, &s));
  CHECK_EQ(c.Wear("LG TV SSCR2").hottest_key, std::string("wear.zone.bottom_right"));
}

TEST(history_wear_map_per_monitor) {
  History h;
  FrameSummary s;
  s.zone_light.fill(1);
  SecondSample a = Sec(kMonday, 20261123, &s);
  SecondSample b = Sec(kMonday + 1, 20261123, &s);
  b.monitor = "DELL";
  h.AddSecond(a);
  h.AddSecond(b);
  h.AddSecond(b);
  CHECK_NEAR(h.Wear("LG TV SSCR2").measured_hours, 1.0 / 3600, 1e-12);
  CHECK_NEAR(h.Wear("DELL").measured_hours, 2.0 / 3600, 1e-12);
  CHECK_EQ(h.Wear("nessuno").hottest, -1);
}

TEST(history_top_apps_and_content) {
  History h;
  const FrameSummary m = Risk(0.1);
  auto add = [&](int64_t t, int day, const char* app, bool paused, ContentKind kind, int n) {
    for (int i = 0; i < n; ++i) {
      SecondSample s = Sec(t + i, day, paused ? nullptr : &m);
      s.app = app;
      s.app_paused = paused;
      s.content = kind;
      h.AddSecond(s);
    }
  };
  add(kMonday - 8 * kDay, 20261115, "vecchio.exe", false, ContentKind::StillWork, 1000);  // fuori dai 7 giorni
  add(kMonday - 2 * kDay, 20261121, "code.exe", false, ContentKind::StillWork, 300);
  add(kMonday - kDay, 20261122, "game.exe", true, ContentKind::Games, 500);
  add(kMonday, 20261123, "code.exe", false, ContentKind::Video, 100);
  const auto top = h.TopApps(20261123, 8);
  CHECK_EQ(top.size(), size_t{2});
  if (top.size() == 2) {
    CHECK_EQ(top[0].exe, std::string("game.exe"));
    CHECK_NEAR(top[0].paused_s, 500, 1e-9);
    CHECK_EQ(top[1].exe, std::string("code.exe"));
    CHECK_NEAR(top[1].protected_s, 400, 1e-9);
  }
  const auto shares = h.ContentShares(20261123);
  double sum = 0;
  for (double v : shares) sum += v;
  CHECK_NEAR(sum, 1.0, 1e-9);
  CHECK_NEAR(shares[static_cast<int>(ContentKind::Games)], 500.0 / 900, 1e-9);
  CHECK_NEAR(shares[static_cast<int>(ContentKind::StillWork)], 300.0 / 900, 1e-9);
  CHECK_NEAR(History().ContentShares(20261123)[0], 0.0, 1e-9);
}

TEST(history_streak_counts_consecutive_days) {
  History h;
  const FrameSummary m = Risk(0);
  auto protect = [&](int64_t day_start, int day_key) {
    for (int i = 0; i < 1800; ++i) h.AddSecond(Sec(day_start + i, day_key, &m));
  };
  protect(kMonday + 8 * kDay, 20261201);
  protect(kMonday + 9 * kDay, 20261202);
  protect(kMonday + 10 * kDay, 20261203);
  protect(kMonday + 12 * kDay, 20261205);
  CHECK(h.Streak(20261205) == std::make_pair(1, 3));
  CHECK(h.Streak(20261206) == std::make_pair(1, 3));  // vale fino a ieri
  CHECK(h.Streak(20261207) == std::make_pair(0, 3));
  // Un giorno con meno di 30 minuti non conta.
  for (int i = 0; i < 1799; ++i) h.AddSecond(Sec(kMonday + 13 * kDay + i, 20261206, &m));
  CHECK(h.Streak(20261206) == std::make_pair(1, 3));
  // Fine anno.
  History y;
  for (int d = 0; d < 3; ++d) {
    const int keys[3] = {20261230, 20261231, 20270101};
    for (int i = 0; i < 1800; ++i) y.AddSecond(Sec(kMonday + (37 + d) * kDay + i, keys[d], &m));
  }
  CHECK(y.Streak(20270101) == std::make_pair(3, 3));
}

TEST(history_wear_avoided) {
  History h;
  CHECK(!h.WearAvoidedToday(20261123).has_value());
  CHECK(!h.WearAvoidedTotal().has_value());
  FrameSummary s;
  s.light_still_before = 1.0;
  s.light_still_removed = 0.3;
  h.AddSecond(Sec(kMonday, 20261123, &s));
  CHECK(h.WearAvoidedToday(20261123).has_value());
  CHECK_NEAR(h.WearAvoidedToday(20261123).value_or(0), 0.3, 1e-9);
  CHECK_NEAR(h.WearAvoidedTotal().value_or(0), 0.3, 1e-9);
  CHECK(!h.WearAvoidedToday(20261124).has_value());
}

TEST(history_sessions_week) {
  History h;
  h.CountSession(20261122);  // domenica della settimana prima
  h.CountSession(20261123);  // lunedi'
  h.CountSession(20261125);  // mercoledi'
  CHECK_EQ(h.SessionsTotal(), 3);
  CHECK_EQ(h.SessionsThisWeek(20261125), 2);
  const FrameSummary m = Risk(0);
  for (int i = 0; i < 100; ++i) h.AddSecond(Sec(kMonday - kDay + i, 20261122, &m));
  for (int i = 0; i < 50; ++i) h.AddSecond(Sec(kMonday + i, 20261123, &m));
  CHECK_NEAR(h.ProtectedThisWeekS(20261125), 50, 1e-9);
  h.Clear();
  CHECK_EQ(h.SessionsTotal(), 0);
}

namespace {
History Rich() {
  History h;
  FrameSummary s;
  s.risk = 0.25;
  s.light_still_before = 1;
  s.light_still_removed = 0.4;
  s.zone_light.fill(0.1);
  s.zone_light[40] = 2;
  for (int d = 0; d < 3; ++d) {
    const int keys[3] = {20261123, 20261124, 20261125};
    h.CountSession(keys[d]);
    for (int i = 0; i < 1900; ++i) {
      SecondSample x = Sec(kMonday + d * kDay + i, keys[d], &s);
      x.app = i % 2 ? "code.exe" : "chrome.exe";
      x.content = i % 3 ? ContentKind::StillWork : ContentKind::Video;
      h.AddSecond(x);
    }
  }
  return h;
}
}  // namespace

TEST(history_json_roundtrip) {
  const History a = Rich();
  const History b = History::FromJson(a.ToJson());
  const int64_t now = kMonday + 2 * kDay + 1899;
  CHECK(a.Timeline(now, oled::kTimelineBuckets) == b.Timeline(now, oled::kTimelineBuckets));
  const auto wa = a.Wear("LG TV SSCR2"), wb = b.Wear("LG TV SSCR2");
  CHECK_EQ(wb.hottest, 40);
  CHECK_NEAR(wb.hottest_hours, wa.hottest_hours, 1e-9);
  CHECK_EQ(wb.since_local_s, wa.since_local_s);
  CHECK_NEAR(wb.measured_hours, wa.measured_hours, 1e-9);
  CHECK_EQ(b.TopApps(20261125, 8).size(), size_t{2});
  CHECK(a.ContentShares(20261125) == b.ContentShares(20261125));
  CHECK(b.Streak(20261125) == std::make_pair(3, 3));
  CHECK_NEAR(b.WearAvoidedTotal().value_or(0), 0.4, 1e-9);
  CHECK_NEAR(b.WearAvoidedToday(20261125).value_or(0), 0.4, 1e-9);
  CHECK_EQ(b.SessionsTotal(), 3);
  CHECK_EQ(b.SessionsThisWeek(20261125), 3);
  CHECK_NEAR(b.ProtectedThisWeekS(20261125), 3 * 1900, 1e-9);
}

TEST(history_json_bad_input_is_empty) {
  for (const char* text : {"{}", "[]", "5", R"({"version": 99, "sessions_total": 4})",
                           R"({"version": 1, "sessions_total": -3, "timeline": "x", "days": [5, {"day": "a"}],
                               "wear": [{"monitor": 3}], "streak_days": [-1, "z"], "light_before_total": -2})"}) {
    const auto parsed = oled::ParseJson(text);
    CHECK(parsed.value.has_value());
    if (!parsed.value) continue;
    const History h = History::FromJson(*parsed.value);
    CHECK_EQ(h.SessionsTotal(), 0);
    CHECK(h.TopApps(20261125, 8).empty());
    CHECK(!h.WearAvoidedTotal().has_value());
    CHECK_EQ(h.Wear("LG TV SSCR2").hottest, -1);
  }
}

TEST(history_json_keeps_top_apps_per_day) {
  History h;
  for (int a = 0; a < 60; ++a) {
    for (int i = 0; i <= a; ++i) {
      SecondSample x = Sec(kMonday + a * 100 + i, 20261123);
      x.app = "app" + std::to_string(a) + ".exe";
      h.AddSecond(x);
    }
  }
  const History b = History::FromJson(h.ToJson());
  const auto top = b.TopApps(20261123, 100);
  CHECK_EQ(top.size(), size_t{50});
  CHECK_EQ(top.front().exe, std::string("app59.exe"));
  CHECK_EQ(top.back().exe, std::string("app10.exe"));
}

// Un file di un'altra versione o di forma sbagliata non va riusato (ne' sovrascritto in
// silenzio): il chiamante lo mette da parte.
TEST(history_json_usable_versions_1_and_2) {
  const auto usable = [](const char* text) {
    const auto parsed = oled::ParseJson(text);
    return parsed.value.has_value() && oled::HistoryJsonUsable(*parsed.value);
  };
  CHECK(usable(R"({"version": 1})"));
  CHECK(usable(R"({"version": 2, "timeline": []})"));
  CHECK(!usable(R"({"version": 3, "timeline": []})"));
  CHECK(!usable("{}"));
  CHECK(!usable("[]"));
  CHECK(!usable(R"({"version": "1"})"));
}

// L'orologio torna indietro (correzione manuale, data sbagliata all'avvio poi corretta):
// i secchi "nel futuro" si scartano e il grafico riprende subito.
TEST(history_timeline_recovers_after_clock_goes_back) {
  History h;
  const FrameSummary a = Risk(0.2), b = Risk(0.6);
  h.AddSecond(Sec(kMonday + 30 * kDay, 20261223, &a));  // data sbagliata, un mese avanti
  h.AddSecond(Sec(kMonday, 20261123, &b));               // orologio corretto
  h.AddSecond(Sec(kMonday + 300, 20261123, &b));
  const auto t = h.Timeline(kMonday + 300, 2);
  CHECK(t[0].has_value() && t[1].has_value());
  if (t[0] && t[1]) {
    CHECK_NEAR(*t[0], 0.6, 1e-9);
    CHECK_NEAR(*t[1], 0.6, 1e-9);
  }
  // Un piccolo passo indietro (ora legale, pochi minuti) resta nell'ultimo secchio.
  History d;
  d.AddSecond(Sec(kMonday + 600, 20261123, &a));
  d.AddSecond(Sec(kMonday + 599, 20261123, &b));
  const auto u = d.Timeline(kMonday + 600, 1);
  CHECK(u[0].has_value());
  if (u[0]) CHECK_NEAR(*u[0], 0.4, 1e-9);
}

namespace {

// history.json della 1.1.0 (versione 1): una mappa 32x18 con 2 ore nella zona 0.
std::string V1WithMap() {
  std::string hours = "[2";
  for (int z = 1; z < oled::kZones; ++z) hours += ",0";
  hours += "]";
  return R"({"version": 1, "wear": [{"monitor": "TV", "since": 1000, "measured_s": 7200, "hours": )" + hours + "}]}";
}

SecondSample OnMonitor(int64_t local_s, const FrameSummary* summary) {
  SecondSample x = Sec(local_s, 20261123, summary);
  x.monitor = "TV";
  return x;
}

}  // namespace

TEST(history_v1_map_kept_on_same_grid) {
  const auto parsed = oled::ParseJson(V1WithMap());
  CHECK(parsed.value.has_value());
  if (!parsed.value) return;
  History h = History::FromJson(*parsed.value);
  CHECK_NEAR(h.Wear("TV").hottest_hours, 2.0, 1e-9);
  FrameSummary s;  // griglia predefinita 32x18, come uno schermo 16:9
  s.zone_light[0] = 3600;  // un'ora in un secondo
  h.AddSecond(OnMonitor(kMonday, &s));
  const auto w = h.Wear("TV");
  CHECK(w.grid == (oled::ZoneGrid{32, 18}));
  CHECK_NEAR(w.hottest_hours, 3.0, 1e-9);
  CHECK_EQ(w.since_local_s, int64_t{1000});
}

TEST(history_grid_change_resets_map_only) {
  History h;
  FrameSummary wide;
  wide.risk = 0.5;
  wide.zone_light[0] = 1;
  for (int i = 0; i < 600; ++i) h.AddSecond(OnMonitor(kMonday + i, &wide));
  h.CountSession(20261123);
  FrameSummary ultra;
  ultra.grid = oled::ZoneGrid{38, 16};
  ultra.risk = 0.5;
  ultra.zone_light[607] = 1;  // ultima zona della griglia 38x16
  h.AddSecond(OnMonitor(kMonday + 600, &ultra));
  const auto w = h.Wear("TV");
  CHECK(w.grid == (oled::ZoneGrid{38, 16}));
  CHECK_EQ(w.normalized.size(), size_t{608});
  CHECK_EQ(w.hottest, 607);
  CHECK_EQ(w.hottest_key, std::string("wear.zone.bottom_right"));
  CHECK_NEAR(w.measured_hours, 1.0 / 3600, 1e-12);
  CHECK_EQ(w.since_local_s, kMonday + 600);
  CHECK_EQ(h.SessionsTotal(), 1);  // il resto dello storico resta
  const auto timeline = h.Timeline(kMonday + 601, 3);
  CHECK(timeline.back().has_value());
}

TEST(history_v2_roundtrip) {
  History h;
  FrameSummary ultra;
  ultra.grid = oled::ZoneGrid{38, 16};
  ultra.zone_light[100] = 2;
  for (int i = 0; i < 10; ++i) h.AddSecond(OnMonitor(kMonday + i, &ultra));
  const JsonValue j = h.ToJson();
  CHECK_NEAR(j.Find("version")->AsNumber(), 2, 1e-12);
  const History b = History::FromJson(j);
  const auto wa = h.Wear("TV"), wb = b.Wear("TV");
  CHECK(wb.grid == (oled::ZoneGrid{38, 16}));
  CHECK_EQ(wb.hottest, 100);
  CHECK_NEAR(wb.hottest_hours, wa.hottest_hours, 1e-12);
  CHECK(wb.normalized == wa.normalized);
}

TEST(history_v2_bad_wear_skipped) {
  const auto hours = [](int n) {
    std::string out = "[1";
    for (int i = 1; i < n; ++i) out += ",0";
    return out + "]";
  };
  const std::string bad[] = {
      R"({"version": 2, "wear": [{"monitor": "TV", "since": 1, "measured_s": 5, "cols": 0, "rows": 18, "hours": )" + hours(1) + "}]}",
      R"({"version": 2, "wear": [{"monitor": "TV", "since": 1, "measured_s": 5, "cols": 32, "rows": -1, "hours": )" + hours(576) + "}]}",
      R"({"version": 2, "wear": [{"monitor": "TV", "since": 1, "measured_s": 5, "cols": 40, "rows": 17, "hours": )" + hours(680) + "}]}",
      R"({"version": 2, "wear": [{"monitor": "TV", "since": 1, "measured_s": 5, "cols": 38, "rows": 16, "hours": )" + hours(576) + "}]}",
  };
  for (const std::string& text : bad) {
    const auto parsed = oled::ParseJson(text);
    CHECK(parsed.value.has_value());
    if (!parsed.value) continue;
    CHECK_EQ(History::FromJson(*parsed.value).Wear("TV").hottest, -1);
  }
}
