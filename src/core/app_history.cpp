#include "app_history.h"

#include <algorithm>
#include <cmath>

#include "ascii.h"

namespace oled {
namespace {

bool Contains(const std::vector<std::string>& list, std::string_view exe) {
  return std::any_of(list.begin(), list.end(), [&](const std::string& s) { return EqualsIgnoreCase(s, exe); });
}

void Erase(std::vector<std::string>& list, std::string_view exe) {
  list.erase(std::remove_if(list.begin(), list.end(), [&](const std::string& s) { return EqualsIgnoreCase(s, exe); }),
             list.end());
}

}  // namespace

void AppHistory::Record(std::string_view exe, PauseReason reason, int64_t now_s) {
  if (exe.empty()) return;
  AppEntry entry{ToLowerAscii(exe), reason, now_s, 1};
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const AppEntry& e) { return EqualsIgnoreCase(e.exe, exe); });
  if (it != entries_.end()) {
    entry.count = it->count + 1;
    entries_.erase(it);
  }
  entries_.insert(entries_.begin(), std::move(entry));
  if (entries_.size() > kMax) entries_.resize(kMax);
}

void AppHistory::Remove(std::string_view exe) {
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const AppEntry& e) { return EqualsIgnoreCase(e.exe, exe); }),
                 entries_.end());
}

JsonValue AppHistory::ToJson() const {
  JsonValue arr = JsonValue::MakeArray();
  for (const auto& e : entries_) {
    JsonValue o = JsonValue::MakeObject();
    o.Set("exe", JsonValue(e.exe));
    o.Set("reason", JsonValue(static_cast<int>(e.reason)));
    o.Set("last_seen_s", JsonValue(static_cast<double>(e.last_seen_s)));
    o.Set("count", JsonValue(e.count));
    arr.Push(std::move(o));
  }
  return arr;
}

AppHistory AppHistory::FromJson(const JsonValue& v) {
  AppHistory h;
  if (v.type() != JsonValue::Type::Array) return h;
  // Il file e' salvato dalla piu' recente: si ricostruisce nello stesso ordine.
  for (const auto& item : v.AsArray()) {
    if (h.entries_.size() >= kMax) break;
    const JsonValue* exe = item.Find("exe");
    const JsonValue* reason = item.Find("reason");
    const JsonValue* seen = item.Find("last_seen_s");
    const JsonValue* count = item.Find("count");
    if (!exe || exe->type() != JsonValue::Type::String || exe->AsString().empty()) continue;
    if (!reason || reason->type() != JsonValue::Type::Number || !seen || seen->type() != JsonValue::Type::Number ||
        !count || count->type() != JsonValue::Type::Number) {
      continue;
    }
    const double r = reason->AsNumber();
    if (r < 0 || r > static_cast<double>(PauseReason::FullscreenWindow) || std::floor(r) != r) continue;
    if (count->AsNumber() < 1 || seen->AsNumber() < 0) continue;
    h.entries_.push_back(AppEntry{ToLowerAscii(exe->AsString()), static_cast<PauseReason>(static_cast<int>(r)),
                                  static_cast<int64_t>(seen->AsNumber()), static_cast<int>(count->AsNumber())});
  }
  return h;
}

AppRule RuleFor(const Config& config, std::string_view exe) {
  if (Contains(config.allowlist, exe)) return AppRule::NeverPause;
  if (Contains(config.denylist, exe)) return AppRule::AlwaysPause;
  return AppRule::Auto;
}

Config WithRule(Config config, std::string_view exe, AppRule rule) {
  Erase(config.denylist, exe);
  Erase(config.allowlist, exe);
  if (rule == AppRule::AlwaysPause) config.denylist.push_back(ToLowerAscii(exe));
  if (rule == AppRule::NeverPause) config.allowlist.push_back(ToLowerAscii(exe));
  return config;
}

Config WithListedApp(Config config, std::string_view exe) {
  if (!Contains(config.app_list, exe)) config.app_list.push_back(ToLowerAscii(exe));
  return config;
}

Config WithoutApp(Config config, std::string_view exe) {
  Erase(config.app_list, exe);
  Erase(config.denylist, exe);
  Erase(config.allowlist, exe);
  Erase(config.dim_exclude, exe);
  return config;
}

Config WithDimExclude(Config config, std::string_view exe, bool on) {
  Erase(config.dim_exclude, exe);
  if (on) config.dim_exclude.push_back(ToLowerAscii(exe));
  return config;
}

}  // namespace oled
