#include "stats_file.h"

#include <fstream>
#include <sstream>
#include <system_error>

#include "json.h"
#include "logger.h"

namespace oled {

StatsData LoadStats(const std::filesystem::path& file) {
  StatsData data;
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return data;
  std::ifstream in(file, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  const auto parsed = ParseJson(text.str());
  if (!parsed.value || parsed.value->type() != JsonValue::Type::Object) {
    LogWarn("stats.json non leggibile ({}): si riparte da zero", parsed.error);
    return data;
  }
  const JsonValue& root = *parsed.value;
  if (const JsonValue* wear = root.Find("wear")) data.wear = WearStats::FromJson(*wear);
  if (const JsonValue* apps = root.Find("apps")) data.apps = AppHistory::FromJson(*apps);
  if (const JsonValue* r = root.Find("ui_rect"); r && r->type() == JsonValue::Type::Array && r->AsArray().size() == 4) {
    const auto& a = r->AsArray();
    bool numbers = true;
    for (const auto& v : a) numbers = numbers && v.type() == JsonValue::Type::Number;
    if (numbers) {
      RECT rect{static_cast<LONG>(a[0].AsNumber()), static_cast<LONG>(a[1].AsNumber()),
                static_cast<LONG>(a[2].AsNumber()), static_cast<LONG>(a[3].AsNumber())};
      if (rect.right - rect.left >= 200 && rect.bottom - rect.top >= 200) data.ui_rect = rect;
    }
  }
  return data;
}

bool SaveStats(const std::filesystem::path& file, const StatsData& data) {
  JsonValue root = JsonValue::MakeObject();
  root.Set("wear", data.wear.ToJson());
  root.Set("apps", data.apps.ToJson());
  if (data.ui_rect) {
    JsonValue r = JsonValue::MakeArray();
    for (LONG v : {data.ui_rect->left, data.ui_rect->top, data.ui_rect->right, data.ui_rect->bottom}) {
      r.Push(JsonValue(static_cast<double>(v)));
    }
    root.Set("ui_rect", r);
  }
  const std::filesystem::path temp = file.wstring() + L".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << SerializeJson(root, 0) << "\n";
    if (!out) return false;
  }
  return MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

}  // namespace oled
